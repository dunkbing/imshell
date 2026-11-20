#include "imshell/video_js.hpp"

#include "imshell/embedded.hpp"
#include "imshell/video_cli.hpp"
#include "imshell/video_effects.hpp"
#include "imshell/video_footage.hpp"
#include "imshell/video_scenes.hpp"

#include "imgui.h"
#include "quickjs.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// the script's one JS context and the little state its calls share. a frame is a pure function of its number, so
// nothing here survives a draw: the layer is reset, Math.random is reseeded from the frame, Date.now is 0
namespace imshell::video {
    namespace {
        struct State {
            JSContext* ctx = nullptr;
            JSValue config = JS_UNDEFINED;
            std::string script;
            bool back = false;
            uint64_t rng = 1;
            ImFont* font = nullptr;
            int fps = 30;
            std::map<std::string, std::unique_ptr<Footage>> footage;
        };
        State g;

        [[noreturn]] void fail(JSContext* c) {
            JSValue exc = JS_GetException(c);
            const char* message = JS_ToCString(c, exc);
            JSValue stack = JS_GetPropertyStr(c, exc, "stack");
            const char* where = JS_IsUndefined(stack) ? nullptr : JS_ToCString(c, stack);
            std::fprintf(stderr, "error: %s\n%s", message ? message : "(no message)", where ? where : "");
            std::fflush(stderr);
            std::exit(1);
        }

        double num(JSContext* c, int argc, JSValueConst* argv, int i, double fallback = 0.0) {
            double d = fallback;
            if (i < argc && !JS_IsUndefined(argv[i]) && !JS_IsNull(argv[i])) {
                JS_ToFloat64(c, &d, argv[i]);
            }
            return d;
        }

        std::string str(JSContext* c, JSValueConst v) {
            const char* s = JS_ToCString(c, v);
            std::string out = s ? s : "";
            JS_FreeCString(c, s);
            return out;
        }

        std::string text(JSContext* c, int argc, JSValueConst* argv, int i, const char* fallback = "") {
            return i < argc && !JS_IsUndefined(argv[i]) ? str(c, argv[i]) : fallback;
        }

