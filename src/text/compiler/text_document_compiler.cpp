// SPDX-License-Identifier: MIT

#include "../text_document_compile_result.hpp"

#include <chronon3d/text/text_resolver.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace chronon3d::text_internal {
namespace {

void apply_spacing_collapse(
    TextDocumentCompileResult& compile_result,
    const ResolvedTextTree& tree,
    const TextLayoutSpec& layout
) {
    if (compile_result.paragraphs.size() < 2 || tree.paragraphs.size() < 2) {
        return;
    }

    for (std::size_t i = 1; i < compile_result.paragraphs.size(); ++i) {
        auto& previous = compile_result.paragraphs[i - 1];
        auto& current = compile_result.paragraphs[i];

        if (!previous.result || !current.result) continue;
        if (previous.source_index >= tree.paragraphs.size()) continue;
        if (current.source_index >= tree.paragraphs.size()) continue;

        const auto& previous_paragraph = tree.paragraphs[previous.source_index];
        const auto& current_paragraph = tree.paragraphs[current.source_index];

        const ParagraphStyle previous_style = previous_paragraph.style == ParagraphStyle{}
            ? layout.paragraph
            : previous_paragraph.style;
        const ParagraphStyle current_style = current_paragraph.style == ParagraphStyle{}
            ? layout.paragraph
            : current_paragraph.style;

        const float previous_after = previous_style.space_after;
        const float current_before = current_style.space_before;
        if (previous_after <= 0.0f && current_before <= 0.0f) continue;

        float merged_gap = current_before;
        switch (previous_style.spacing_collapse) {
        case ParagraphSpacingCollapse::Add:
            merged_gap = previous_after + current_before;
            break;
        case ParagraphSpacingCollapse::Maximum:
            merged_gap = std::max(previous_after, current_before);
            break;
        }

        if (previous_after > 0.0f) {
            previous.result.value()->bounds.y -= previous_after;
        }
        const float delta = merged_gap - current_before;
        if (delta != 0.0f) {
            current.result.value()->bounds.y += delta;
        }
    }
}

} // namespace

TextDocumentCompileResult compile_text_document(
    const TextDocument& doc,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    TextLayoutCache* cache
) {
    TextDocumentCompileResult result;
    result.complete = true;

    if (doc.utf8.empty() && doc.paragraphs.empty()) {
        return result;
    }

    auto tree = resolve_text_run_tree(doc, engine);
    TextCompileServices services{&engine, cache};
    const FontSpec primary_font = doc.defaults.font;

    for (std::size_t index = 0; index < tree.paragraphs.size(); ++index) {
        const auto& runs = tree.paragraphs[index].runs;
        if (runs.size() > 1) {
            const FontIdentity base_identity = font_identity_of(runs.front().font);
            const bool divergent = std::any_of(
                runs.begin() + 1,
                runs.end(),
                [&base_identity](const ResolvedTextRun& run) {
                    return font_identity_of(run.font) != base_identity;
                });
            if (divergent) {
                result.paragraphs.push_back(CompiledParagraphResult{
                    index,
                    TextLayoutError{
                        TextLayoutErrorKind::UnsupportedMultiFontRun,
                        "compile_text_document: paragraph " + std::to_string(index)
                            + " is multi-font (span font differs from run 0)"
                    }
                });
                result.complete = false;
                continue;
            }
        }

        TextLayoutRequest request{
            &doc,
            &layout,
            primary_font,
            index,
        };
        auto layout_result = compile_text_layout(request, services, &tree);
        if (!layout_result) {
            result.complete = false;
        }
        result.paragraphs.push_back(CompiledParagraphResult{
            index,
            std::move(layout_result)
        });
    }

    apply_spacing_collapse(result, tree, layout);
    return result;
}

} // namespace chronon3d::text_internal
