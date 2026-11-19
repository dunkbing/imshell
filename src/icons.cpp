#include "imshell/icons.hpp"

#include "icons.hpp"

#include "imgui_internal.h"

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace imshell {
    namespace {
        struct Entry {
            std::string name;
            std::string glyph; // utf-8 of the codepoint; empty when the platform has no such icon
        };

        struct Registry {
            std::mutex mutex;
            std::deque<Entry> entries; // a deque: glyph strings are handed out and must not move
            std::unordered_map<std::string, size_t> byName;
        };

        Registry& registry() {
            static Registry r;
            return r;
        }

        std::string utf8(unsigned cp) {
            char buf[5] = {};
            ImTextCharToUtf8(buf, cp);
            return buf;
        }
    } // namespace

    const char* Icon(const char* name, const char* fallback) {
#ifdef IMGUI_USE_WCHAR32
        if (!name || !*name)
            return fallback;
        Registry& r = registry();
        std::lock_guard lock(r.mutex);
        auto it = r.byName.find(name);
        if (it == r.byName.end()) {
            Entry e{name, {}};
            if (detail::IconAvailable(name))
                e.glyph = utf8(detail::kIconBase + static_cast<unsigned>(r.entries.size()));
            r.entries.push_back(std::move(e));
            it = r.byName.emplace(name, r.entries.size() - 1).first;
        }
        const Entry& e = r.entries[it->second];
        return e.glyph.empty() ? fallback : e.glyph.c_str();
#else
        (void)name;
        return fallback;
#endif
    }

    void MergeIcons(ImFont* font) {
        const ImFontLoader* loader = font ? detail::IconLoader() : nullptr;
        if (!loader)
            return;
        for (const ImFontConfig* source : font->Sources) // once per font, as MergeEmoji
            if (source->FontLoader == loader)
                return;
        static unsigned char none = 0; // the loader reads the system's symbols itself
        ImFontConfig config;
        config.FontLoader = loader;
        config.FontData = &none;
        config.FontDataSize = 1;
        config.FontDataOwnedByAtlas = false;
        config.MergeMode = true;
        config.DstFont = font;
        ImFormatString(config.Name, IM_ARRAYSIZE(config.Name), "%s", "icons");
        ImGui::GetIO().Fonts->AddFont(&config);
    }

    namespace detail {
        const std::string* IconName(unsigned codepoint) {
            Registry& r = registry();
            std::lock_guard lock(r.mutex);
            if (codepoint < kIconBase || codepoint - kIconBase >= r.entries.size())
                return nullptr;
            return &r.entries[codepoint - kIconBase].name;
        }
    } // namespace detail
} // namespace imshell
