// A test runner in one header. No third-party test framework is used, so the
// project builds with a C++20 compiler and CMake and nothing else.
//
// A case is declared with TEST(suite, name) and registers itself at static
// initialisation. The binary runs every case, or only the cases of the suites
// named on the command line, which is how CTest addresses one suite at a time.
#ifndef BLUEPRINT_TESTING_HPP
#define BLUEPRINT_TESTING_HPP

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace bptest {

struct Case {
    std::string suite;
    std::string name;
    std::function<void()> body;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> body) {
        registry().push_back(Case{suite, name, std::move(body)});
    }
};

class Failure : public std::exception {
public:
    explicit Failure(std::string what) : what_(std::move(what)) {}
    const char* what() const noexcept override { return what_.c_str(); }

private:
    std::string what_;
};

// Values are printed on failure. Anything streamable prints itself; the two
// overloads below cover what the tests actually compare.
template <typename T>
std::string show(const T& v) {
    if constexpr (std::is_convertible_v<T, std::string>) {
        return "\"" + std::string(v) + "\"";
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else {
        return std::to_string(v);
    }
}

int run(int argc, char** argv);

} // namespace bptest

#define BP_CONCAT_INNER(a, b) a##b
#define BP_CONCAT(a, b) BP_CONCAT_INNER(a, b)

#define TEST(suite, name)                                                            \
    static void BP_CONCAT(bp_test_body_, __LINE__)();                                \
    static ::bptest::Registrar BP_CONCAT(bp_test_reg_, __LINE__)(                     \
        #suite, #name, &BP_CONCAT(bp_test_body_, __LINE__));                          \
    static void BP_CONCAT(bp_test_body_, __LINE__)()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            throw ::bptest::Failure(std::string(__FILE__) + ":" +                     \
                                    std::to_string(__LINE__) + ": CHECK(" #cond ")"); \
        }                                                                            \
    } while (false)

#define CHECK_EQ(a, b)                                                               \
    do {                                                                             \
        const auto& bp_lhs = (a);                                                    \
        const auto& bp_rhs = (b);                                                    \
        if (!(bp_lhs == bp_rhs)) {                                                   \
            throw ::bptest::Failure(std::string(__FILE__) + ":" +                     \
                                    std::to_string(__LINE__) + ": CHECK_EQ(" #a ", " #b ")\n" \
                                    "      left  = " + ::bptest::show(bp_lhs) + "\n" \
                                    "      right = " + ::bptest::show(bp_rhs));      \
        }                                                                            \
    } while (false)

#define CHECK_CONTAINS(haystack, needle)                                             \
    do {                                                                             \
        const std::string bp_h = (haystack);                                         \
        const std::string bp_n = (needle);                                           \
        if (bp_h.find(bp_n) == std::string::npos) {                                  \
            throw ::bptest::Failure(std::string(__FILE__) + ":" +                     \
                                    std::to_string(__LINE__) +                        \
                                    ": expected to contain \"" + bp_n + "\"\n--- actual ---\n" + bp_h); \
        }                                                                            \
    } while (false)

#define CHECK_THROWS(expr)                                                           \
    do {                                                                             \
        bool bp_threw = false;                                                       \
        try { (void)(expr); } catch (const std::exception&) { bp_threw = true; }     \
        if (!bp_threw) {                                                             \
            throw ::bptest::Failure(std::string(__FILE__) + ":" +                     \
                                    std::to_string(__LINE__) +                        \
                                    ": expected " #expr " to throw");                \
        }                                                                            \
    } while (false)

#endif