        // "#rgb", "#rgba", "#rrggbb", "#rrggbbaa", "rgb(r, g, b)" and "rgba(r, g, b, a)" (a 0..1); false for anything else
        bool parseColour(std::string s, uint32_t& out) {
            if (!s.empty() && s[0] == '#') {
                s.erase(0, 1);
                if (s.size() == 3 || s.size() == 4) { // each digit doubled
                    std::string wide;
                    for (char ch : s) {
                        wide += ch;
                        wide += ch;
                    }
                    s = wide;
                }
                if ((s.size() != 6 && s.size() != 8) || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
                    return false;
                }
                const uint32_t v = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
                out = s.size() == 8 ? IM_COL32((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255) : IM_COL32((v >> 16) & 255, (v >> 8) & 255, v & 255, 255);
                return true;
            }
            if (s.rfind("rgb", 0) == 0) {
                const size_t open = s.find('('), close = s.find(')');
                if (open == std::string::npos || close == std::string::npos || close < open) {
                    return false;
                }
                std::stringstream parts(s.substr(open + 1, close - open - 1));
                double v[4] = {0, 0, 0, 1};
                int n = 0;
                std::string piece;
                while (n < 4 && std::getline(parts, piece, ',')) {
                    char* end = nullptr;
                    v[n++] = std::strtod(piece.c_str(), &end);
                    if (end == piece.c_str()) {
                        return false;
                    }
                }
                if (n < 3) {
                    return false;
                }
                out = IM_COL32(static_cast<int>(std::clamp(v[0], 0.0, 255.0)), static_cast<int>(std::clamp(v[1], 0.0, 255.0)), static_cast<int>(std::clamp(v[2], 0.0, 255.0)),
                               static_cast<int>(std::clamp(v[3], 0.0, 1.0) * 255.0 + 0.5));
                return true;
            }
            return false;
        }

        // a colour is rgba(...)'s number or a string above; the current ImGui alpha scales it, so a scene's dissolve fades it
        ImU32 colour(JSContext* c, int argc, JSValueConst* argv, int i) {
            uint32_t value = 0xffffffffu;
            if (i < argc && JS_IsString(argv[i])) {
                const std::string s = str(c, argv[i]);
                if (!parseColour(s, value)) {
                    JS_ThrowTypeError(c, "not a colour: '%s' (use '#rrggbb', '#rrggbbaa', 'rgb(r, g, b)', 'rgba(r, g, b, a)' or rgba(r, g, b, a))", s.c_str());
                    fail(c);
                }
            } else if (i < argc) {
                value = static_cast<uint32_t>(num(c, argc, argv, i));
            }
            const float alpha = ImGui::GetStyle().Alpha * static_cast<float>(value >> IM_COL32_A_SHIFT) / 255.f;
            return (value & ~IM_COL32_A_MASK) | (static_cast<uint32_t>(std::clamp(alpha, 0.f, 1.f) * 255.f + 0.5f) << IM_COL32_A_SHIFT);
        }

        ImDrawList* list() { return g.back ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList(); }

        Ease easeNamed(const std::string& name) {
            if (name == "in") return Ease::InCubic;
            if (name == "out") return Ease::OutCubic;
            if (name == "inOut") return Ease::InOutCubic;
            if (name == "back") return Ease::OutBack;
            return Ease::Linear;
        }

        Fit fitNamed(const std::string& name) {
            if (name == "contain") return Fit::Contain;
            if (name == "stretch") return Fit::Stretch;
            return Fit::Cover;
        }

        // an array [a, b] argument
        Range range(JSContext* c, JSValueConst v) {
            Range r;
            JSValue a = JS_GetPropertyUint32(c, v, 0), b = JS_GetPropertyUint32(c, v, 1);
            double x = 0, y = 1;
            JS_ToFloat64(c, &x, a);
            JS_ToFloat64(c, &y, b);
            JS_FreeValue(c, a);
            JS_FreeValue(c, b);
            r.from = static_cast<float>(x);
            r.to = static_cast<float>(y);
            return r;
        }

#define FN(name) JSValue name(JSContext* c, JSValueConst, int argc, JSValueConst* argv)

        FN(jsRgba) {
            const float a = static_cast<float>(num(c, argc, argv, 3, 1.0));
            return JS_NewFloat64(c, static_cast<double>(IM_COL32(static_cast<int>(num(c, argc, argv, 0)), static_cast<int>(num(c, argc, argv, 1)), static_cast<int>(num(c, argc, argv, 2)),
                                                                 static_cast<int>(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f))));
        }

        FN(jsLayer) {
            g.back = text(c, argc, argv, 0) == "back";
            return JS_UNDEFINED;
        }

        FN(jsRect) {
            list()->AddRectFilled(ImVec2(static_cast<float>(num(c, argc, argv, 0)), static_cast<float>(num(c, argc, argv, 1))),
                                  ImVec2(static_cast<float>(num(c, argc, argv, 0) + num(c, argc, argv, 2)), static_cast<float>(num(c, argc, argv, 1) + num(c, argc, argv, 3))), colour(c, argc, argv, 4),
                                  static_cast<float>(num(c, argc, argv, 5)));
            return JS_UNDEFINED;
        }

        FN(jsStrokeRect) {
            list()->AddRect(ImVec2(static_cast<float>(num(c, argc, argv, 0)), static_cast<float>(num(c, argc, argv, 1))),
                            ImVec2(static_cast<float>(num(c, argc, argv, 0) + num(c, argc, argv, 2)), static_cast<float>(num(c, argc, argv, 1) + num(c, argc, argv, 3))), colour(c, argc, argv, 4),
                            static_cast<float>(num(c, argc, argv, 6)), 0, static_cast<float>(num(c, argc, argv, 5, 1.0)));
            return JS_UNDEFINED;
        }

        FN(jsCircle) {
            list()->AddCircleFilled(ImVec2(static_cast<float>(num(c, argc, argv, 0)), static_cast<float>(num(c, argc, argv, 1))), static_cast<float>(num(c, argc, argv, 2)), colour(c, argc, argv, 3), 64);
            return JS_UNDEFINED;
        }

        FN(jsRing) {
            list()->AddCircle(ImVec2(static_cast<float>(num(c, argc, argv, 0)), static_cast<float>(num(c, argc, argv, 1))), static_cast<float>(num(c, argc, argv, 2)), colour(c, argc, argv, 3), 64,
                              static_cast<float>(num(c, argc, argv, 4, 1.0)));
            return JS_UNDEFINED;
        }

        FN(jsLine) {
            list()->AddLine(ImVec2(static_cast<float>(num(c, argc, argv, 0)), static_cast<float>(num(c, argc, argv, 1))), ImVec2(static_cast<float>(num(c, argc, argv, 2)), static_cast<float>(num(c, argc, argv, 3))),
                            colour(c, argc, argv, 4), static_cast<float>(num(c, argc, argv, 5, 1.0)));
            return JS_UNDEFINED;
        }

        FN(jsText) {
            const std::string s = text(c, argc, argv, 0);
            list()->AddText(g.font, static_cast<float>(num(c, argc, argv, 3, 32.0)), ImVec2(static_cast<float>(num(c, argc, argv, 1)), static_cast<float>(num(c, argc, argv, 2))), colour(c, argc, argv, 4), s.c_str());
            return JS_UNDEFINED;
        }

        // [width, height] of text(s, ..., size)
        FN(jsMeasure) {
            const std::string s = text(c, argc, argv, 0);
            const ImVec2 box = g.font->CalcTextSizeA(static_cast<float>(num(c, argc, argv, 1, 32.0)), 1e9f, 0.f, s.c_str());
            JSValue out = JS_NewArray(c);
            JS_SetPropertyUint32(c, out, 0, JS_NewFloat64(c, box.x));
            JS_SetPropertyUint32(c, out, 1, JS_NewFloat64(c, box.y));
            return out;
        }

        // image(path, x, y, w, h, rounding, "cover" | "contain" | "stretch"): a png or jpeg, or a frame of a video (at `time` seconds)
        FN(jsImage) {
            const std::string path = text(c, argc, argv, 0);
            std::unique_ptr<Footage>& clip = g.footage[path];
            if (!clip) {
                clip = std::make_unique<Footage>(path, g.fps);
            }
            if (!clip->ok()) {
                return JS_ThrowReferenceError(c, "cannot read image %s", path.c_str());
            }
            clip->Draw(list(), num(c, argc, argv, 7), ImVec2(static_cast<float>(num(c, argc, argv, 1)), static_cast<float>(num(c, argc, argv, 2))),
                       ImVec2(static_cast<float>(num(c, argc, argv, 3)), static_cast<float>(num(c, argc, argv, 4))), fitNamed(text(c, argc, argv, 6, "cover")),
                       IM_COL32(255, 255, 255, static_cast<int>(ImGui::GetStyle().Alpha * 255.f + 0.5f)), static_cast<float>(num(c, argc, argv, 5)));
            return JS_UNDEFINED;
        }

        // interpolate(position, [inFrom, inTo], [outFrom, outTo], "linear" | "in" | "out" | "inOut" | "back")
        FN(jsInterpolate) {
            if (argc < 3) {
                return JS_ThrowTypeError(c, "interpolate(position, [from, to], [from, to], ease)");
            }
            return JS_NewFloat64(c, Interpolate(num(c, argc, argv, 0), range(c, argv[1]), range(c, argv[2]), easeNamed(text(c, argc, argv, 3))));
        }

        // spring(position, fps, from, to, start): a damped spring released at `start`
        FN(jsSpring) {
            return JS_NewFloat64(c, Spring(num(c, argc, argv, 0), static_cast<int>(num(c, argc, argv, 1, g.fps)), static_cast<float>(num(c, argc, argv, 2)), static_cast<float>(num(c, argc, argv, 3, 1.0)),
                                           static_cast<int>(num(c, argc, argv, 4))));
        }

        // springIn(position, fps, from, to, start, frames): the spring, eased so it is at `to` after `frames`
        FN(jsSpringIn) {
            return JS_NewFloat64(c, SpringIn(num(c, argc, argv, 0), static_cast<int>(num(c, argc, argv, 1, g.fps)), static_cast<float>(num(c, argc, argv, 2)), static_cast<float>(num(c, argc, argv, 3, 1.0)),
                                             static_cast<int>(num(c, argc, argv, 4)), static_cast<int>(num(c, argc, argv, 5, 30.0))));
        }

        // gradientRect(x, y, w, h, from, to, "v" | "h"): top to bottom (default) or left to right; square corners
        FN(jsGradientRect) {
            const ImVec2 a(static_cast<float>(num(c, argc, argv, 0)), static_cast<float>(num(c, argc, argv, 1)));
            const ImVec2 b(static_cast<float>(num(c, argc, argv, 0) + num(c, argc, argv, 2)), static_cast<float>(num(c, argc, argv, 1) + num(c, argc, argv, 3)));
            const ImU32 from = colour(c, argc, argv, 4), to = colour(c, argc, argv, 5);
            if (text(c, argc, argv, 6, "v") == "h") {
                list()->AddRectFilledMultiColor(a, b, from, to, to, from);
            } else {
                list()->AddRectFilledMultiColor(a, b, from, from, to, to);
            }
            return JS_UNDEFINED;
        }

        // shadow(x, y, w, h, rounding, blur, color): a soft shadow under a rounded rect, drawn as stacked rounded rects that fade outwards
        FN(jsShadow) {
            constexpr int steps = 14;
            const float x = static_cast<float>(num(c, argc, argv, 0)), y = static_cast<float>(num(c, argc, argv, 1)), w = static_cast<float>(num(c, argc, argv, 2)), h = static_cast<float>(num(c, argc, argv, 3));
            const float rounding = static_cast<float>(num(c, argc, argv, 4)), blur = static_cast<float>(num(c, argc, argv, 5, 20.0));
            const ImU32 base = colour(c, argc, argv, 6);
            const float peak = static_cast<float>(base >> IM_COL32_A_SHIFT) / 255.f;
            for (int i = steps - 1; i >= 0; --i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps - 1); // 0 = the rect, 1 = the far edge of the blur
                const float grow = blur * t;
                const float a = peak * (1.f - t) * (1.f - t) * 0.45f;
                list()->AddRectFilled(ImVec2(x - grow, y - grow), ImVec2(x + w + grow, y + h + grow), (base & ~IM_COL32_A_MASK) | (static_cast<uint32_t>(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f) << IM_COL32_A_SHIFT),
                                      rounding + grow);
            }
            return JS_UNDEFINED;
        }

