// every imshell::ui component and the editor on one page, on the desktops and in a browser (the
// website's demo)
#include "imshell/resizable.hpp"
#include "imshell/app.hpp"
#include "imshell/animation.hpp"
#include "imshell/button.hpp"
#include "imshell/components.hpp"
#include "imshell/editor.hpp"
#include "imshell/native_menu.hpp"
#include "imshell/fonts.hpp"
#include "imshell/icons.hpp"
#include "imshell/native_controls.hpp"
#include "imshell/select.hpp"
#include "imshell/toggle_button.hpp"
#include "imshell/themes.hpp"
#include "imshell/title_bar.hpp"
#include "imshell/vibrancy.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace ui = imshell::ui;

struct Gallery : imshell::Delegate {
    std::string email, password = "hunter2", name = "Ada Lovelace";
    bool chips[5] = {true, false, true, false, false};
    bool loading = true;
    int theme = 0;
    bool sidebar = std::getenv("IMSHELL_GALLERY_VIBRANCY") != nullptr;
    bool sideOpen = true;
    float sideBase = 200.0f;
    int sideRow = 0;
    imshell::TextEditor editor;

    Gallery() {
        using Token = imshell::TextEditor::Token;
        static constexpr std::string_view words[] = {"const", "else", "for", "if", "let", "return"};
        // a lexer in a few lines: comments, strings, numbers and a handful of keywords
        editor.SetHighlighter([](std::string_view text, std::span<Token> tokens, const auto&) {
            for (size_t i = 0; i < text.size();) {
                size_t end = i + 1;
                Token token = Token::Text;
                if (text.substr(i, 2) == "//") {
                    end = std::min(text.find('\n', i), text.size());
                    token = Token::Comment;
                } else if (text[i] == '"') {
                    end = std::min(text.find_first_of("\"\n", i + 1), text.size() - 1) + 1;
                    token = Token::String;
                } else if (std::isalnum(static_cast<unsigned char>(text[i]))) {
                    while (end < text.size() && std::isalnum(static_cast<unsigned char>(text[end])))
                        ++end;
                    const std::string_view word = text.substr(i, end - i);
                    if (std::isdigit(static_cast<unsigned char>(text[i])))
                        token = Token::Number;
                    else if (std::find(std::begin(words), std::end(words), word) != std::end(words))
                        token = Token::Keyword;
                    else if (end < text.size() && text[end] == '(')
                        token = Token::Function;
                }
                std::fill(tokens.begin() + i, tokens.begin() + end, token);
                i = end;
            }
        });
        // completion: the keywords that start with the word before the cursor
        editor.SetCompletionProvider([](std::string_view text, int cursor, bool) {
            int from = cursor;
            while (from > 0 && std::isalpha(static_cast<unsigned char>(text[from - 1])))
                --from;
            imshell::TextEditor::CompletionResponse response{.replaceStart = from,
                                                            .replaceEnd = cursor};
            for (const std::string_view word : words) {
                if (word.starts_with(text.substr(from, cursor - from)))
                    response.items.push_back(
                        {.text = std::string(word), .detailText = "keyword", .icon = "K"});
            }
            return response;
        });
        editor.SetLineComment("//");
        editor.SetText("// highlighting and completion are hooks the app fills\n"
                       "let total = 0\n"
                       "for (let i = 0; i < 10; i = i + 1) {\n"
                       "    total = add(total, i)\n"
                       "}\n"
                       "print(\"total\", total)\n");
    }

    // the system's own ui font where there is one, with the native icons merged in
    void setup() override {
        static std::vector<char> file;
        if (FILE* f = std::fopen("/System/Library/Fonts/SFNS.ttf", "rb")) {
            std::fseek(f, 0, SEEK_END);
            file.resize(static_cast<size_t>(std::ftell(f)));
            std::rewind(f);
            if (std::fread(file.data(), 1, file.size(), f) != file.size())
                file.clear();
            std::fclose(f);
        }
        if (!file.empty())
            imshell::AddFont({{file.data(), file.size()}}, 15.0f);
        else
            imshell::MergeIcons(ImGui::GetIO().Fonts->AddFontDefault());
    }

    void nativeIcons() {
        static const char* names[] = {"gearshape", "trash", "magnifyingglass", "plus", "folder", "doc.on.doc",
                                      "arrow.clockwise", "square.and.arrow.up", "star", "heart", "lock", "bell"};
        for (const char* n : names) {
            ImGui::Text("%s", imshell::Icon(n, "?"));
            ImGui::SameLine(0, 14);
        }
        ImGui::NewLine();
        imshell::IconLabelButton(imshell::Icon("plus", "+"), "New");
        ImGui::SameLine();
        imshell::IconLabelButton(imshell::Icon("trash", "x"), "Delete", imshell::ButtonVariant::Destructive);
        ImGui::SameLine();
        imshell::IconButton(imshell::Icon("gearshape", "*"), imshell::ButtonVariant::Ghost);
        ImGui::SameLine();
        imshell::IconButton(imshell::Icon("magnifyingglass", "?"));
    }

