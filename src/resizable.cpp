#include "imshell/resizable.hpp"

#include "imshell/frames.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace imshell {
    namespace {
        struct Panel {
            PanelOptions options; // as given last frame: this frame's layout is made from it
            float size = -1;      // pixels; -1: not sized yet (shares what is left)
        };

        struct Group {
            ResizableOptions options;
            std::vector<Panel> panels;
            std::vector<float> layout; // this frame's size of each panel; 0 when hidden
            int next = 0;              // the panel ResizablePanel begins next
            bool childOpen = false;
            bool changed = false; // a panel's options differ from last frame: the layout is late
            // a drag: the panels on each side of the handle, and their sizes and the mouse
            // where it began
            int dragA = -1, dragB = -1;
            float dragFrom = 0, startA = 0, startB = 0;
            bool released = false;
        };

        std::unordered_map<ImGuiID, Group> groups;
        std::vector<Group*> stack;

        float along(const ImVec2& v, Axis axis) {
            return axis == Axis::Horizontal ? v.x : v.y;
        }

        float upper(const PanelOptions& o) {
            return o.max > 0 ? std::max(o.max, o.min) : 1e9f;
        }

        // every visible panel's size for `room` pixels: fixed panels keep theirs, the others
        // share the rest in proportion to their sizes, then minimums and maximums are kept
        void lay(Group& g, float room) {
            const size_t n = g.panels.size();
            g.layout.assign(n, 0);
            float fixedSum = 0, sizedSum = 0;
            int unsized = 0;
            for (size_t i = 0; i < n; ++i) {
                const Panel& p = g.panels[i];
                if (!p.options.visible) {
                    continue;
                }
                if (p.options.fixed && p.size >= 0) {
                    g.layout[i] = std::clamp(p.size, p.options.min, upper(p.options));
                    fixedSum += g.layout[i];
                } else if (p.size >= 0) {
                    sizedSum += p.size;
                } else {
                    ++unsized;
                }
            }
            const float rest = std::max(0.0f, room - fixedSum);
            // unsized panels take what the sized ones leave, evenly; with none, the sized
            // ones scale to fill the room
            const float leftover = rest - sizedSum;
            const float scale = unsized > 0 ? (leftover >= 0 ? 1 : rest / std::max(1.0f, sizedSum))
                                            : rest / std::max(1.0f, sizedSum);
            const float share = unsized > 0 ? std::max(0.0f, leftover) / static_cast<float>(unsized) : 0;
            for (size_t i = 0; i < n; ++i) {
                const Panel& p = g.panels[i];
                if (p.options.visible && !(p.options.fixed && p.size >= 0)) {
                    g.layout[i] = p.size >= 0 ? p.size * scale : share;
                }
            }
            // limits: a panel pushed past one gives or takes the difference from the panels
            // that are not fixed and still have room, a few rounds
            for (int round = 0; round < 4; ++round) {
                float excess = 0; // > 0: pixels to give back
                std::vector<size_t> free;
                for (size_t i = 0; i < n; ++i) {
                    const PanelOptions& o = g.panels[i].options;
                    if (!o.visible) {
                        continue;
                    }
                    const float limited = std::clamp(g.layout[i], o.min, upper(o));
                    excess += g.layout[i] - limited;
                    g.layout[i] = limited;
                    if (!o.fixed && limited > o.min && limited < upper(o)) {
                        free.push_back(i);
                    }
                }
                if (std::abs(excess) < 0.5f || free.empty()) {
                    break;
                }
                for (const size_t i : free) {
                    g.layout[i] += excess / static_cast<float>(free.size());
                }
            }
            // still too wide (the minimums do not fit): fixed panels give way down to their
            // minimum, then the others shrink in proportion, so nothing ends past the edge
            float total = 0;
            for (size_t i = 0; i < n; ++i) {
                total += g.panels[i].options.visible ? g.layout[i] : 0;
            }
            for (size_t i = 0; i < n && total > room; ++i) {
                const PanelOptions& o = g.panels[i].options;
                if (o.visible && o.fixed) {
                    const float give = std::min(total - room, std::max(0.0f, g.layout[i] - o.min));
                    g.layout[i] -= give;
                    total -= give;
                }
            }
            if (total > room && total > 0) {
                // the flexible panels go below their minimums first; fixed ones only when
                // they alone are too wide
                float fixedTotal = 0;
                for (size_t i = 0; i < n; ++i) {
                    const PanelOptions& o = g.panels[i].options;
                    fixedTotal += o.visible && o.fixed ? g.layout[i] : 0;
                }
                const float flexible = total - fixedTotal;
                const bool fixedFit = fixedTotal < room && flexible > 0;
                for (size_t i = 0; i < n; ++i) {
                    const bool fixed = g.panels[i].options.fixed;
                    if (!fixedFit) {
                        g.layout[i] *= room / total;
                    } else if (!fixed) {
                        g.layout[i] *= (room - fixedTotal) / flexible;
                    }
                }
            }
            // whole pixels; the last visible panel ends at the edge
            float used = 0;
            int last = -1;
            for (size_t i = 0; i < n; ++i) {
                if (g.panels[i].options.visible) {
                    g.layout[i] = std::floor(g.layout[i]);
                    used += g.layout[i];
                    last = static_cast<int>(i);
                }
            }
            if (last >= 0 && used < room) {
                g.layout[last] += room - used;
            }
        }

        // the line on a handle: one screen pixel, two while lit, on the pixel grid
        void handleLine(ImDrawList* draw, float at, ImVec2 from, ImVec2 to, Axis axis, ImU32 color, bool lit) {
            const float scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
            const float thick = (lit ? 2.0f : 1.0f) / scale;
            const float snapped = std::floor(at * scale) / scale;
            if (axis == Axis::Horizontal) {
                draw->AddRectFilled(ImVec2(snapped, from.y), ImVec2(snapped + thick, to.y), color);
            } else {
                draw->AddRectFilled(ImVec2(from.x, snapped), ImVec2(to.x, snapped + thick), color);
            }
        }
    } // namespace

    bool BeginResizable(const char* id, const ResizableOptions& options) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        const bool open = ImGui::BeginChild(id, options.size, ImGuiChildFlags_None,
                                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        if (!open) {
            ImGui::EndChild();
            return false;
        }
        Group& g = groups[ImGui::GetCurrentWindow()->ID];
        g.options = options;
        g.next = 0;
        g.childOpen = false;
        g.changed = false;
        g.released = false;
        // the handles have 1px between the panels; the rest is shared
        int visible = 0;
        for (const Panel& p : g.panels) {
            visible += p.options.visible;
        }
        const float room = along(ImGui::GetContentRegionAvail(), options.axis) - static_cast<float>(std::max(0, visible - 1));
        lay(g, std::max(0.0f, room));
        stack.push_back(&g);
        return true;
    }

    bool ResizablePanel(const PanelOptions& options) {
        IM_ASSERT(!stack.empty() && "ResizablePanel outside BeginResizable");
        Group& g = *stack.back();
        if (g.childOpen) {
            ImGui::EndChild();
            g.childOpen = false;
        }
        const size_t i = static_cast<size_t>(g.next++);
        if (i >= g.panels.size()) { // new: laid out from next frame
            g.panels.push_back({options, options.size && *options.size > 0 ? *options.size : -1});
            g.layout.push_back(0);
            g.changed = true;
        }
        Panel& p = g.panels[i];
        const PanelOptions& was = p.options;
        if (was.visible != options.visible || was.fixed != options.fixed || was.min != options.min ||
            was.max != options.max) {
            g.changed = true;
        }
        if (options.size && *options.size > 0 && p.size >= 0 && std::abs(*options.size - p.size) >= 1) {
            p.size = *options.size; // the caller changed it: taken as asked
            g.changed = true;
        }
        p.options = options;
        if (!options.visible) {
            return false;
        }
        // where it starts: after the visible panels before it and a pixel for each handle
        float at = 0;
        for (size_t k = 0; k < i; ++k) {
            if (g.panels[k].options.visible) {
                at += g.layout[k] + 1;
            }
        }
        float size = i < g.layout.size() ? g.layout[i] : 0;
        if (size <= 0) { // new this frame: what is left, until the next frame lays it out
            size = std::max(1.0f, along(ImGui::GetWindowSize(), g.options.axis) - at);
        }
        if (g.options.axis == Axis::Horizontal) {
            ImGui::SetCursorPos(ImVec2(at, 0));
        } else {
            ImGui::SetCursorPos(ImVec2(0, at));
        }
        const ImVec2 extent = g.options.axis == Axis::Horizontal ? ImVec2(size, ImGui::GetWindowSize().y)
                                                                 : ImVec2(ImGui::GetWindowSize().x, size);
        ImGui::BeginChild(ImGui::GetCurrentWindow()->GetID(static_cast<int>(i)), extent, options.childFlags,
                          options.windowFlags);
        g.childOpen = true;
        return true;
    }

    bool EndResizable() {
        IM_ASSERT(!stack.empty() && "EndResizable without BeginResizable");
        Group& g = *stack.back();
        stack.pop_back();
        if (g.childOpen) {
            ImGui::EndChild();
            g.childOpen = false;
        }
        // panels not submitted this frame are gone
        if (static_cast<size_t>(g.next) < g.panels.size()) {
            g.panels.resize(static_cast<size_t>(g.next));
            g.layout.resize(static_cast<size_t>(g.next));
            g.changed = true;
        }
        const Axis axis = g.options.axis;
        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 extent = ImGui::GetWindowSize();
        const ImU32 line = g.options.line ? g.options.line : ImGui::GetColorU32(ImGuiCol_Separator);
        const ImU32 lit = g.options.lit ? g.options.lit : ImGui::GetColorU32(ImGuiCol_SeparatorActive);
        // a handle after each visible panel that has a visible panel after it: a small window
        // of its own over the boundary, so it takes the mouse before the panels under it
        float at = 0;
        int before = -1;
        for (size_t i = 0; i < g.panels.size(); ++i) {
            if (!g.panels[i].options.visible) {
                continue;
            }
            if (before >= 0) {
                const float boundary = at - 1; // the pixel between the two
                const float half = g.options.hitSize * 0.5f;
                const ImVec2 bandAt = axis == Axis::Horizontal ? ImVec2(boundary - half + 0.5f, 0) : ImVec2(0, boundary - half + 0.5f);
                const ImVec2 bandSize = axis == Axis::Horizontal ? ImVec2(g.options.hitSize, extent.y) : ImVec2(extent.x, g.options.hitSize);
                ImGui::SetCursorPos(bandAt);
                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginChild("##handle", bandSize, ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                bool hot = false;
                if (!g.options.disabled) {
                    ImGui::InvisibleButton("##drag", bandSize);
                    hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
                    if (hot) {
                        ImGui::SetMouseCursor(axis == Axis::Horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
                    }
                    const float mouse = along(ImGui::GetIO().MousePos, axis);
                    if (ImGui::IsItemActivated()) {
                        g.dragA = before;
                        g.dragB = static_cast<int>(i);
                        g.dragFrom = mouse;
                        g.startA = g.layout[static_cast<size_t>(before)];
                        g.startB = g.layout[i];
                    }
                    if (ImGui::IsItemActive() && g.dragB == static_cast<int>(i)) {
                        // from where the drag began, not frame by frame: the handle stays under
                        // the mouse after it meets a limit
                        const PanelOptions& a = g.panels[static_cast<size_t>(g.dragA)].options;
                        const PanelOptions& b = g.panels[i].options;
                        const float lo = std::max(a.min - g.startA, g.startB - upper(b));
                        const float hi = std::min(upper(a) - g.startA, g.startB - b.min);
                        const float d = std::round(std::clamp(mouse - g.dragFrom, std::min(lo, hi), std::max(lo, hi)));
                        g.panels[static_cast<size_t>(g.dragA)].size = g.startA + d;
                        g.panels[i].size = g.startB - d;
                        // the other panels keep the size they have now
                        for (size_t k = 0; k < g.panels.size(); ++k) {
                            if (k != i && static_cast<int>(k) != g.dragA && g.panels[k].options.visible) {
                                g.panels[k].size = g.layout[k];
                            }
                        }
                        requestFrames(std::chrono::milliseconds(100));
                    }
                    if (ImGui::IsItemDeactivated() && g.dragB == static_cast<int>(i)) {
                        g.dragA = g.dragB = -1;
                        g.released = true;
                    }
                }
                ImGui::EndChild();
                ImGui::PopID();
                handleLine(ImGui::GetWindowDrawList(), along(origin, axis) + boundary, origin,
                           ImVec2(origin.x + extent.x, origin.y + extent.y), axis, hot ? lit : line, hot);
            }
            at += g.layout[i] + 1;
            before = static_cast<int>(i);
        }
        // the sizes asked for (by a drag or the caller) are kept as they are: squeezing the
        // group does not change them, so they come back when there is room again
        for (Panel& p : g.panels) {
            if (p.options.size && p.size >= 0) {
                *p.options.size = p.size;
            }
        }
        if (g.changed) {
            requestFrames(std::chrono::milliseconds(50)); // laid out from this frame's options
        }
        ImGui::EndChild();
        return g.released;
    }
} // namespace imshell
