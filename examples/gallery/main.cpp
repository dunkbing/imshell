// every imshell::ui component and the editor on one page, on the desktops and in a browser (the
// website's demo)
#include "imshell/resizable.hpp"
#include "imshell/app.hpp"
#include "imshell/button.hpp"
#include "imshell/components.hpp"
#include "imshell/editor.hpp"
#include "imshell/select.hpp"
#include "imshell/themes.hpp"
#include "imshell/title_bar.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ui = imshell::ui;

struct Gallery : imshell::Delegate {
    std::string email, password = "hunter2", name = "Ada Lovelace";
    bool chips[5] = {true, false, true, false, false};
    bool loading = true;
    int theme = 0;
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

    void frame() override {
        const auto bar = imshell::BeginTitleBar();
        const char* title = "imshell components";
        ImGui::SetCursorScreenPos(
            ImVec2(bar.pos.x + (bar.size.x - ImGui::CalcTextSize(title).x) / 2,
                   bar.pos.y + (bar.size.y - ImGui::GetTextLineHeight()) / 2));
        ImGui::TextDisabled("%s", title);
        imshell::EndTitleBar();

        const imshell::Rect content = imshell::ContentRect();
        ImGui::SetNextWindowPos(content.pos);
        ImGui::SetNextWindowSize(content.size);
        ImGui::Begin("gallery", nullptr, ImGuiWindowFlags_NoDecoration);
        themePicker();
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
            for (const auto& t : imshell::Theme::THEMES)
                v.push_back(t.name);
            return v;
        }();
        ImGui::SetNextItemWidth(220);
        if (imshell::Select("##theme", &theme, names.data(), static_cast<int>(names.size()))) {
            imshell::Theme::Current() = &imshell::Theme::THEMES[theme];
            imshell::Theme::ApplyTheme(imshell::Theme::Current()->colors);
        }
        ImGui::Spacing();
    }
};

static Gallery gallery; // android: outlives a recreated activity

int main() {
    imshell::Theme::Current() = &imshell::Theme::themeById("tokyo-night");
    gallery.theme = static_cast<int>(imshell::Theme::Current() - imshell::Theme::THEMES);
    imshell::Config config;
    config.title = "imshell components";
    config.width = 960;
    config.height = 720;
    config.titleBar = true;
    return imshell::Run(config, gallery);
}
