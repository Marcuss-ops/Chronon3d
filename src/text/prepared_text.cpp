// SPDX-License-Identifier: MIT

#include <chronon3d/text/prepared_text.hpp>

namespace chronon3d {

PreparedText prepare_text(const TextDefinition& def) {
    PreparedText pt;
    pt.document = to_text_document(def);
    pt.spans = def.spans;
    pt.style = def.style;
    pt.frame = def.frame;
    pt.frame.paragraph = def.paragraph;
    pt.document.defaults.layout.paragraph = def.paragraph;
    pt.document.paragraphs.clear();
    if (!pt.document.utf8.empty()) {
        pt.document.split_paragraphs(def.paragraph);
    }
    pt.shaping = TextShapingOptions{
        .direction = def.animation.direction,
        .language = def.animation.language,
        .script = def.animation.script,
        .open_type_features = {},
    };
    pt.animation = def.animation;
    return pt;
}

} // namespace chronon3d
