#include "imshell/markdown.hpp"

#include "imshell/fonts.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imshell/themes.hpp"
#include <algorithm>
#include <cstdint>
#include <format>
#include <imgui_rich_md/rich_md.h>
#include <memory>
#include <unordered_map>

namespace imshell::Markdown {
    namespace {
        Options gOptions;

        // the Render() in progress: code blocks are drawn from inside RichMd::RenderRaw
        struct Scope {
            const std::string* idPrefix = nullptr;
            int codeBlocks = 0;
            ImFont* font = nullptr; // the caller's, before rich_md pushes its own
            float fontSize = 0.0f;
        };
        Scope gScope;
        bool gEmojiMerged = false; // into rich_md's faces, once per context

        // one read-only editor per code block, for colours and selection; dropped once its
        // block has not been drawn for a while
        struct CodeView {
            std::unique_ptr<TextEditor> editor;
            std::string text;
            std::string language;
            int lastFrame = 0;
        };
        std::unordered_map<std::string, CodeView>& codeViews() {
            static std::unordered_map<std::string, CodeView> views;
            return views;
        }

        bool smallButton(const std::string& label, const char* id, const std::string& tooltip,
                         const ImVec4& color) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const bool clicked = ImGui::SmallButton(std::format("{}###{}", label, id).c_str());
            ImGui::PopStyleColor();
            if (!tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip.c_str());
            return clicked;
        }