        // clip(x, y, w, h, () => { ... }): what the callback draws is cut to the rectangle (an image panning inside a frame)
        FN(jsClip) {
            if (argc < 5 || !JS_IsFunction(c, argv[4])) {
                return JS_ThrowTypeError(c, "clip(x, y, w, h, () => { ... })");
            }
            ImDrawList* dl = list();
            dl->PushClipRect(ImVec2(static_cast<float>(num(c, argc, argv, 0)), static_cast<float>(num(c, argc, argv, 1))),
                             ImVec2(static_cast<float>(num(c, argc, argv, 0) + num(c, argc, argv, 2)), static_cast<float>(num(c, argc, argv, 1) + num(c, argc, argv, 3))), true);
            JSValue result = JS_Call(c, argv[4], JS_UNDEFINED, 0, nullptr);
            dl->PopClipRect();
            if (JS_IsException(result)) {
                fail(c);
            }
            JS_FreeValue(c, result);
            return JS_UNDEFINED;
        }

        // window(name, x, y, w, h, () => { label(...); button(...); progress(...) }): a real ImGui window, drawn in the theme
        FN(jsWindow) {
            if (argc < 6 || !JS_IsFunction(c, argv[5])) {
                return JS_ThrowTypeError(c, "window(name, x, y, w, h, () => { ... })");
            }
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(num(c, argc, argv, 1)), static_cast<float>(num(c, argc, argv, 2))));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(num(c, argc, argv, 3)), static_cast<float>(num(c, argc, argv, 4))));
            const std::string name = text(c, argc, argv, 0);
            ImGui::Begin(name.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            JSValue result = JS_Call(c, argv[5], JS_UNDEFINED, 0, nullptr);
            ImGui::End();
            if (JS_IsException(result)) {
                fail(c);
            }
            JS_FreeValue(c, result);
            return JS_UNDEFINED;
        }

        FN(jsLabel) {
            ImGui::TextUnformatted(text(c, argc, argv, 0).c_str());
            return JS_UNDEFINED;
        }

        FN(jsButton) {
            ImGui::Button(text(c, argc, argv, 0).c_str());
            return JS_UNDEFINED;
        }

        FN(jsCheckbox) {
            bool value = argc > 1 && JS_ToBool(c, argv[1]) > 0;
            ImGui::Checkbox(text(c, argc, argv, 0).c_str(), &value);
            return JS_UNDEFINED;
        }

        FN(jsProgress) {
            ImGui::ProgressBar(static_cast<float>(num(c, argc, argv, 0)), ImVec2(-1.f, 0.f));
            return JS_UNDEFINED;
        }

        FN(jsSeparator) {
            ImGui::Separator();
            return JS_UNDEFINED;
        }

        FN(jsSameLine) {
            ImGui::SameLine();
            return JS_UNDEFINED;
        }

        // Math.random: the same numbers for the same frame, whichever way it is reached
        FN(jsRandom) {
            g.rng ^= g.rng << 13;
            g.rng ^= g.rng >> 7;
            g.rng ^= g.rng << 17;
            return JS_NewFloat64(c, static_cast<double>(g.rng >> 11) / 9007199254740992.0);
        }

        FN(jsNow) { return JS_NewFloat64(c, 0.0); }

