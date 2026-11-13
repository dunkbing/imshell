// imshell::ui components (include/imshell/components.hpp): plain imgui items and draw-list
// shapes in the theme's colours; nothing here keeps state across frames except toasts
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imshell/components.hpp"

#include "imgui_internal.h"
#include "imshell/button.hpp"
#include "imshell/frames.hpp"
#include "imshell/themes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace imshell::ui {
    namespace {
        const Theme& colors() {
            return theme();
        }

        ImVec4 toneColor(Tone tone) {
            const auto& c = colors();
            switch (tone) {
            case Tone::Info:
                return c.primary;
            case Tone::Success:
                return c.success;
            case Tone::Warning:
                return c.warning;
            case Tone::Danger:
                return c.destructive;
            default:
                return c.mutedForeground;
            }
        }

        ImU32 withAlpha(ImVec4 c, float a) {
            c.w = a;
            return ImGui::GetColorU32(c);
        }

        ImU32 u32(const ImVec4& c) {
            return ImGui::GetColorU32(c);
        }

        // std::string as an imgui buffer: imgui asks for more room through the callback
        int resizeString(ImGuiInputTextCallbackData* data) {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* s = static_cast<std::string*>(data->UserData);
                s->resize(data->BufTextLen);
                data->Buf = s->data();
            }
            return 0;
        }
    } // namespace

    bool TextField(const char* label, std::string& value, const TextFieldOptions& o) {
        ImGui::PushID(label);
        const auto& c = colors();
        if (label && *label && label[0] != '#')
            ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
        ImGui::SetNextItemWidth(o.width > 0 ? o.width : -FLT_MIN);
        if (o.error)
            ImGui::PushStyleColor(ImGuiCol_Border, c.destructive);
        ImGuiInputTextFlags flags = o.flags | ImGuiInputTextFlags_CallbackResize;
        if (o.password)
            flags |= ImGuiInputTextFlags_Password;
        const bool edited =
            ImGui::InputTextWithHint("##field", o.placeholder ? o.placeholder : "", value.data(),
                                     value.capacity() + 1, flags, resizeString, &value);
        if (o.error)
            ImGui::PopStyleColor();
        if (o.error || o.hint) {
            ImGui::PushStyleColor(ImGuiCol_Text, o.error ? c.destructive : c.mutedForeground);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(o.error ? o.error : o.hint);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
        return edited;
    }

    void BeginCard(const char* id, const char* title) {
        const auto& c = colors();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.card);
        ImGui::PushStyleColor(ImGuiCol_Border, c.border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Radius::Lg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(Theme::Spacing::L, Theme::Spacing::L));
        ImGui::BeginChild(id, ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        if (title) {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::Spacing();
        }
    }

    void EndCard() {
        ImGui::EndChild();
    }

    void Badge(const char* text, Tone tone) {
        const ImVec4 tint = toneColor(tone);
        const ImVec2 pad(Theme::Spacing::M, Theme::Spacing::XS);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 size(textSize.x + pad.x * 2, textSize.y + pad.y * 2);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, at + size, withAlpha(tint, 0.18f), size.y * 0.5f);
        dl->AddText(at + pad, u32(tone == Tone::Neutral ? colors().foreground : tint), text);
    }

    void Callout(const char* title, const char* body, Tone tone) {
        const ImVec4 tint = toneColor(tone);
        const float pad = Theme::Spacing::M + Theme::Spacing::S;
        const float stripe = 3.0f;
        const float width = ImGui::GetContentRegionAvail().x;
        const float wrap = width - pad * 2 - stripe;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float titleH = title ? ImGui::GetTextLineHeight() + Theme::Spacing::S : 0;
        const ImVec2 bodySize = ImGui::CalcTextSize(body, nullptr, false, wrap);
        const ImVec2 size(width, pad * 2 + titleH + bodySize.y);
        ImGui::Dummy(size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = Radius::Md;
        dl->AddRectFilled(at, at + size, withAlpha(tint, 0.10f), r);
        dl->AddRectFilled(at, ImVec2(at.x + stripe, at.y + size.y), u32(tint), r,
                          ImDrawFlags_RoundCornersLeft);
        ImVec2 text(at.x + stripe + pad, at.y + pad);
        if (title) {
            dl->AddText(text, u32(tint), title);
            text.y += titleH;
        }
        dl->AddText(nullptr, 0, text, u32(colors().foreground), body, nullptr, wrap);
    }

    void Progress(float fraction, const char* label, float width) {
        const auto& c = colors();
        if (label) {
            ImGui::TextUnformatted(label);
            if (fraction >= 0) {
                char pct[8];
                std::snprintf(pct, sizeof pct, "%d%%",
                              static_cast<int>(std::round(fraction * 100)));
                ImGui::SameLine(ImGui::GetCursorPosX() +
                                (width > 0 ? width : ImGui::GetContentRegionAvail().x) -
                                ImGui::CalcTextSize(pct).x);
                ImGui::TextDisabled("%s", pct);
            }
        }
        const float w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
        const float h = 6.0f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, at + ImVec2(w, h), u32(c.muted), h * 0.5f);
        if (fraction >= 0) {
            const float f = std::clamp(fraction, 0.0f, 1.0f);
            if (f > 0)
                dl->AddRectFilled(at, at + ImVec2(std::max(h, w * f), h), u32(c.primary), h * 0.5f);
            return;
        }
        // indeterminate: a third of the track sweeping across
        requestFrames();
        const float t = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.8f, 1.0f);
        const float seg = w / 3;
        const float x0 = std::max(at.x, at.x - seg + t * (w + seg));
        const float x1 = std::min(at.x + w, at.x + t * (w + seg));
        if (x1 > x0)
            dl->AddRectFilled(ImVec2(x0, at.y), ImVec2(x1, at.y + h), u32(c.primary), h * 0.5f);
    }

    void Skeleton(ImVec2 size) {
        if (size.x <= 0)
            size.x = ImGui::GetContentRegionAvail().x;
        requestFrames();
        const auto& c = colors();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        // a slow pulse between two surface shades
        const float t = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
        ImVec4 col;
        for (int i = 0; i < 4; ++i)
            (&col.x)[i] = (&c.muted.x)[i] + ((&c.accent.x)[i] - (&c.muted.x)[i]) * t;
        ImGui::GetWindowDrawList()->AddRectFilled(at, at + size, u32(col),
                                                  Radius::Md);
    }

    void Avatar(const char* name, float diameter) {
        const auto& c = colors();
        const auto& palette = c.chart;
        unsigned hash = 2166136261u; // fnv-1a: a name keeps its colour
        for (const char* p = name; *p; ++p)
            hash = (hash ^ static_cast<unsigned char>(*p)) * 16777619u;
        char initials[8] = {};
        int n = 0;
        bool start = true;
        for (const char* p = name; *p && n < 2; ++p) {
            if (*p == ' ') {
                start = true;
            } else if (start) {
                // shortcut: ascii initials only; a multi-byte first letter shows its first byte
                initials[n++] = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
                start = false;
            }
        }
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(diameter, diameter));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", name);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 centre = at + ImVec2(diameter, diameter) * 0.5f;
        dl->AddCircleFilled(centre, diameter * 0.5f, u32(palette[hash % std::size(palette)]));
        const float size = diameter * 0.4f;
        ImFont* font = ImGui::GetFont();
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, initials);
        dl->AddText(font, size, centre - ts * 0.5f, u32(c.background), initials);
    }

    void Metric(const char* id, const char* label, const char* value, const char* delta) {
        BeginCard(id);
        ImGui::TextDisabled("%s", label);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
        ImGui::TextUnformatted(value);
        ImGui::PopFont();
        if (delta && *delta) {
            const bool down = delta[0] == '-';
            ImGui::PushStyleColor(ImGuiCol_Text, down ? colors().destructive : colors().success);
            ImGui::TextUnformatted(delta);
            ImGui::PopStyleColor();
        }
        EndCard();
    }

    bool EmptyState(const char* title, const char* body, const char* action) {
        const float width = ImGui::GetContentRegionAvail().x;
        const float wrap = std::min(width, 360.0f);
        const auto centred = [&](float textWidth) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 std::max(0.0f, (width - textWidth) * 0.5f));
        };
        ImGui::Dummy(ImVec2(0, Theme::Spacing::L));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        centred(ImGui::CalcTextSize(title).x);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        const ImVec2 bodySize = ImGui::CalcTextSize(body, nullptr, false, wrap);
        centred(bodySize.x);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::PushStyleColor(ImGuiCol_Text, colors().mutedForeground);
        ImGui::TextUnformatted(body);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        bool pressed = false;
        if (action) {
            ImGui::Dummy(ImVec2(0, Theme::Spacing::S));
            const ImGuiStyle& s = ImGui::GetStyle();
            centred(ImGui::CalcTextSize(action).x + s.FramePadding.x * 2);
            pressed = Button(action, ButtonVariant::Primary);
        }
        ImGui::Dummy(ImVec2(0, Theme::Spacing::L));
        return pressed;
    }

    bool Chips(const char* id, std::span<const char* const> labels, std::span<bool> selected) {
        const auto& c = colors();
        bool changed = false;
        ImGui::PushID(id);
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const ImGuiStyle& s = ImGui::GetStyle();
        for (size_t i = 0; i < labels.size() && i < selected.size(); ++i) {
            const float w = ImGui::CalcTextSize(labels[i]).x + s.FramePadding.x * 2;
            if (i > 0) {
                ImGui::SameLine(0, Theme::Spacing::S);
                if (ImGui::GetCursorScreenPos().x + w > right)
                    ImGui::NewLine();
            }
            const bool on = selected[i];
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, on ? withAlpha(c.primary, 0.20f) : u32(c.muted));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  on ? withAlpha(c.primary, 0.30f) : u32(c.accent));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, withAlpha(c.primary, 0.40f));
            ImGui::PushStyleColor(ImGuiCol_Border, on ? c.primary : c.border);
            ImGui::PushStyleColor(ImGuiCol_Text, on ? c.primary : c.foreground);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button(labels[i])) {
                selected[i] = !selected[i];
                changed = true;
            }
            ImGui::PopID();
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar();
        }
        ImGui::PopID();
        return changed;
    }

    namespace {
        struct ToastItem {
            std::string text;
            Tone tone;
            double until;
            int id;
        };
        std::vector<ToastItem>& toasts() {
            static std::vector<ToastItem> list;
            return list;
        }
    } // namespace

    void Toast(const char* text, Tone tone, float seconds) {
        static int next = 0;
        toasts().push_back({text, tone, ImGui::GetTime() + seconds, next++});
        requestFrames();
    }

    void RenderToasts() {
        auto& list = toasts();
        const double now = ImGui::GetTime();
        std::erase_if(list, [now](const ToastItem& t) { return t.until <= now; });
        if (list.empty())
            return;
        requestFrames(); // they fade and expire on their own
        const auto& c = colors();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float margin = Theme::Spacing::L;
        const float width = std::min(320.0f, vp->WorkSize.x - margin * 2);
        float y = vp->WorkPos.y + vp->WorkSize.y - margin;
        ImDrawList* dl = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
        const float pad = Theme::Spacing::M + Theme::Spacing::S;
        for (auto it = list.rbegin(); it != list.rend(); ++it) {
            const float left = static_cast<float>(it->until - now);
            const float alpha = std::clamp(left / 0.3f, 0.0f, 1.0f); // fade out over the last 0.3 s
            const ImVec2 ts =
                ImGui::CalcTextSize(it->text.c_str(), nullptr, false, width - pad * 2 - 12);
            const ImVec2 size(width, ts.y + pad * 2);
            const ImVec2 at(vp->WorkPos.x + vp->WorkSize.x - margin - width, y - size.y);
            dl->AddRectFilled(at, at + size, withAlpha(c.popover, alpha),
                              Radius::Lg);
            dl->AddRect(at, at + size, withAlpha(c.border, alpha), Radius::Lg);
            dl->AddCircleFilled(
                ImVec2(at.x + pad + 3, at.y + pad + ImGui::GetTextLineHeight() * 0.5f), 4,
                withAlpha(toneColor(it->tone), alpha));
            dl->AddText(nullptr, 0, ImVec2(at.x + pad + 12, at.y + pad), withAlpha(c.foreground, alpha),
                        it->text.c_str(), nullptr, width - pad * 2 - 12);
            y = at.y - Theme::Spacing::S;
        }
    }
} // namespace imshell::ui
