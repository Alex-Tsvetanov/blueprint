#include "testing.hpp"

#include <algorithm>

namespace bptest {

int run(int argc, char** argv) {
    std::vector<std::string> wanted(argv + 1, argv + argc);
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    for (const auto& c : registry()) {
        if (!wanted.empty() &&
            std::find(wanted.begin(), wanted.end(), c.suite) == wanted.end()) {
            continue;
        }
        try {
            c.body();
            std::cout << "  ok    " << c.suite << "." << c.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "  FAIL  " << c.suite << "." << c.name << "\n"
                      << "        " << e.what() << "\n";
            failures.push_back(c.suite + "." + c.name);
            ++failed;
        }
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    for (const auto& f : failures) std::cout << "  failed: " << f << "\n";
    return failed == 0 ? 0 : 1;
}

} // namespace bptest

int main(int argc, char** argv) { return bptest::run(argc, argv); }
