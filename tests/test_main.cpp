#include "testing.hpp"

#include <algorithm>

namespace bptest {

int run(int argc, char** argv) {
    std::vector<std::string> wanted(argv + 1, argv + argc);
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    std::vector<std::string> failures;
    std::vector<std::string> skips;

    for (const auto& c : registry()) {
        if (!wanted.empty() &&
            std::find(wanted.begin(), wanted.end(), c.suite) == wanted.end()) {
            continue;
        }
        try {
            c.body();
            std::cout << "  ok    " << c.suite << "." << c.name << "\n";
            ++passed;
        } catch (const Skip& e) {
            // Printed, counted, and (when nothing else ran) turned into the
            // CTest skip exit code. Never treated as a pass.
            std::cout << "  SKIP  " << c.suite << "." << c.name << "\n"
                      << "        " << e.what() << "\n";
            skips.push_back(c.suite + "." + c.name);
            ++skipped;
        } catch (const std::exception& e) {
            std::cout << "  FAIL  " << c.suite << "." << c.name << "\n"
                      << "        " << e.what() << "\n";
            failures.push_back(c.suite + "." + c.name);
            ++failed;
        }
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << skipped << " skipped\n";
    for (const auto& f : failures) std::cout << "  failed: " << f << "\n";
    for (const auto& s : skips) std::cout << "  skipped: " << s << "\n";
    if (failed != 0) return 1;
    if (skipped != 0 && passed == 0) return kSkipExit;
    return 0;
}

} // namespace bptest

int main(int argc, char** argv) { return bptest::run(argc, argv); }