    // the os's own switch and segmented control beside imshell's, to compare
    void nativeControls() {
        static bool nativeOn = true, ownOn = true;
        static int nativeView = 0, ownView = 0, nativeSize = 1, ownSize = 1;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(imshell::NativeControlsAvailable() ? "Native" : "Fallback");
        ImGui::SameLine();
        imshell::NativeSwitch("##nativeSwitch", &nativeOn);
        ImGui::SameLine();
        imshell::NativeSegmented("##nativeSegments", {"List", "Grid", "Cards"}, &nativeView);
        ImGui::SameLine();
        imshell::NativePopUp("##nativePopUp", {"Small", "Medium", "Large"}, &nativeSize);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("imshell ");
        ImGui::SameLine();
        imshell::ToggleButton("##ownSwitch", &ownOn);
        ImGui::SameLine();
        imshell::SegmentedTabs("##ownSegments", {"List", "Grid", "Cards"}, &ownView);
        ImGui::SameLine();
        static const char* const sizes[] = {"Small", "Medium", "Large"};
        ImGui::SetNextItemWidth(110);
        imshell::Select("##ownPopUp", &ownSize, sizes, 3);
    }

    // a sidebar row in the system's style: a rounded highlight, an icon, then the label
    bool sidebarRow(const char* id, const char* symbol, const char* label, bool selected) {
        const imshell::Theme& t = imshell::CurrentTheme();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float height = 28.0f;
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(ImGui::GetContentRegionAvail().x, height));
        const float alpha = selected ? 0.14f : ImGui::IsItemHovered() ? 0.07f : 0.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (alpha > 0)
            draw->AddRectFilled(pos, ImVec2(pos.x + ImGui::GetItemRectSize().x, pos.y + height),
                                ImGui::GetColorU32(ImVec4(t.foreground.x, t.foreground.y, t.foreground.z, alpha)), 6.0f);
        const float textY = pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
        float x = pos.x + 10;
        if (const char* glyph = imshell::Icon(symbol)) {
            draw->AddText(ImVec2(x, textY), ImGui::GetColorU32(selected ? t.primary : t.mutedForeground), glyph);
            x += 26;
        }
        draw->AddText(ImVec2(x, textY), ImGui::GetColorU32(t.foreground), label);
        return clicked;
    }

    void frame() override {
        // a sidebar (IMSHELL_GALLERY_VIBRANCY=1 starts with it): the full window height, under the
        // title bar (whose own background is then transparent), in the window material where
        // the os has one. the title bar's button folds it, its right edge resizes it
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiID widthId = ImGui::GetID("sideWidth");
        const float sideWidth = sidebar ? imshell::Animate(widthId, sideOpen ? sideBase : 0.0f, imshell::kSmooth) : 0.0f;
        if (sidebar)
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        const auto bar = imshell::BeginTitleBar();
        if (sidebar) // the bar is transparent over the sidebar only; the rest keeps the theme's background
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bar.pos.x + sideWidth, bar.pos.y), ImVec2(bar.pos.x + bar.size.x, bar.pos.y + bar.size.y), ImGui::GetColorU32(imshell::CurrentTheme().background));
        if (sidebar) {
            // the fold button next to the traffic lights, as in the system's apps
            if (imshell::IconButton(imshell::Icon("sidebar.left", "="), imshell::ButtonVariant::Ghost))
                sideOpen = !sideOpen;
        }
        const char* title = "imshell components";
        const float left = std::max(sideWidth, bar.leftInset + 40);
        ImGui::SetCursorScreenPos(
            ImVec2(bar.pos.x + left + (bar.size.x - left - ImGui::CalcTextSize(title).x) / 2,
                   bar.pos.y + (bar.size.y - ImGui::GetTextLineHeight()) / 2));
        ImGui::TextDisabled("%s", title);
        imshell::EndTitleBar();
        if (sidebar)
            ImGui::PopStyleColor();

        const imshell::Rect content = imshell::ContentRect();
        if (sidebar && sideWidth > 1) {
            const bool material = imshell::VibrancyAvailable();
            if (material)
                imshell::Vibrancy(viewport->Pos, ImVec2(sideWidth, viewport->Size.y), imshell::VibrancyMaterial::Sidebar);
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(ImVec2(sideWidth, viewport->Size.y));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, material ? ImVec4(0, 0, 0, 0) : ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::Begin("##sidebar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::SetCursorPosY(imshell::TitleBarHeight() + 8); // below the traffic lights
            ImGui::PushStyleColor(ImGuiCol_Text, imshell::CurrentTheme().mutedForeground);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            ImGui::TextUnformatted("Mailboxes");
            ImGui::PopStyleColor();
            static const struct { const char* symbol; const char* label; } rows[] = {
                {"tray", "Inbox"}, {"doc", "Drafts"}, {"paperplane", "Sent"}, {"trash", "Trash"}};
            for (int i = 0; i < 4; ++i)
                if (sidebarRow(rows[i].label, rows[i].symbol, rows[i].label, sideRow == i))
                    sideRow = i;
            // a hairline on the right edge
            ImGui::GetWindowDrawList()->AddLine(ImVec2(viewport->Pos.x + sideWidth - 0.5f, viewport->Pos.y),
                                                ImVec2(viewport->Pos.x + sideWidth - 0.5f, viewport->Pos.y + viewport->Size.y),
                                                ImGui::GetColorU32(imshell::CurrentTheme().border));
            // the grip: a strip on the right edge, inside the sidebar so nothing sits over it
            ImGui::SetCursorScreenPos(ImVec2(viewport->Pos.x + sideWidth - 6, viewport->Pos.y + imshell::TitleBarHeight()));
            ImGui::InvisibleButton("##grip", ImVec2(6, viewport->Size.y - imshell::TitleBarHeight()));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive()) {
                sideBase = std::clamp(ImGui::GetIO().MousePos.x - viewport->Pos.x, 140.0f, 360.0f);
                imshell::SnapAnimation(widthId, sideBase); // follows the pointer, no easing
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor();
        }
        ImGui::SetNextWindowPos(ImVec2(content.pos.x + sideWidth, content.pos.y));
        ImGui::SetNextWindowSize(ImVec2(content.size.x - sideWidth, content.size.y));
        ImGui::Begin("gallery", nullptr, ImGuiWindowFlags_NoDecoration);
        themePicker();
        nativeIcons();
        nativeControls();
        ImGui::Spacing();
        const float column = std::min(420.0f, (ImGui::GetContentRegionAvail().x - 24) / 2);
        const bool twoColumns = ImGui::GetContentRegionAvail().x > 640;

        ImGui::BeginGroup();
        ImGui::PushItemWidth(column);
        ImGui::BeginChild("left", ImVec2(twoColumns ? column : 0, 0), ImGuiChildFlags_AutoResizeY);
        ui::BeginCard("signup", "Create an account");
        ui::TextField("Email", email,
                      {.placeholder = "you@example.com",
                       .error = !email.empty() && email.find('@') == std::string::npos
                                    ? "That doesn't look like an email address"
                                    : nullptr});
        ui::TextField("Password", password, {.hint = "At least 8 characters", .password = true});
        ImGui::Spacing();
        if (imshell::Button("Sign up", imshell::ButtonVariant::Primary))
            ui::Toast("Welcome aboard", ui::Tone::Success);
        ImGui::SameLine();
        if (imshell::Button("Cancel"))
            ui::Toast("Nothing was saved");
        ImGui::Spacing();
        imshell::Button("Delete", imshell::ButtonVariant::Destructive);
        ImGui::SameLine();
        imshell::Button("Outline", imshell::ButtonVariant::Outline);
        ImGui::SameLine();
        imshell::Button("Ghost", imshell::ButtonVariant::Ghost);
        ui::EndCard();

        ImGui::Spacing();
        ui::BeginCard("menu", "Context menu");
        ImGui::TextUnformatted("Right-click the row");
        ImGui::Selectable("invoice-2026.pdf");
        switch (imshell::NativeContextMenu("##rowMenu", [] {
            return std::vector<imshell::NativeMenuItem>{
                {.label = "Copy", .id = 1, .icon = "doc.on.doc"},
                {.label = "Rename", .id = 2, .icon = "pencil"},
                imshell::NativeMenuItem::Separator(),
                {.label = "Delete", .id = 3, .icon = "trash"},
            };
        })) {
        case 1: ui::Toast("Copied"); break;
        case 2: ui::Toast("Rename"); break;
        case 3: ui::Toast("Deleted", ui::Tone::Danger); break;
        default: break;
        }
        ui::EndCard();

        ImGui::Spacing();
        ui::BeginCard("status", "Status");
        ui::Badge("New", ui::Tone::Info);
        ImGui::SameLine();
        ui::Badge("Passing", ui::Tone::Success);
        ImGui::SameLine();
        ui::Badge("3 warnings", ui::Tone::Warning);
        ImGui::SameLine();
        ui::Badge("Failed", ui::Tone::Danger);
        ImGui::SameLine();
        ui::Badge("Draft");
        ImGui::Spacing();
        ui::Callout(
            "Heads up",
            "Callouts sit in the layout and wrap their text. For a modal, use imshell::Alert.",
            ui::Tone::Warning);
        ImGui::Spacing();
        ui::Progress(0.62f, "Uploading");
        ImGui::Spacing();
        ui::Progress(-1, "Indexing");
        ui::EndCard();
        ImGui::EndChild();
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        if (twoColumns)
            ImGui::SameLine(0, 24);
        ImGui::BeginChild("right", ImVec2(twoColumns ? column : 0, 0), ImGuiChildFlags_AutoResizeY);
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        ImGui::BeginChild("m1", ImVec2(half, 0), ImGuiChildFlags_AutoResizeY);
        ui::Metric("revenue", "Revenue", "$48.2k", "+12.4%");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("m2", ImVec2(half, 0), ImGuiChildFlags_AutoResizeY);
        ui::Metric("churn", "Churn", "2.1%", "-0.3%");
        ImGui::EndChild();

        ImGui::Spacing();
        ui::BeginCard("team", "Team");
        for (const char* member :
             {"Ada Lovelace", "Alan Turing", "Grace Hopper", "Linus Torvalds"}) {
            ui::Avatar(member);
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::Spacing();
        static const char* const tags[] = {"Design", "Backend", "iOS", "Android", "Web"};
        ui::Chips("tags", tags, chips);
        ui::EndCard();

        ImGui::Spacing();
        ui::BeginCard("loading", "Loading");
        if (loading) {
            ui::Skeleton(ImVec2(0, 14));
            ImGui::Spacing();
            ui::Skeleton(ImVec2(220, 14));
        } else {
            ui::EmptyState("No projects yet", "Projects you create show up here.", "New project");
        }
        ImGui::Spacing();
        if (imshell::SmallButton(loading ? "Show the empty state" : "Show the skeleton"))
            loading = !loading;
        ui::EndCard();
        ImGui::EndChild();

        ImGui::Spacing();
        ui::BeginCard("editor", "Editor");
        editor.Render("code", ImVec2(0, 200), true);
        ui::EndCard();

        // drag the lines between the panes; the left one keeps its width when the window resizes
        ImGui::Spacing();
        ui::BeginCard("panes", "Resizable");
        static float list = 160, preview = 0;
        static bool previewShown = true;
        ImGui::Checkbox("Preview", &previewShown);
        if (imshell::BeginResizable("panes", {.size = ImVec2(0, 160)})) {
            if (imshell::ResizablePanel({.size = &list, .min = 100, .fixed = true}))
                ImGui::TextDisabled("List, %.0f px", list);
            if (imshell::ResizablePanel({.min = 120})) {
                if (imshell::BeginResizable("stack", {.axis = imshell::Axis::Vertical})) {
                    if (imshell::ResizablePanel({.min = 40}))
                        ImGui::TextUnformatted("Top");
                    if (imshell::ResizablePanel({.min = 40}))
                        ImGui::TextUnformatted("Bottom");
                    imshell::EndResizable();
                }
            }
            if (imshell::ResizablePanel({.size = &preview, .min = 100, .visible = previewShown}))
                ImGui::TextDisabled("Preview");
            if (imshell::EndResizable())
                ui::Toast("Sizes saved");
        }
        ui::EndCard();

        ImGui::End();
        ui::RenderToasts();
    }

    void themePicker() {
        static std::vector<const char*> names = [] {
            std::vector<const char*> v;
            for (const auto& t : imshell::Themes())
                v.push_back(t.name);
            v.push_back("System");
            return v;
        }();
        ImGui::SetNextItemWidth(220);
        if (imshell::Select("##theme", &theme, names.data(), static_cast<int>(names.size()))) {
            imshell::SetTheme(theme < static_cast<int>(imshell::Themes().size()) ? imshell::Themes()[theme] : imshell::SystemTheme());
        }
        ImGui::SameLine();
        ImGui::Checkbox("Sidebar", &sidebar);
        ImGui::Spacing();
    }
};

static Gallery gallery; // android: outlives a recreated activity

int main() {
    const char* pick = std::getenv("IMSHELL_GALLERY_THEME"); // screenshots open one theme
    const std::string_view want = pick ? pick : "tokyo-night";
    if (want == "system") {
        imshell::SetTheme(imshell::SystemTheme());
        gallery.theme = static_cast<int>(imshell::Themes().size());
    }
    for (size_t i = 0; i < imshell::Themes().size(); ++i)
        if (want == imshell::Themes()[i].id) {
            imshell::SetTheme(imshell::Themes()[i]);
            gallery.theme = static_cast<int>(i);
        }
    imshell::Config config;
    config.title = "imshell components";
    config.width = 960;
    config.height = 720;
    config.titleBar = true;
    return imshell::Run(config, gallery);
}