#undef FN

        void define(const char* name, JSCFunction* fn, int length) {
            JSValue global = JS_GetGlobalObject(g.ctx);
            JS_SetPropertyStr(g.ctx, global, name, JS_NewCFunction(g.ctx, fn, name, length));
            JS_FreeValue(g.ctx, global);
        }

        double number(JSValueConst obj, const char* key, double fallback) {
            JSValue v = JS_GetPropertyStr(g.ctx, obj, key);
            double d = fallback;
            if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
                JS_ToFloat64(g.ctx, &d, v);
            }
            JS_FreeValue(g.ctx, v);
            return d;
        }

        std::string string(JSValueConst obj, const char* key, const char* fallback = "") {
            JSValue v = JS_GetPropertyStr(g.ctx, obj, key);
            std::string out = JS_IsUndefined(v) ? fallback : str(g.ctx, v);
            JS_FreeValue(g.ctx, v);
            return out;
        }

        JSValue frameObject(const Frame& f) {
            JSValue o = JS_NewObject(g.ctx);
            JS_SetPropertyStr(g.ctx, o, "index", JS_NewInt32(g.ctx, f.index));
            JS_SetPropertyStr(g.ctx, o, "frames", JS_NewInt32(g.ctx, f.frames));
            JS_SetPropertyStr(g.ctx, o, "fps", JS_NewInt32(g.ctx, f.fps));
            JS_SetPropertyStr(g.ctx, o, "width", JS_NewInt32(g.ctx, f.width));
            JS_SetPropertyStr(g.ctx, o, "height", JS_NewInt32(g.ctx, f.height));
            JS_SetPropertyStr(g.ctx, o, "scene", JS_NewInt32(g.ctx, f.scene));
            JS_SetPropertyStr(g.ctx, o, "time", JS_NewFloat64(g.ctx, f.time()));
            JS_SetPropertyStr(g.ctx, o, "position", JS_NewFloat64(g.ctx, f.position()));
            JS_SetPropertyStr(g.ctx, o, "progress", JS_NewFloat64(g.ctx, f.progress()));
            return o;
        }

        void callDraw(JSValueConst fn, const Frame& f) {
            g.back = false;
            g.rng = static_cast<uint64_t>(f.index) * 2654435761ull + static_cast<uint64_t>(f.scene) * 40503ull + 88172645463325252ull;
            JSValue frame = frameObject(f);
            JSValue result = JS_Call(g.ctx, fn, JS_UNDEFINED, 1, &frame);
            JS_FreeValue(g.ctx, frame);
            if (JS_IsException(result)) {
                fail(g.ctx);
            }
            JS_FreeValue(g.ctx, result);
        }

        struct Post {
            std::string effect;
            std::map<std::string, double> p;
            double get(const char* k, double fallback) const {
                auto it = p.find(k);
                return it == p.end() ? fallback : it->second;
            }
        };

        void applyPost(const std::vector<Post>& posts, const Frame& f, Image& image) {
            for (const Post& e : posts) {
                if (e.effect == "glow") Glow(image, static_cast<float>(e.get("threshold", 0.7)), static_cast<float>(e.get("radius", 0.02)), static_cast<float>(e.get("intensity", 0.6)));
                else if (e.effect == "grade") Grade(image, static_cast<float>(e.get("brightness", 0)), static_cast<float>(e.get("contrast", 1)), static_cast<float>(e.get("saturation", 1)), static_cast<float>(e.get("gamma", 1)));
                else if (e.effect == "vignette") Vignette(image, static_cast<float>(e.get("strength", 0.5)), static_cast<float>(e.get("radius", 0.6)));
                else if (e.effect == "grain") Grain(image, static_cast<float>(e.get("amount", 0.05)), static_cast<uint32_t>(f.index)); // moves with the frame
                else if (e.effect == "blur") Blur(image, static_cast<float>(e.get("radius", 0.01)));
                else if (e.effect == "chromatic") Chromatic(image, static_cast<float>(e.get("shift", 0.004)));
                else if (e.effect == "pixelate") Pixelate(image, static_cast<float>(e.get("cell", 0.01)));
            }
        }

        const char* kApi =
            "imshell-video scene.js <command>   commands: timeline, frame T, strip RANGE, inspect, verify, render -o out.mp4, preview, bench (imshell-video scene.js help)\n"
            "\n"
            "a scene calls video({...}) once:\n"
            "  video({ width: 1920, height: 1080, fps: 30, frames: 120,\n"
            "          draw(f) { ... },                       // f: index, frames, fps, width, height, time (s), position (the frame number the motion helpers take; inside a scene it counts from the scene's own start, like index), progress (0..1)\n"
            "          scenes: [{ name, frames, overlap, crossfade, draw(f) }],   // instead of frames/draw: scenes crossfade over `overlap` frames (crossfade: false fades both, for scenes with no background); f.index and f.position count from the scene's start\n"
            "          audio: [{ path, start, offset, duration, volume, fadeIn, fadeOut }],   // silentAudio: true writes a silent track when there is none (App Store previews need one)\n"
            "          post: [{ effect: 'glow', threshold, radius, intensity }, 'grade' brightness contrast saturation gamma, 'vignette' strength radius,\n"
            "                 'grain' amount, 'blur' radius, 'chromatic' shift, 'pixelate' cell],   // sizes are fractions of the height\n"
            "          font: 'path/to/font.ttf' })             // the default is JetBrains Mono\n"
            "\n"
            "drawing (colours: rgba(r, g, b, a 0..1) or '#rrggbb' / '#rrggbbaa'; the scene's fade scales them):\n"
            "  layer('back'|'front')  rect(x, y, w, h, color, rounding)  strokeRect(x, y, w, h, color, thickness, rounding)  circle(x, y, r, color)\n"
            "  ring(x, y, r, color, thickness)  line(x1, y1, x2, y2, color, thickness)  text(s, x, y, size, color)  measure(s, size) -> [w, h]\n"
            "  gradientRect(x, y, w, h, fromColor, toColor, 'v'|'h')  shadow(x, y, w, h, rounding, blur, color)  clip(x, y, w, h, () => { ... })   // clip cuts what the callback draws, e.g. an image panning in a frame\n"
            "  image(path, x, y, w, h, rounding, 'cover'|'contain'|'stretch', videoSeconds)   png/jpeg or a frame of a video file (needs ffmpeg)\n"
            "widgets, drawn in the real ImGui theme: window(name, x, y, w, h, () => { label(s); button(s); checkbox(s, on); progress(0..1); separator(); sameLine(); })\n"
            "motion: interpolate(f.position, [inFrom, inTo], [outFrom, outTo], 'linear'|'in'|'out'|'inOut'|'back')\n"
            "        spring(f.position, f.fps, from, to, startFrame)  springIn(f.position, f.fps, from, to, startFrame, frames)\n"
            "args: imshell-video scene.js render --set name=Ada --set plan=pro -o ada.mp4 gives the script args.name and args.plan (strings), so one script makes many videos\n"
            "determinism: a frame must depend only on its number. Math.random is reseeded from the frame and Date.now is 0; do not keep state in globals between frames (verify catches it).\n"
            "a thrown error prints its message and stack and exits 1.\n";
    } // namespace

    int RunScript(int argc, char** argv) {
        if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
            std::fprintf(stderr, "usage: imshell-video scene.js <command> ...   (imshell-video api: what a scene can do)\n");
            return argc < 2 ? 1 : 0;
        }
        if (std::strcmp(argv[1], "api") == 0) {
            std::fputs(kApi, stdout);
            return 0;
        }
        g.script = argv[1];
        std::ifstream in(g.script, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot read %s\n", g.script.c_str());
            return 1;
        }
        std::stringstream source;
        source << in.rdbuf();

        JSRuntime* rt = JS_NewRuntime();
        g.ctx = JS_NewContext(rt);
        JSValue global = JS_GetGlobalObject(g.ctx);
        JSValue math = JS_GetPropertyStr(g.ctx, global, "Math");
        JS_SetPropertyStr(g.ctx, math, "random", JS_NewCFunction(g.ctx, jsRandom, "random", 0));
        JSValue date = JS_GetPropertyStr(g.ctx, global, "Date");
        JS_SetPropertyStr(g.ctx, date, "now", JS_NewCFunction(g.ctx, jsNow, "now", 0));
        JS_FreeValue(g.ctx, date);
        JS_FreeValue(g.ctx, math);
        JS_FreeValue(g.ctx, global);
        define("rgba", jsRgba, 4);
        define("layer", jsLayer, 1);
        define("rect", jsRect, 6);
        define("strokeRect", jsStrokeRect, 7);
        define("gradientRect", jsGradientRect, 7);
        define("shadow", jsShadow, 7);
        define("clip", jsClip, 5);
        define("circle", jsCircle, 4);
        define("ring", jsRing, 5);
        define("line", jsLine, 6);
        define("text", jsText, 5);
        define("measure", jsMeasure, 2);
        define("image", jsImage, 8);
        define("interpolate", jsInterpolate, 4);
        define("spring", jsSpring, 5);
        define("springIn", jsSpringIn, 6);
        define("window", jsWindow, 6);
        define("label", jsLabel, 1);
        define("button", jsButton, 1);
        define("checkbox", jsCheckbox, 2);
        define("progress", jsProgress, 1);
        define("separator", jsSeparator, 0);
        define("sameLine", jsSameLine, 0);
        // video(config): remembers the config; the composition is built from it below
        JS_SetPropertyStr(
            g.ctx, JS_GetGlobalObject(g.ctx), "video",
            JS_NewCFunction(
                g.ctx,
                [](JSContext* c, JSValueConst, int argc, JSValueConst* argv) -> JSValue {
                    if (argc < 1 || !JS_IsObject(argv[0])) {
                        return JS_ThrowTypeError(c, "video({ width, height, fps, frames, draw(f) { ... } })");
                    }
                    g.config = JS_DupValue(c, argv[0]);
                    return JS_UNDEFINED;
                },
                "video", 1));

        // --set key=value (any number of them) becomes args.key, a string: one script, many videos
        {
            JSValue args = JS_NewObject(g.ctx);
            for (int i = 2; i + 1 < argc; ++i) {
                if (std::strcmp(argv[i], "--set") == 0) {
                    const std::string pair = argv[i + 1];
                    const size_t eq = pair.find('=');
                    if (eq == std::string::npos || eq == 0) {
                        std::fprintf(stderr, "--set wants key=value, not '%s'\n", pair.c_str());
                        return 1;
                    }
                    JS_SetPropertyStr(g.ctx, args, pair.substr(0, eq).c_str(), JS_NewString(g.ctx, pair.c_str() + eq + 1));
                }
            }
            JSValue global2 = JS_GetGlobalObject(g.ctx);
            JS_SetPropertyStr(g.ctx, global2, "args", args);
            JS_FreeValue(g.ctx, global2);
        }

        JSValue ran = JS_Eval(g.ctx, source.str().c_str(), source.str().size(), g.script.c_str(), JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(ran)) {
            fail(g.ctx);
        }
        JS_FreeValue(g.ctx, ran);
        if (JS_IsUndefined(g.config)) {
            std::fprintf(stderr, "error: %s never called video({ ... }); see `imshell-video api`\n", g.script.c_str());
            return 1;
        }

        Composition comp;
        comp.width = static_cast<int>(number(g.config, "width", 1920));
        comp.height = static_cast<int>(number(g.config, "height", 1080));
        comp.fps = static_cast<int>(number(g.config, "fps", 30));
        comp.frames = static_cast<int>(number(g.config, "frames", 90));
        comp.silentAudio = number(g.config, "silentAudio", 0) != 0;
        g.fps = comp.fps;

        JSValue scenes = JS_GetPropertyStr(g.ctx, g.config, "scenes");
        JSValue draw = JS_GetPropertyStr(g.ctx, g.config, "draw");
        if (JS_IsArray(scenes)) {
            std::vector<Scene> list;
            for (uint32_t i = 0;; ++i) {
                JSValue s = JS_GetPropertyUint32(g.ctx, scenes, i);
                if (JS_IsUndefined(s)) {
                    break;
                }
                JSValue sceneDraw = JS_GetPropertyStr(g.ctx, s, "draw");
                if (!JS_IsFunction(g.ctx, sceneDraw)) {
                    std::fprintf(stderr, "error: scenes[%u] has no draw(f) function\n", i);
                    return 1;
                }
                Scene scene;
                scene.name = string(s, "name");
                scene.frames = static_cast<int>(number(s, "frames", 60));
                scene.overlap = static_cast<int>(number(s, "overlap", 0));
                scene.crossfade = number(s, "crossfade", 1) != 0; // scripts crossfade by default; crossfade: false fades both scenes
                scene.draw = [sceneDraw](const Frame& f) { callDraw(sceneDraw, f); };
                list.push_back(std::move(scene));
                JS_FreeValue(g.ctx, s);
            }
            comp = MakeComposition(comp.width, comp.height, comp.fps, std::move(list));
        } else if (JS_IsFunction(g.ctx, draw)) {
            comp.draw = [draw](const Frame& f) { callDraw(draw, f); };
        } else {
            std::fprintf(stderr, "error: video({ ... }) needs a draw(f) function or scenes: [...]\n");
            return 1;
        }

        const std::string fontFile = string(g.config, "font");
        comp.setup = [fontFile] {
            ImGuiIO& io = ImGui::GetIO();
            if (!fontFile.empty()) {
                g.font = io.Fonts->AddFontFromFileTTF(fontFile.c_str(), 0.f);
            } else if (getEmbeddedFontCount() > 0) {
                ImFontConfig cfg;
                cfg.FontDataOwnedByAtlas = false;
                g.font = io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(getEmbeddedFonts()[0].data), static_cast<int>(getEmbeddedFonts()[0].size), 0.f, &cfg);
            }
            if (!g.font) {
                std::fprintf(stderr, "error: font could not be loaded (%s)\n", fontFile.empty() ? "embedded" : fontFile.c_str());
                std::exit(1);
            }
        };

        JSValue audio = JS_GetPropertyStr(g.ctx, g.config, "audio");
        for (uint32_t i = 0; JS_IsArray(audio); ++i) {
            JSValue a = JS_GetPropertyUint32(g.ctx, audio, i);
            if (JS_IsUndefined(a)) {
                break;
            }
            AudioClip clip;
            clip.path = string(a, "path");
            clip.scene = string(a, "scene");
            clip.start = number(a, "start", 0.0);
            clip.offset = number(a, "offset", 0.0);
            clip.duration = number(a, "duration", -1.0);
            clip.volume = static_cast<float>(number(a, "volume", 1.0));
            clip.fadeIn = number(a, "fadeIn", 0.0);
            clip.fadeOut = number(a, "fadeOut", 0.0);
            comp.audio.push_back(clip);
            JS_FreeValue(g.ctx, a);
        }

        JSValue post = JS_GetPropertyStr(g.ctx, g.config, "post");
        std::vector<Post> posts;
        for (uint32_t i = 0; JS_IsArray(post); ++i) {
            JSValue e = JS_GetPropertyUint32(g.ctx, post, i);
            if (JS_IsUndefined(e)) {
                break;
            }
            Post p;
            p.effect = string(e, "effect");
            for (const char* key : {"threshold", "radius", "intensity", "brightness", "contrast", "saturation", "gamma", "strength", "amount", "shift", "cell"}) {
                JSValue v = JS_GetPropertyStr(g.ctx, e, key);
                if (!JS_IsUndefined(v)) {
                    p.p[key] = number(e, key, 0.0);
                }
                JS_FreeValue(g.ctx, v);
            }
            posts.push_back(std::move(p));
            JS_FreeValue(g.ctx, e);
        }
        if (!posts.empty()) {
            comp.post = [posts](const Frame& f, Image& image) { applyPost(posts, f, image); };
        }

        // Cli expects argv[0] to be the program: the script stands in for it, so `scene.js render` reads like `video render`
        return Cli(comp, argc - 1, argv + 1, "imshell-video scene.js");
    }
} // namespace imshell::video
