// every mydear::ui component on one page, on the desktops and in a browser (the website's demo)
#include "mydear/app.hpp"
#include "mydear/button.hpp"
#include "mydear/components.hpp"
#include "mydear/select.hpp"
#include "mydear/themes.hpp"
#include "mydear/title_bar.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace ui = mydear::ui;

struct Gallery : mydear::Delegate {
    std::string email, password = "hunter2", name = "Ada Lovelace";
    bool chips[5] = {true, false, true, false, false};
    bool loading = true;
    int theme = 0;

    void frame() override {
        const auto bar = mydear::BeginTitleBar();
        const char* title = "mydear components";
        ImGui::SetCursorScreenPos(
            ImVec2(bar.pos.x + (bar.size.x - ImGui::CalcTextSize(title).x) / 2,
                   bar.pos.y + (bar.size.y - ImGui::GetTextLineHeight()) / 2));
        ImGui::TextDisabled("%s", title);
        mydear::EndTitleBar();

        const mydear::Rect content = mydear::ContentRect();
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
        if (mydear::Button("Sign up", mydear::ButtonVariant::Primary))
            ui::Toast("Welcome aboard", ui::Tone::Success);
        ImGui::SameLine();
        if (mydear::Button("Cancel"))
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
            "Callouts sit in the layout and wrap their text. For a modal, use mydear::Alert.",
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
        if (mydear::SmallButton(loading ? "Show the empty state" : "Show the skeleton"))
            loading = !loading;
        ui::EndCard();
        ImGui::EndChild();

        ImGui::End();
        ui::RenderToasts();
    }

    void themePicker() {
        static std::vector<const char*> names = [] {
            std::vector<const char*> v;
            for (const auto& t : mydear::Theme::THEMES)
                v.push_back(t.name);
            return v;
        }();
        ImGui::SetNextItemWidth(220);
        if (mydear::Select("##theme", &theme, names.data(), static_cast<int>(names.size()))) {
            mydear::Theme::Current() = &mydear::Theme::THEMES[theme];
            mydear::Theme::ApplyTheme(mydear::Theme::Current()->colors);
        }
        ImGui::Spacing();
    }
};

static Gallery gallery; // android: outlives a recreated activity

int main() {
    mydear::Theme::Current() = &mydear::Theme::themeById("tokyo-night");
    gallery.theme = static_cast<int>(mydear::Theme::Current() - mydear::Theme::THEMES);
    mydear::Config config;
    config.title = "mydear components";
    config.width = 960;
    config.height = 720;
    config.titleBar = true;
    return mydear::Run(config, gallery);
}
