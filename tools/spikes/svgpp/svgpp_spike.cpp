#include "../../../src/assets/svg_importer.hpp"

#include <cstddef>
#include <iostream>
#include <iterator>

namespace {

struct CorpusCase {
    const char* name;
    const char* path_data;
    bool expect_success;
};

constexpr CorpusCase kCorpus[] = {
    {"moveto-line-close", "M 0 0 L 100 0 L 100 100 Z", true},
    {"horizontal-vertical-relative", "M 10 10 l 5 0 v 5 h -5 z", true},
    {"implicit-moveto-lines", "M0 0 10 0 10 10", true},
    {"cubic-smooth", "M0 0 C10 0 20 10 30 30 S50 50 60 60", true},
    {"quadratic-smooth", "M0 0 Q10 0 20 20 T40 40", true},
    {"compact-scientific", "M1e1-2e1L3.5e+1,4.25e-1", true},
    {"arc-absolute", "M0 0 A10 20 30 0 1 40 50", true},
    {"arc-relative", "M10 10 a5 5 0 1 0 20 20", true},
    {"mixed-repeated-curves", "m0,0 c1,2 3,4 5,6 7,8 9,10 11,12", true},
    {"malformed-number", "M 0 nope", false},
};

} // namespace

int main() {
    chronon3d::assets::SvgImporter importer;
    std::size_t passed = 0;
    std::size_t expected = 0;
    std::size_t arc_cases = 0;

    std::cout << "SVGPP_SPIKE mode=canonical-svg-importer\n";
    for (const auto& test : kCorpus) {
        const auto result = importer.import_path_data(test.path_data);
        const bool case_ok = result.ok == test.expect_success;
        passed += case_ok;
        expected += result.ok == test.expect_success;
        if (test.name[0] == 'a') ++arc_cases;
        std::cout << "case=" << test.name
                  << " result=" << (result.ok ? "PASS" : "FAIL")
                  << " commands=" << result.path.commands.size()
                  << " contract=" << (case_ok ? "PASS" : "FAIL") << '\n';
        if (!result.ok && test.expect_success) {
            std::cout << "error=" << result.error << '\n';
        }
    }

    std::cout << "corpus_cases=" << std::size(kCorpus) << '\n';
    std::cout << "importer_pass=" << passed << '/' << std::size(kCorpus) << '\n';
    std::cout << "expected_behavior=" << expected << '/' << std::size(kCorpus) << '\n';
    std::cout << "arc_cases_exercised=" << arc_cases << '\n';
    std::cout << "result=" << (expected == std::size(kCorpus) ? "PASS" : "FAIL") << '\n';
    return expected == std::size(kCorpus) ? 0 : 1;
}