        void renderCode(const std::string& rawCode, const std::string& language) {
            const Theme& t = theme();
            std::string code = rawCode;
            while (!code.empty() && code.back() == '\n')
                code.pop_back();
            const int frame = ImGui::GetFrameCount();
            auto& views = codeViews();
            if (frame % 600 == 0)
                std::erase_if(views,
                              [&](const auto& v) { return frame - v.second.lastFrame > 600; });
            // rich_md scopes the block's ids by its text, which changes while it streams:
            // key them by position instead, so the windows below stay the same
            const std::string key = std::format("{}_{}", gScope.idPrefix ? *gScope.idPrefix : "md",
                                                gScope.codeBlocks++);
            CodeView& view = views[key];
            // also when the language changes: a streamed fence's name arrives in pieces
            if (!view.editor || view.language != language) {
                if (!view.editor) {
                    view.editor = std::make_unique<TextEditor>();
                    view.editor->SetReadOnly(true);
                    view.editor->SetShowLineNumbers(false); // a snippet, not a file
                }
                view.language = language;
                view.editor->SetHighlighter(gOptions.highlighter ? gOptions.highlighter(language)
                                                                 : TextEditor::Highlighter());
            }
            // the frame's own colour behind the text: one box, not two
            auto palette = TextEditor::FromTheme(t);
            palette.background = ImGui::ColorConvertFloat4ToU32(t.sidebar);
            view.editor->SetPalette(palette);
            if (view.text != code) { // streaming: the block grows
                view.text = code;
                view.editor->SetText(code);
            }
            view.lastFrame = frame;

            ImGui::PushOverrideID(ImHashStr(key.c_str()));
            ImGui::PushFont(gScope.font, gScope.fontSize); // the caller's: its icons too
            ImGui::PushStyleColor(ImGuiCol_ChildBg, t.sidebar);
            ImGui::PushStyleColor(ImGuiCol_Border, t.border);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Radius::Md);
            if (ImGui::BeginChild("##md_code", ImVec2(-1, 0),
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
                const ImVec2 topRight(ImGui::GetCursorScreenPos().x +
                                          ImGui::GetContentRegionAvail().x,
                                      ImGui::GetCursorScreenPos().y);
                const int lines = static_cast<int>(std::ranges::count(code, '\n')) + 1;
                view.editor->Render(
                    "##md_editor",
                    // the editor's margins, and room for a sideways scrollbar
                    ImVec2(-1, static_cast<float>(lines) * ImGui::GetTextLineHeightWithSpacing() +
                                   6.0f + ImGui::GetStyle().ScrollbarSize));

                // the buttons float over the code, top right: a later sibling child draws on top
                static ImGuiID copied = 0;
                static double copiedAt = -10.0;
                const ImGuiID copyId = ImGui::GetID("copy");
                const bool done = copyId == copied && ImGui::GetTime() - copiedAt < 1.5;
                const std::string& copyLabel = done ? gOptions.copiedIcon : gOptions.copyIcon;
                const ImGuiStyle& style = ImGui::GetStyle();
                const auto buttonWidth = [&](const std::string& label) {
                    return ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2;
                };
                const auto applies = [&](const CodeAction& action) {
                    return action.run && (!action.applies || action.applies(language));
                };
                float width = buttonWidth(copyLabel) + 4;
                for (const CodeAction& action : gOptions.codeActions)
                    if (applies(action))
                        width += buttonWidth(action.icon) + style.ItemSpacing.x;
                ImGui::SetCursorScreenPos(ImVec2(topRight.x - width, topRight.y));
                if (ImGui::BeginChild("##md_code_actions", ImVec2(width, ImGui::GetFrameHeight()),
                                      ImGuiChildFlags_None,
                                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar)) {
                    for (size_t i = 0; i < gOptions.codeActions.size(); ++i) {
                        const CodeAction& action = gOptions.codeActions[i];
                        if (!applies(action))
                            continue;
                        if (smallButton(action.icon, std::format("action{}", i).c_str(),
                                        action.tooltip, t.mutedForeground))
                            action.run(code, language);
                        ImGui::SameLine();
                    }
                    if (smallButton(copyLabel, "copy", "", done ? t.success : t.mutedForeground)) {
                        ImGui::SetClipboardText(code.c_str());
                        copied = copyId;
                        copiedAt = ImGui::GetTime();
                    }
                }
                ImGui::EndChild();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            ImGui::PopFont();
            ImGui::PopID();
        }

        // rich_md asks for fonts/Roboto/Roboto-{Regular,Bold}[Italic].ttf and a code font
        const Font* fontFor(const std::string& path) {
            if (!path.starts_with("fonts/") || !path.ends_with(".ttf") ||
                path.starts_with("fonts/latex/"))
                return nullptr;
            const Font* font = &gOptions.code;
            if (path.starts_with("fonts/Roboto/")) {
                if (!gOptions.regular.data)
                    return nullptr;
                const bool bold = path.ends_with("-Bold.ttf") || path.ends_with("-BoldItalic.ttf");
                const bool italic = path.ends_with("Italic.ttf");
                font = bold ? (italic ? &gOptions.boldItalic : &gOptions.bold)
                            : (italic ? &gOptions.italic : &gOptions.regular);
                if (!font->data)
                    font = &gOptions.regular;
            }
            return font->data ? font : nullptr;
        }

        void ensureContext() {
            if (RichMd::GetCurrentContext())
                return;
            // once: Shutdown keeps the services, and wrapping ours again would chain them
            static bool hosted = false;
            if (!hosted) {
                hosted = true;
                RichMd::HostServices host = RichMd::GetHostServices();
                host.RenderCodeBlock = renderCode;
                host.ReadAsset =
                    [fallback = host.ReadAsset](const std::string& path) -> RichMd::AssetBytes {
                    if (const Font* font = fontFor(path)) {
                        const auto* bytes = static_cast<const uint8_t*>(font->data);
                        return std::vector<uint8_t>(bytes, bytes + font->size);
                    }
                    return fallback ? fallback(path) : RichMd::ReadAssetDefault(path);
                };
                RichMd::SetHostServices(host);
            }
            RichMd::MarkdownOptions options;
            options.hardSoftBreaks = gOptions.hardBreaks;
            RichMd::CreateContext(options);
        }
    } // namespace

    void Configure(Options options) {
        gOptions = std::move(options);
        codeViews().clear(); // their highlighters came from the old options
    }

    void Render(const std::string& markdown, const std::string& idPrefix) {
        ensureContext();
        RichMd::GetStyle().codeColor = theme().syntax.number; // inline code
        const Scope saved = gScope;
        gScope = {.idPrefix = &idPrefix,
                  .font = ImGui::GetFont(),
                  // unscaled, as PushFont takes it
                  .fontSize = ImGui::GetCurrentContext()->FontSizeBase};
        ImGui::PushID(idPrefix.c_str());
        RichMd::RenderRaw(markdown);
        ImGui::PopID();
        gScope = saved;
        // colour emoji in the faces rich_md loaded (they exist once it has rendered), not in the
        // host's own fonts, which chose with AddFont's `emoji`
        if (!gEmojiMerged) {
            gEmojiMerged = true;
            MergeEmoji(RichMd::GetCodeFont().font);
            for (const bool bold : {false, true})
                for (const bool italic : {false, true})
                    MergeEmoji(RichMd::GetFont(RichMd::MarkdownFontSpec(italic, bold)).font);
        }
    }

    void Shutdown() {
        codeViews().clear();
        gEmojiMerged = false;
        if (RichMd::GetCurrentContext())
            RichMd::DestroyContext();
    }
} // namespace imshell::Markdown
