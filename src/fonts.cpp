#include "imshell/fonts.hpp"

#include "emoji.hpp"
#include "imgui_internal.h"

#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace imshell {
    namespace {
        // a font file mapped read-only for the whole run. its pages stay clean: the os loads only
        // the glyph data drawn and can drop it again, where a read copy of a 23 mb .ttc stays
        // resident. empty if it can't be mapped
        std::span<char> mapFile(const std::string& path) {
#ifdef _WIN32
            HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                return {};
            LARGE_INTEGER size{};
            HANDLE mapping = GetFileSizeEx(file, &size)
                                 ? CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr)
                                 : nullptr;
            CloseHandle(file);
            if (!mapping)
                return {};
            void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            CloseHandle(mapping); // the view keeps the mapping alive
            return data ? std::span(static_cast<char*>(data), static_cast<size_t>(size.QuadPart))
                        : std::span<char>{};
#else
            const int fd = open(path.c_str(), O_RDONLY);
            if (fd < 0)
                return {};
            struct stat st{};
            void* data = fstat(fd, &st) == 0 && st.st_size > 0
                             ? mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)
                             : MAP_FAILED;
            close(fd); // the mapping keeps the file open
            return data == MAP_FAILED
                       ? std::span<char>{}
                       : std::span(static_cast<char*>(data), static_cast<size_t>(st.st_size));
#endif
        }

        // one mapping for every font that merges it: the atlas keeps the bytes for glyphs it
        // rasterizes later
        std::span<char> cjkData() {
            static const std::span<char> data = [] {
                const std::string path = SystemCjkFontPath();
                return path.empty() ? std::span<char>{} : mapFile(path);
            }();
            return data;
        }
    } // namespace

    // stb_truetype, imgui's default rasterizer, reads truetype and cff outlines but not cff2:
    // a variable cjk font (android 13 and later, fedora's default) fails its assert. reads the
    // table directory of the file's first font only, not the font
    static bool stbCanRead(const std::string& path) {
#ifdef IMGUI_ENABLE_FREETYPE
        return std::filesystem::exists(path); // freetype reads cff2
#else
        std::ifstream in(path, std::ios::binary);
        unsigned char h[12];
        const auto u32 = [](const unsigned char* p) {
            return uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3];
        };
        if (!in.read(reinterpret_cast<char*>(h), sizeof h))
            return false;
        if (std::memcmp(h, "ttcf", 4) == 0) { // a collection: the first font's offset follows
            unsigned char offset[4];
            if (!in.read(reinterpret_cast<char*>(offset), 4) || !in.seekg(u32(offset)) ||
                !in.read(reinterpret_cast<char*>(h), sizeof h))
                return false;
        }
        const int tables = h[4] << 8 | h[5];
        for (int i = 0; i < tables; ++i) {
            unsigned char record[16];
            if (!in.read(reinterpret_cast<char*>(record), sizeof record))
                return false;
            if (std::memcmp(record, "CFF2", 4) == 0)
                return false;
        }
        return tables > 0;
#endif
    }

    std::string SystemCjkFontPath() {
        static const char* candidates[] = {
#if defined(__ANDROID__)
            "/system/fonts/NotoSansCJK-Regular.ttc",
            "/system/fonts/NotoSerifCJK-Regular.ttc", // android 13+: the sans one is a variable
                                                      // font
#elif defined(__APPLE__)
            "/System/Library/Fonts/Hiragino Sans GB.ttc",
            "/System/Library/Fonts/PingFang.ttc",
            "/Library/Fonts/Arial Unicode.ttf",
#elif defined(_WIN32)
            "C:\\Windows\\Fonts\\msyh.ttc",    // microsoft yahei (sc)
            "C:\\Windows\\Fonts\\YuGothM.ttc", // yu gothic (jp)
            "C:\\Windows\\Fonts\\meiryo.ttc",
            "C:\\Windows\\Fonts\\msgothic.ttc",
#else
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc", // fedora
            // not fedora's default NotoSansCJK-VF.ttc: stb_truetype cannot read its cff2 outlines
            "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
            "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc",
#endif
        };
        for (const char* path : candidates)
            if (stbCanRead(path))
                return path;
        return {};
    }

    namespace {
        // the codepoints not in `ranges` (pairs, zero-terminated), as imgui's exclude list.
        // kept for the atlas's life, one list per ranges array
        const ImWchar* outsideOf(const ImWchar* ranges) {
            static std::map<const ImWchar*, std::vector<ImWchar>> made;
            auto& out = made[ranges];
            if (out.empty()) {
                unsigned next = 1;
                for (const ImWchar* r = ranges; r[0] != 0; r += 2) {
                    if (r[0] > next)
                        out.insert(out.end(), {static_cast<ImWchar>(next), static_cast<ImWchar>(r[0] - 1)});
                    next = static_cast<unsigned>(r[1]) + 1;
                }
                if (next <= IM_UNICODE_CODEPOINT_MAX)
                    out.insert(out.end(), {static_cast<ImWchar>(next), static_cast<ImWchar>(IM_UNICODE_CODEPOINT_MAX)});
                out.push_back(0);
            }
            return out.data();
        }
    } // namespace

    void MergeEmoji(ImFont* font) {
        static std::set<ImFont*> merged;
        const ImFontLoader* loader = font ? detail::EmojiLoader() : nullptr;
        if (!loader || !merged.insert(font).second)
            return;
        static unsigned char none = 0; // the loader reads the system font itself
        ImFontConfig config;
        config.FontLoader = loader;
        config.FontData = &none;
        config.FontDataSize = 1;
        config.FontDataOwnedByAtlas = false;
        config.MergeMode = true;
        config.DstFont = font;
        ImFormatString(config.Name, IM_ARRAYSIZE(config.Name), "%s", "emoji");
        ImGui::GetIO().Fonts->AddFont(&config);
    }

    ImFont* AddFont(std::span<const FontFile> files, float size, bool systemCjk, bool emoji) {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        ImFont* font = nullptr;
        for (const FontFile& f : files) {
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            config.MergeMode = font != nullptr;
            config.DstFont = font;
            if (f.icons) {
                config.OversampleH = config.OversampleV = 1;
                config.PixelSnapH = true;
            }
            // imgui 1.92 looks glyphs up past a merged source's ranges: an icon font that also
            // maps ordinary codepoints (font awesome has ✅ ❤ 👍) would take them from the
            // emoji and the text. everything outside its ranges is excluded instead
            if (f.ranges && font)
                config.GlyphExcludeRanges = outsideOf(f.ranges);
            ImFont* added = atlas->AddFontFromMemoryTTF(
                const_cast<void*>(f.data), static_cast<int>(f.size), size, &config, f.ranges);
            if (!font && !added)
                return nullptr; // no base to merge into
            if (!font)
                font = added;
        }
        // the system's colour emoji, before the cjk font so its pictures win over plain symbols
        if (emoji)
            MergeEmoji(font);
        if (font && systemCjk && !cjkData().empty()) {
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            config.MergeMode = true;
            config.DstFont = font;
            // japanese also covers the common chinese ideographs
            atlas->AddFontFromMemoryTTF(cjkData().data(), static_cast<int>(cjkData().size()), size,
                                        &config, atlas->GetGlyphRangesJapanese());
        }
        return font;
    }
} // namespace imshell
