// the desktop's icon theme as glyphs of a merged font source on linux: an SF Symbol name maps to
// a freedesktop symbolic icon name (Adwaita and most themes have them), which gtk draws from
// the theme's svg into a cairo surface and keeps as coverage only, so the text colour tints it
#include "../emoji.hpp"
#include "../icons.hpp"

#include "imgui_internal.h"

#include <cairo.h>
#include <gtk/gtk.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace imshell::detail {
    namespace {
        // names are SF Symbols (what Icon() takes on every platform); values are freedesktop
        // icon names. a name with no entry, or one the theme lacks, has no icon here (the
        // caller's fallback shows). a name already ending in "-symbolic" is used as it is
        const std::unordered_map<std::string, const char*>& names() {
            static const std::unordered_map<std::string, const char*> map = {
                {"gearshape", "emblem-system-symbolic"}, {"gear", "emblem-system-symbolic"},
                {"trash", "user-trash-symbolic"}, {"doc.on.doc", "edit-copy-symbolic"},
                {"pencil", "document-edit-symbolic"}, {"plus", "list-add-symbolic"},
                {"minus", "list-remove-symbolic"}, {"xmark", "window-close-symbolic"},
                {"checkmark", "object-select-symbolic"}, {"magnifyingglass", "system-search-symbolic"},
                {"folder", "folder-symbolic"}, {"folder.badge.plus", "folder-new-symbolic"},
                {"doc", "text-x-generic-symbolic"}, {"doc.text", "text-x-generic-symbolic"},
                {"square.and.arrow.up", "document-send-symbolic"},
                {"square.and.arrow.down", "document-save-symbolic"},
                {"arrow.clockwise", "view-refresh-symbolic"}, {"chevron.right", "go-next-symbolic"},
                {"chevron.left", "go-previous-symbolic"}, {"chevron.down", "pan-down-symbolic"},
                {"chevron.up", "pan-up-symbolic"}, {"star", "non-starred-symbolic"},
                {"star.fill", "starred-symbolic"}, {"heart", "emblem-favorite-symbolic"},
                {"house", "user-home-symbolic"}, {"person", "avatar-default-symbolic"},
                {"bell", "preferences-system-notifications-symbolic"}, {"lock", "changes-prevent-symbolic"},
                {"lock.open", "changes-allow-symbolic"}, {"eye", "view-reveal-symbolic"},
                {"info.circle", "dialog-information-symbolic"},
                {"exclamationmark.triangle", "dialog-warning-symbolic"},
                {"play", "media-playback-start-symbolic"}, {"play.fill", "media-playback-start-symbolic"},
                {"pause", "media-playback-pause-symbolic"}, {"pause.fill", "media-playback-pause-symbolic"},
                {"stop", "media-playback-stop-symbolic"}, {"stop.fill", "media-playback-stop-symbolic"},
                {"paperclip", "mail-attachment-symbolic"}, {"link", "insert-link-symbolic"},
                {"tag", "tag-symbolic"}, {"calendar", "x-office-calendar-symbolic"},
                {"clock", "preferences-system-time-symbolic"}, {"printer", "printer-symbolic"},
                {"envelope", "mail-unread-symbolic"}, {"paperplane", "mail-send-symbolic"},
                {"ellipsis", "view-more-symbolic"}, {"line.3.horizontal", "open-menu-symbolic"},
                {"arrow.left", "go-previous-symbolic"}, {"arrow.right", "go-next-symbolic"},
                {"arrow.up", "go-up-symbolic"}, {"arrow.down", "go-down-symbolic"},
                {"camera", "camera-photo-symbolic"}, {"globe", "web-browser-symbolic"},
                {"wifi", "network-wireless-symbolic"}, {"bookmark", "bookmark-new-symbolic"},
                {"mic", "audio-input-microphone-symbolic"}, {"power", "system-shutdown-symbolic"},
                {"scissors", "edit-cut-symbolic"}, {"doc.on.clipboard", "edit-paste-symbolic"},
                {"arrow.uturn.backward", "edit-undo-symbolic"}, {"arrow.uturn.forward", "edit-redo-symbolic"},
                {"video", "camera-video-symbolic"}, {"photo", "image-x-generic-symbolic"},
                {"questionmark.circle", "dialog-question-symbolic"}, {"speaker", "audio-volume-high-symbolic"},
                {"speaker.slash", "audio-volume-muted-symbolic"}, {"message", "chat-message-new-symbolic"},
                {"tray", "mail-inbox-symbolic"},
            };
            return map;
        }

        // the theme of the default display; null with no display (then there is no loader)
        GtkIconTheme* theme() {
            static GtkIconTheme* t = [] {
                if (!gtk_is_initialized() && !gtk_init_check())
                    return static_cast<GtkIconTheme*>(nullptr);
                GdkDisplay* display = gdk_display_get_default();
                return display ? gtk_icon_theme_get_for_display(display) : nullptr;
            }();
            return t;
        }

        // the freedesktop name for a symbol name when the theme has it, else null
        const char* themeName(const std::string& name) {
            GtkIconTheme* t = theme();
            if (!t)
                return nullptr;
            const char* mapped = nullptr;
            if (const auto it = names().find(name); it != names().end())
                mapped = it->second;
            else if (std::string_view(name).ends_with("-symbolic"))
                mapped = name.c_str();
            return mapped && gtk_icon_theme_has_icon(t, mapped) ? mapped : nullptr;
        }

        bool srcInit(ImFontAtlas*, ImFontConfig*) {
            return true;
        }

        void srcDestroy(ImFontAtlas*, ImFontConfig*) {}

        // the glyph index is the codepoint's place in the block, plus one (0 is "none")
        int glyphIndex(ImFontAtlas*, ImFontConfig*, ImWchar codepoint) {
            const unsigned cp = static_cast<unsigned>(codepoint);
            return IconName(cp) ? static_cast<int>(cp - kIconBase) + 1 : 0;
        }

        bool containsGlyph(ImFontAtlas*, ImFontConfig*, int glyph) {
            return glyph > 0;
        }

        bool bakedInit(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {
            return true;
        }

        void bakedDestroy(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {}

        bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, int glyphIndex,
                       ImFontGlyph* out, float* outAdvance) {
            const std::string* name = IconName(kIconBase + static_cast<unsigned>(glyphIndex) - 1);
            const char* icon = name ? themeName(*name) : nullptr;
            if (!icon)
                return false;
            const float density = src->RasterizerDensity * baked->RasterizerDensity;
            const float em = baked->Size * density;
            const int size = std::max(1, static_cast<int>(std::lround(em)));
            if (outAdvance) {
                *outAdvance = static_cast<float>(size) / density;
                return true;
            }
            out->AdvanceX = static_cast<float>(size) / density;

            GtkIconPaintable* paintable = gtk_icon_theme_lookup_icon(
                theme(), icon, nullptr, size, 1, GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
            if (!paintable)
                return true; // advance only
            GtkSnapshot* snapshot = gtk_snapshot_new();
            gdk_paintable_snapshot(GDK_PAINTABLE(paintable), snapshot, size, size);
            GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
            bool ok = true;
            if (node) {
                cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
                cairo_t* cr = cairo_create(surface);
                gsk_render_node_draw(node, cr);
                cairo_destroy(cr);
                cairo_surface_flush(surface);
                atlas->Builder->TempBuffer.resize(size * size * 4);
                unsigned char* pixels = reinterpret_cast<unsigned char*>(atlas->Builder->TempBuffer.Data);
                const unsigned char* from = cairo_image_surface_get_data(surface);
                const int stride = cairo_image_surface_get_stride(surface);
                // coverage only: white, tinted by the text; cairo's alpha (byte 3 on little endian,
                // 0 on big) is the coverage
                const int alpha = G_BYTE_ORDER == G_LITTLE_ENDIAN ? 3 : 0;
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        unsigned char* p = pixels + (y * size + x) * 4;
                        p[0] = p[1] = p[2] = 255;
                        p[3] = from[y * stride + x * 4 + alpha];
                    }
                cairo_surface_destroy(surface);
                gsk_render_node_unref(node);
                // centred on the text: the middle of a capital sits about 0.35 em above the baseline
                ok = PutEmojiGlyph(atlas, src, baked, out, pixels, size, size, 0, static_cast<int>(std::lround(0.35f * em - size / 2.0f)), density, false);
            }
            g_object_unref(paintable);
            return ok;
        }
    } // namespace

    bool IconAvailable(const char* name) {
        return themeName(name) != nullptr;
    }

    const ImFontLoader* IconLoader() {
        if (!theme())
            return nullptr; // no display
        static ImFontLoader loader = [] {
            ImFontLoader l;
            l.Name = "Icon theme";
            l.FontSrcInit = srcInit;
            l.FontSrcDestroy = srcDestroy;
            l.FontSrcGetGlyphIndexFromCodepoint = glyphIndex;
            l.FontSrcContainsGlyph = containsGlyph;
            l.FontBakedInit = bakedInit;
            l.FontBakedDestroy = bakedDestroy;
            l.FontBakedLoadGlyph = loadGlyph;
            return l;
        }();
        return &loader;
    }
} // namespace imshell::detail
