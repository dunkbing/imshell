// the themes: each one's own palette (the colors its authors chose), turned into roles once
#include "imshell/themes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace imshell {
    namespace {
        constexpr ImVec4 hex(unsigned rgb) {
            return ImVec4(static_cast<float>((rgb >> 16) & 0xff) / 255.0f, static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
                          static_cast<float>(rgb & 0xff) / 255.0f, 1.0f);
        }

        // each palette maps onto these slots: accents first, then text, overlay (muted text,
        // borders), surface (fills), base (the window), mantle (sidebar) and crust (darkest edge).
        // light themes run the other way
    struct Palette {
        ImVec4 rose;
        ImVec4 coral;
        ImVec4 magenta;
        ImVec4 purple;
        ImVec4 red;
        ImVec4 maroon;
        ImVec4 peach;
        ImVec4 yellow;
        ImVec4 green;
        ImVec4 teal;
        ImVec4 sky;
        ImVec4 sapphire;
        ImVec4 blue;
        ImVec4 lavender;
        ImVec4 text;
        ImVec4 subtext1;
        ImVec4 subtext0;
        ImVec4 overlay2;
        ImVec4 overlay1;
        ImVec4 overlay0;
        ImVec4 surface2;
        ImVec4 surface1;
        ImVec4 surface0;
        ImVec4 base;
        ImVec4 mantle;
        ImVec4 crust;
    };

    // one dark
    constexpr Palette ONE_DARK = {
        ImVec4(0.88f, 0.67f, 0.69f, 1.0f), // rose       #e0abb0
        ImVec4(0.84f, 0.51f, 0.56f, 1.0f), // coral      #d5818e
        ImVec4(0.84f, 0.37f, 0.87f, 1.0f), // magenta    #d55fde
        ImVec4(0.78f, 0.47f, 0.87f, 1.0f), // purple     #c678dd
        ImVec4(0.88f, 0.42f, 0.46f, 1.0f), // red        #e06c75
        ImVec4(0.75f, 0.31f, 0.27f, 1.0f), // maroon     #be5046
        ImVec4(0.82f, 0.60f, 0.40f, 1.0f), // peach      #d19a66
        ImVec4(0.90f, 0.75f, 0.48f, 1.0f), // yellow     #e5c07b
        ImVec4(0.60f, 0.76f, 0.47f, 1.0f), // green      #98c379
        ImVec4(0.34f, 0.71f, 0.76f, 1.0f), // teal       #56b6c2
        ImVec4(0.38f, 0.69f, 0.94f, 1.0f), // sky        #61afef
        ImVec4(0.17f, 0.73f, 0.77f, 1.0f), // sapphire   #2bbac5
        ImVec4(0.38f, 0.69f, 0.94f, 1.0f), // blue       #61afef
        ImVec4(0.32f, 0.55f, 1.00f, 1.0f), // lavender   #528bff
        ImVec4(0.67f, 0.70f, 0.75f, 1.0f), // text       #abb2bf
        ImVec4(0.62f, 0.65f, 0.71f, 1.0f), // subtext1   #9da5b4
        ImVec4(0.51f, 0.54f, 0.59f, 1.0f), // subtext0   #828997
        ImVec4(0.39f, 0.43f, 0.51f, 1.0f), // overlay2   #636d83
        ImVec4(0.36f, 0.39f, 0.44f, 1.0f), // overlay1   #5c6370
        ImVec4(0.29f, 0.32f, 0.39f, 1.0f), // overlay0   #4b5263
        ImVec4(0.24f, 0.26f, 0.31f, 1.0f), // surface2   #3d4350
        ImVec4(0.21f, 0.23f, 0.27f, 1.0f), // surface1   #353b45
        ImVec4(0.17f, 0.19f, 0.24f, 1.0f), // surface0   #2c313c
        ImVec4(0.16f, 0.17f, 0.20f, 1.0f), // base       #282c34
        ImVec4(0.13f, 0.15f, 0.17f, 1.0f), // mantle     #21252b
        ImVec4(0.10f, 0.11f, 0.14f, 1.0f)  // crust      #1a1d23
    };

    // one light
    constexpr Palette ONE_LIGHT = {
        ImVec4(0.88f, 0.38f, 0.44f, 1.0f), // rose       #e06070
        ImVec4(0.82f, 0.25f, 0.38f, 1.0f), // coral      #d04060
        ImVec4(0.77f, 0.27f, 0.61f, 1.0f), // magenta    #c4449c
        ImVec4(0.65f, 0.15f, 0.64f, 1.0f), // purple     #a626a4
        ImVec4(0.89f, 0.34f, 0.29f, 1.0f), // red        #e45649
        ImVec4(0.79f, 0.07f, 0.26f, 1.0f), // maroon     #ca1243
        ImVec4(0.84f, 0.37f, 0.00f, 1.0f), // peach      #d75f00
        ImVec4(0.76f, 0.51f, 0.00f, 1.0f), // yellow     #c18401
        ImVec4(0.31f, 0.63f, 0.31f, 1.0f), // green      #50a14f
        ImVec4(0.00f, 0.52f, 0.74f, 1.0f), // teal       #0184bc
        ImVec4(0.25f, 0.47f, 0.95f, 1.0f), // sky        #4078f2
        ImVec4(0.13f, 0.63f, 0.77f, 1.0f), // sapphire   #20a0c5
        ImVec4(0.25f, 0.47f, 0.95f, 1.0f), // blue       #4078f2
        ImVec4(0.45f, 0.36f, 0.87f, 1.0f), // lavender   #735cde
        ImVec4(0.22f, 0.23f, 0.26f, 1.0f), // text       #383a42
        ImVec4(0.29f, 0.33f, 0.39f, 1.0f), // subtext1   #4b5563
        ImVec4(0.41f, 0.42f, 0.47f, 1.0f), // subtext0   #696c77
        ImVec4(0.63f, 0.63f, 0.65f, 1.0f), // overlay2   #a0a1a7
        ImVec4(0.72f, 0.72f, 0.72f, 1.0f), // overlay1   #b8b8b8
        ImVec4(0.83f, 0.83f, 0.83f, 1.0f), // overlay0   #d4d4d4
        ImVec4(0.82f, 0.82f, 0.82f, 1.0f), // surface2   #d0d0d0 (borders/dividers)
        ImVec4(0.88f, 0.88f, 0.88f, 1.0f), // surface1   #e0e0e0
        ImVec4(0.93f, 0.93f, 0.94f, 1.0f), // surface0   #edeef0
        ImVec4(0.98f, 0.98f, 0.98f, 1.0f), // base       #fafafa
        ImVec4(0.94f, 0.94f, 0.94f, 1.0f), // mantle     #f0f0f0
        ImVec4(0.90f, 0.90f, 0.91f, 1.0f)  // crust      #e6e6e8
    };

    // clang-format off
    constexpr Palette AYU_DARK = {
        hex(0xf29668), hex(0xf07178), hex(0xd2a6ff), hex(0xd2a6ff), hex(0xf07178), hex(0xd95757),
        hex(0xff8f40), hex(0xffb454), hex(0xaad94c), hex(0x95e6cb), hex(0x39bae6), hex(0x39bae6),
        hex(0x59c2ff), hex(0x73b8ff), hex(0xbfbdb6), hex(0xacb6bf), hex(0x8a9199), hex(0x6c7380),
        hex(0x565b66), hex(0x3d424d), hex(0x2a2f3a), hex(0x1f2430), hex(0x171b24), hex(0x0d1017),
        hex(0x0b0e14), hex(0x080a0f)};
    constexpr Palette AYU_MIRAGE = {
        hex(0xf29e74), hex(0xf28779), hex(0xdfbfff), hex(0xdfbfff), hex(0xf28779), hex(0xff6666),
        hex(0xffad66), hex(0xffd173), hex(0xd5ff80), hex(0x95e6cb), hex(0x5ccfe6), hex(0x5ccfe6),
        hex(0x73d0ff), hex(0x80bfff), hex(0xcccac2), hex(0xb8b6ad), hex(0x8a9199), hex(0x707a8c),
        hex(0x5c6773), hex(0x4b5466), hex(0x3d4455), hex(0x33394a), hex(0x2a3040), hex(0x242936),
        hex(0x1f2430), hex(0x1a1f29)};
    constexpr Palette AYU_LIGHT = {
        hex(0xed9366), hex(0xf07171), hex(0xa37acc), hex(0xa37acc), hex(0xe65050), hex(0xd14b4b),
        hex(0xfa8d3e), hex(0xf2ae49), hex(0x86b300), hex(0x4cbf99), hex(0x55b4d4), hex(0x55b4d4),
        hex(0x399ee6), hex(0x478acc), hex(0x5c6166), hex(0x6b7075), hex(0x8a9199), hex(0xa0a6ad),
        hex(0xb8bec4), hex(0xcfd3d8), hex(0xd8dce0), hex(0xe3e6ea), hex(0xeceff2), hex(0xfcfcfc),
        hex(0xf3f4f5), hex(0xe7eaed)};
    constexpr Palette GRUVBOX_DARK = {
        hex(0xd3869b), hex(0xea6962), hex(0xd3869b), hex(0xb16286), hex(0xfb4934), hex(0xcc241d),
        hex(0xfe8019), hex(0xfabd2f), hex(0xb8bb26), hex(0x8ec07c), hex(0x83a598), hex(0x689d6a),
        hex(0x83a598), hex(0x7daea3), hex(0xebdbb2), hex(0xd5c4a1), hex(0xbdae93), hex(0xa89984),
        hex(0x928374), hex(0x7c6f64), hex(0x665c54), hex(0x504945), hex(0x3c3836), hex(0x282828),
        hex(0x1d2021), hex(0x141617)};
    constexpr Palette GRUVBOX_LIGHT = {
        hex(0xb16286), hex(0xcc241d), hex(0xb16286), hex(0x8f3f71), hex(0xcc241d), hex(0x9d0006),
        hex(0xd65d0e), hex(0xb57614), hex(0x79740e), hex(0x427b58), hex(0x458588), hex(0x689d6a),
        hex(0x076678), hex(0x458588), hex(0x3c3836), hex(0x504945), hex(0x665c54), hex(0x7c6f64),
        hex(0x928374), hex(0xa89984), hex(0xbdae93), hex(0xd5c4a1), hex(0xebdbb2), hex(0xfbf1c7),
        hex(0xf2e5bc), hex(0xebdbb2)};
    constexpr Palette VSCODE_DARK = {
        hex(0xce9178), hex(0xf48771), hex(0xc586c0), hex(0xc586c0), hex(0xf14c4c), hex(0xd16969),
        hex(0xce9178), hex(0xdcdcaa), hex(0x89d185), hex(0x4ec9b0), hex(0x9cdcfe), hex(0x4fc1ff),
        hex(0x569cd6), hex(0x0078d4), hex(0xcccccc), hex(0xbbbbbb), hex(0x9d9d9d), hex(0x858585),
        hex(0x6e7681), hex(0x454545), hex(0x3c3c3c), hex(0x313131), hex(0x2b2b2b), hex(0x1f1f1f),
        hex(0x181818), hex(0x111111)};
    constexpr Palette VSCODE_LIGHT = {
        hex(0xb05a78), hex(0xcd3131), hex(0xaf00db), hex(0xaf00db), hex(0xe51400), hex(0xa31515),
        hex(0xb8510f), hex(0xbf8803), hex(0x388a34), hex(0x267f99), hex(0x0070c1), hex(0x0451a5),
        hex(0x005fb8), hex(0x4b69c6), hex(0x3b3b3b), hex(0x4f4f4f), hex(0x616161), hex(0x8a8a8a),
        hex(0xa5a5a5), hex(0xc4c4c4), hex(0xcecece), hex(0xe5e5e5), hex(0xeeeeee), hex(0xffffff),
        hex(0xf8f8f8), hex(0xefefef)};
    constexpr Palette CATPPUCCIN_MOCHA = {
        hex(0xf5e0dc), hex(0xf2cdcd), hex(0xf5c2e7), hex(0xcba6f7), hex(0xf38ba8), hex(0xeba0ac),
        hex(0xfab387), hex(0xf9e2af), hex(0xa6e3a1), hex(0x94e2d5), hex(0x89dceb), hex(0x74c7ec),
        hex(0x89b4fa), hex(0xb4befe), hex(0xcdd6f4), hex(0xbac2de), hex(0xa6adc8), hex(0x9399b2),
        hex(0x7f849c), hex(0x6c7086), hex(0x585b70), hex(0x45475a), hex(0x313244), hex(0x1e1e2e),
        hex(0x181825), hex(0x11111b)};
    constexpr Palette CATPPUCCIN_LATTE = {
        hex(0xdc8a78), hex(0xdd7878), hex(0xea76cb), hex(0x8839ef), hex(0xd20f39), hex(0xe64553),
        hex(0xfe640b), hex(0xdf8e1d), hex(0x40a02b), hex(0x179299), hex(0x04a5e5), hex(0x209fb5),
        hex(0x1e66f5), hex(0x7287fd), hex(0x4c4f69), hex(0x5c5f77), hex(0x6c6f85), hex(0x7c7f93),
        hex(0x8c8fa1), hex(0x9ca0b0), hex(0xacb0be), hex(0xbcc0cc), hex(0xccd0da), hex(0xeff1f5),
        hex(0xe6e9ef), hex(0xdce0e8)};
    constexpr Palette TOKYO_NIGHT = {
        hex(0xf7768e), hex(0xff757f), hex(0xbb9af7), hex(0x9d7cd8), hex(0xf7768e), hex(0xdb4b4b),
        hex(0xff9e64), hex(0xe0af68), hex(0x9ece6a), hex(0x73daca), hex(0x7dcfff), hex(0x2ac3de),
        hex(0x7aa2f7), hex(0xb4bdf8), hex(0xc0caf5), hex(0xa9b1d6), hex(0x9aa5ce), hex(0x737aa2),
        hex(0x565f89), hex(0x545c7e), hex(0x414868), hex(0x3b4261), hex(0x292e42), hex(0x1a1b26),
        hex(0x16161e), hex(0x101014)};
    constexpr Palette NORD = {
        hex(0xd08770), hex(0xbf616a), hex(0xb48ead), hex(0xb48ead), hex(0xbf616a), hex(0xa5545c),
        hex(0xd08770), hex(0xebcb8b), hex(0xa3be8c), hex(0x8fbcbb), hex(0x88c0d0), hex(0x88c0d0),
        hex(0x81a1c1), hex(0x5e81ac), hex(0xd8dee9), hex(0xc8ced9), hex(0xa7afbe), hex(0x8a93a6),
        hex(0x6d778b), hex(0x616e88), hex(0x4c566a), hex(0x434c5e), hex(0x3b4252), hex(0x2e3440),
        hex(0x2a2f3a), hex(0x242933)};
    constexpr Palette DRACULA = {
        hex(0xff92df), hex(0xff6e6e), hex(0xff79c6), hex(0x9580ff), hex(0xff5555), hex(0xff6e6e),
        hex(0xffb86c), hex(0xf1fa8c), hex(0x50fa7b), hex(0x8be9fd), hex(0x8be9fd), hex(0x62d6e8),
        hex(0xbd93f9), hex(0xbd93f9), hex(0xf8f8f2), hex(0xe2e2dc), hex(0xbfbfbf), hex(0x9ea8c7),
        hex(0x6272a4), hex(0x545772), hex(0x4d5066), hex(0x44475a), hex(0x343746), hex(0x282a36),
        hex(0x21222c), hex(0x191a21)};
    constexpr Palette SOLARIZED_DARK = {
        hex(0xd33682), hex(0xdc322f), hex(0xd33682), hex(0x6c71c4), hex(0xdc322f), hex(0xa12a26),
        hex(0xcb4b16), hex(0xb58900), hex(0x859900), hex(0x2aa198), hex(0x2aa198), hex(0x268bd2),
        hex(0x268bd2), hex(0x6c71c4), hex(0x93a1a1), hex(0x839496), hex(0x657b83), hex(0x586e75),
        hex(0x4a5f66), hex(0x37515a), hex(0x1e4652), hex(0x103e4a), hex(0x073642), hex(0x002b36),
        hex(0x00252e), hex(0x001e26)};
    constexpr Palette SOLARIZED_LIGHT = {
        hex(0xd33682), hex(0xdc322f), hex(0xd33682), hex(0x6c71c4), hex(0xdc322f), hex(0xa12a26),
        hex(0xcb4b16), hex(0xb58900), hex(0x859900), hex(0x2aa198), hex(0x2aa198), hex(0x268bd2),
        hex(0x268bd2), hex(0x6c71c4), hex(0x586e75), hex(0x657b83), hex(0x839496), hex(0x93a1a1),
        hex(0xb3b8ad), hex(0xcfcfc0), hex(0xdcd8c4), hex(0xe6e0cc), hex(0xeee8d5), hex(0xfdf6e3),
        hex(0xf5efdc), hex(0xeee8d5)};
    constexpr Palette GITHUB_DARK = {
        hex(0xffa198), hex(0xff7b72), hex(0xdb61a2), hex(0xd2a8ff), hex(0xf85149), hex(0xda3633),
        hex(0xffa657), hex(0xd29922), hex(0x3fb950), hex(0x56d4dd), hex(0xa5d6ff), hex(0x79c0ff),
        hex(0x58a6ff), hex(0x2f81f7), hex(0xe6edf3), hex(0xc9d1d9), hex(0x8b949e), hex(0x7d8590),
        hex(0x6e7681), hex(0x484f58), hex(0x30363d), hex(0x21262d), hex(0x161b22), hex(0x0d1117),
        hex(0x010409), hex(0x000000)};
    constexpr Palette GITHUB_LIGHT = {
        hex(0xbf3989), hex(0xcf222e), hex(0xbf3989), hex(0x8250df), hex(0xd1242f), hex(0xa40e26),
        hex(0xbc4c00), hex(0x9a6700), hex(0x1a7f37), hex(0x1b7c83), hex(0x0550ae), hex(0x0a3069),
        hex(0x0969da), hex(0x6639ba), hex(0x1f2328), hex(0x424a53), hex(0x656d76), hex(0x818b98),
        hex(0xafb8c1), hex(0xd0d7de), hex(0xd8dee4), hex(0xeaeef2), hex(0xf6f8fa), hex(0xffffff),
        hex(0xf6f8fa), hex(0xeaeef2)};
    // clang-format on

        struct Entry {
            const char* id;
            const char* name;
            bool dark;
            Palette palette;
        };
        constexpr Entry ENTRIES[] = {
        {"one-dark", "One Dark", true, ONE_DARK},
        {"one-light", "One Light", false, ONE_LIGHT},
        {"ayu-dark", "Ayu Dark", true, AYU_DARK},
        {"ayu-mirage", "Ayu Mirage", true, AYU_MIRAGE},
        {"ayu-light", "Ayu Light", false, AYU_LIGHT},
        {"gruvbox-dark", "Gruvbox Dark", true, GRUVBOX_DARK},
        {"gruvbox-light", "Gruvbox Light", false, GRUVBOX_LIGHT},
        {"vscode-dark", "VS Code Dark", true, VSCODE_DARK},
        {"vscode-light", "VS Code Light", false, VSCODE_LIGHT},
        {"catppuccin-mocha", "Catppuccin Mocha", true, CATPPUCCIN_MOCHA},
        {"catppuccin-latte", "Catppuccin Latte", false, CATPPUCCIN_LATTE},
        {"tokyo-night", "Tokyo Night", true, TOKYO_NIGHT},
        {"nord", "Nord", true, NORD},
        {"dracula", "Dracula", true, DRACULA},
        {"solarized-dark", "Solarized Dark", true, SOLARIZED_DARK},
        {"solarized-light", "Solarized Light", false, SOLARIZED_LIGHT},
        {"github-dark", "GitHub Dark", true, GITHUB_DARK},
        {"github-light", "GitHub Light", false, GITHUB_LIGHT},
        };
        constexpr size_t COUNT = std::size(ENTRIES);

        ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        float luminance(const ImVec4& c) {
            const auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
            return 0.2126f * lin(c.x) + 0.7152f * lin(c.y) + 0.0722f * lin(c.z);
        }

        float contrast(const ImVec4& a, const ImVec4& b) {
            const float la = luminance(a), lb = luminance(b);
            return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
        }

        // steps color toward `toward` until it reads at `min` against `against`: palettes that fall
        // short of wcag on some pair (ayu light's blue, solarized's greys) move only as far as needed
        ImVec4 reach(ImVec4 color, const ImVec4& toward, const ImVec4& against, float min) {
            for (int i = 0; i < 20 && contrast(color, against) < min; ++i)
                color = mix(color, toward, 0.1f);
            return color;
        }

        // text on a colored fill: whichever of the theme's two ends reads better on it
        ImVec4 onFill(const ImVec4& fill, const Palette& p) {
            return contrast(fill, p.base) >= contrast(fill, p.text) ? p.base : p.text;
        }

        Theme roles(const Entry& e) {
            const Palette& p = e.palette;
            Theme t{};
            t.id = e.id;
            t.name = e.name;
            t.dark = e.dark;
            t.background = p.base;
            t.foreground = p.text;
            t.card = mix(p.base, p.surface0, 0.35f);
            t.cardForeground = p.text;
            // light popups stay white with a border; dark ones lift off the window
            t.popover = e.dark ? p.surface0 : p.base;
            t.popoverForeground = p.text;
            t.sidebar = p.mantle;
            t.sidebarForeground = p.text;
            t.muted = p.surface0;
            t.mutedForeground = p.subtext0;
            t.primary = p.blue;
            t.primaryForeground = onFill(p.blue, p);
            // toward the text, so buttons stand out on windows and popups alike (the surface
            // steps are too close, and not ordered in some light palettes)
            t.secondary = mix(p.surface0, p.text, 0.14f);
            t.secondaryForeground = p.text;
            t.accent = p.surface1;
            t.accentForeground = p.text;
            t.destructive = p.red;
            t.destructiveForeground = onFill(p.red, p);
            t.success = p.green;
            t.warning = p.peach;
            t.border = p.overlay0;
            t.input = p.surface0;
            t.ring = p.blue;
            t.chart[0] = p.blue;
            t.chart[1] = p.green;
            t.chart[2] = p.peach;
            t.chart[3] = p.purple;
            t.chart[4] = p.red;
            // wcag aa: body text 4.5, quiet text, labels on fills and links 3 (tests/theme_test.cpp)
            t.primary = reach(t.primary, p.text, p.base, 3.0f);
            t.ring = t.primary;
            t.primaryForeground = onFill(t.primary, p);
            t.mutedForeground = reach(t.mutedForeground, p.text, p.base, 3.0f);
            t.secondary = reach(t.secondary, p.base, p.text, 4.5f);
            t.syntax = {p.purple, p.green, p.peach, p.overlay2, p.blue, p.yellow, p.teal};
            return t;
        }

        struct Table {
            Theme themes[COUNT];
            Table() {
                for (size_t i = 0; i < COUNT; ++i)
                    themes[i] = roles(ENTRIES[i]);
            }
        };

        const Table& table() {
            static const Table t;
            return t;
        }

        size_t gCurrent = 0;
    } // namespace

    std::span<const Theme> Themes() {
        return table().themes;
    }

    const Theme& ThemeById(std::string_view id) {
        if (id == "light")
            id = "one-light";
        else if (id == "dark")
            id = "one-dark";
        for (const Theme& t : Themes())
            if (id == t.id)
                return t;
        return Themes()[0];
    }

    const Theme& theme() {
        return table().themes[gCurrent];
    }

    void SetTheme(const Theme& t) {
        const Theme* first = table().themes;
        // a theme from Themes() by place; another (an app's own) by id
        gCurrent = &t >= first && &t < first + COUNT ? static_cast<size_t>(&t - first)
                                                     : static_cast<size_t>(&ThemeById(t.id) - first);
        if (ImGui::GetCurrentContext())
            ApplyTheme();
    }

    void SetTheme(std::string_view id) {
        SetTheme(ThemeById(id));
    }

    ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
        return mix(a, b, t);
    }

    ImVec4 WithAlpha(const ImVec4& c, float alpha) {
        return ImVec4(c.x, c.y, c.z, alpha);
    }

    ImVec4 Hovered(const ImVec4& c) {
        return WithAlpha(mix(c, theme().foreground, 0.10f), c.w);
    }

    ImVec4 Pressed(const ImVec4& c) {
        return WithAlpha(mix(c, theme().foreground, 0.20f), c.w);
    }

    bool TagColor(std::string_view key, ImVec4& out) {
        const Palette& p = ENTRIES[gCurrent].palette;
        const struct {
            std::string_view key;
            const ImVec4& color;
        } tags[] = {{"red", p.red}, {"peach", p.peach}, {"yellow", p.yellow},
                    {"green", p.green}, {"blue", p.blue}, {"purple", p.purple}};
        for (const auto& tag : tags)
            if (key == tag.key) {
                out = tag.color;
                return true;
            }
        return false;
    }

    float BottomBarHeight() {
        return ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
    }

    void ApplyTheme() {
        const Theme& t = theme();
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowPadding = ImVec2(16.0f, 16.0f);
        style.FramePadding = ImVec2(8.0f, 6.0f);
        style.CellPadding = ImVec2(8.0f, 6.0f);
        style.ItemSpacing = ImVec2(8.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing = 20.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 12.0f;

        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 0.0f;
        style.TabBarOverlineSize = 0.0f;
        style.DockingSeparatorSize = 1.0f; // the sidebar handle: hover padding keeps it easy to grab

        style.WindowRounding = 0.0f;
        style.ChildRounding = Radius::Lg;
        style.FrameRounding = Radius::Md;
        style.PopupRounding = Radius::Md;
        style.ScrollbarRounding = Radius::Full;
        style.GrabRounding = Radius::Sm;
        style.TabRounding = 0.0f;

        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Left;
        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

        style.Alpha = 1.0f;
        style.DisabledAlpha = 0.5f;
        style.WindowMinSize = ImVec2(200.0f, 200.0f);

        const ImVec4 clear(0, 0, 0, 0);
        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = t.foreground;
        c[ImGuiCol_InputTextCursor] = t.foreground;
        c[ImGuiCol_TextDisabled] = t.mutedForeground;
        c[ImGuiCol_TextLink] = t.primary;
        c[ImGuiCol_WindowBg] = t.background;
        c[ImGuiCol_ChildBg] = clear;
        c[ImGuiCol_PopupBg] = t.popover;
        c[ImGuiCol_Border] = t.border;
        c[ImGuiCol_BorderShadow] = clear;
        c[ImGuiCol_FrameBg] = t.input;
        c[ImGuiCol_FrameBgHovered] = Hovered(t.input);
        c[ImGuiCol_FrameBgActive] = Pressed(t.input);
        c[ImGuiCol_TitleBg] = t.sidebar;
        c[ImGuiCol_TitleBgActive] = t.sidebar;
        c[ImGuiCol_TitleBgCollapsed] = t.sidebar;
        c[ImGuiCol_MenuBarBg] = t.background;
        c[ImGuiCol_ScrollbarBg] = clear;
        c[ImGuiCol_ScrollbarGrab] = t.border;
        c[ImGuiCol_ScrollbarGrabHovered] = Hovered(t.border);
        c[ImGuiCol_ScrollbarGrabActive] = Pressed(t.border);
        c[ImGuiCol_CheckMark] = t.primary;
        c[ImGuiCol_CheckboxSelectedBg] = t.input; // imgui keeps a dark default otherwise
        c[ImGuiCol_SliderGrab] = t.primary;
        c[ImGuiCol_SliderGrabActive] = Pressed(t.primary);
        c[ImGuiCol_Button] = t.secondary;
        c[ImGuiCol_ButtonHovered] = Hovered(t.secondary);
        c[ImGuiCol_ButtonActive] = Pressed(t.secondary);
        // selection is the primary at rising strength; a hovered row is the accent
        c[ImGuiCol_Header] = WithAlpha(t.primary, 0.20f);
        c[ImGuiCol_HeaderHovered] = t.accent;
        c[ImGuiCol_HeaderActive] = WithAlpha(t.primary, 0.40f);
        c[ImGuiCol_Separator] = t.border;
        c[ImGuiCol_SeparatorHovered] = Hovered(t.border);
        c[ImGuiCol_SeparatorActive] = t.primary;
        c[ImGuiCol_ResizeGrip] = t.border;
        c[ImGuiCol_ResizeGripHovered] = Hovered(t.border);
        c[ImGuiCol_ResizeGripActive] = t.primary;
        c[ImGuiCol_Tab] = t.sidebar;
        c[ImGuiCol_TabHovered] = t.accent;
        c[ImGuiCol_TabSelected] = t.background;
        c[ImGuiCol_TabSelectedOverline] = t.primary;
        c[ImGuiCol_TabDimmed] = t.sidebar;
        c[ImGuiCol_TabDimmedSelected] = t.background;
        c[ImGuiCol_TabDimmedSelectedOverline] = t.border;
        c[ImGuiCol_DockingPreview] = WithAlpha(t.primary, 0.3f);
        c[ImGuiCol_DockingEmptyBg] = t.background;
        c[ImGuiCol_PlotLines] = t.chart[0];
        c[ImGuiCol_PlotLinesHovered] = Hovered(t.chart[0]);
        c[ImGuiCol_PlotHistogram] = t.chart[0];
        c[ImGuiCol_PlotHistogramHovered] = Hovered(t.chart[0]);
        c[ImGuiCol_TableHeaderBg] = t.muted;
        c[ImGuiCol_TableBorderStrong] = t.border;
        c[ImGuiCol_TableBorderLight] = WithAlpha(t.border, 0.6f);
        c[ImGuiCol_TableRowBg] = clear;
        c[ImGuiCol_TableRowBgAlt] = WithAlpha(t.muted, 0.5f);
        c[ImGuiCol_TreeLines] = t.border;
        c[ImGuiCol_TextSelectedBg] = WithAlpha(t.primary, 0.3f);
        c[ImGuiCol_DragDropTarget] = WithAlpha(t.primary, 0.8f);
        c[ImGuiCol_DragDropTargetBg] = WithAlpha(t.primary, 0.15f);
        c[ImGuiCol_UnsavedMarker] = t.warning;
        c[ImGuiCol_NavCursor] = t.ring;
        c[ImGuiCol_NavWindowingHighlight] = t.ring;
        c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.2f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.4f);
    }
} // namespace imshell
