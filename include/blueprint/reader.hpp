// Reverse engineering: a Clang abstract syntax tree in JSON form becomes a
// UML model.
//
// The tool shells out to the clang binary with -Xclang -ast-dump=json and
// parses what comes back. It deliberately does not link libclang. The reason
// is in the report, and the short form is that a JSON pipe has no ABI, no
// version-matched headers and no build-time dependency at all, which is what
// lets the whole project build from a compiler and CMake alone.
#ifndef BLUEPRINT_READER_HPP
#define BLUEPRINT_READER_HPP

#include <string>
#include <vector>

#include "blueprint/json.hpp"
#include "blueprint/model.hpp"

namespace bp {

struct ReaderOptions {
    // Only declarations whose file lies under one of these prefixes enter the
    // model. Empty means every file that is not a replacement header.
    std::vector<std::string> roots;
    // Compiler-generated members (implicit copy constructors and the like)
    // are not part of the design and are dropped.
    bool skip_implicit = true;
    // Private members are part of the structure and are kept by default;
    // dropping them is a presentation choice, not a modelling one.
    bool skip_private = false;
    std::string model_name = "model";
};

// Turns a parsed Clang JSON dump into a model. Pure: no file system, no
// process launching, which is what makes it testable from a fixture.
Model read_ast_json(const Json& ast, const ReaderOptions& options);

// The relationship inference rule, exposed on its own because it is the part
// of the reader with the most cases and it deserves its own tests.
struct MemberClassification {
    bool is_relation = false;
    RelationKind kind = RelationKind::Association;
    std::string target;        // qualified name of the other classifier
    std::string multiplicity = "1";
};

// `known` decides whether a bare type name denotes a classifier of the model.
MemberClassification classify_member_type(
    const std::string& type,
    const std::vector<std::string>& known_qualified_names);

// ---------------------------------------------------------------------------
// Driving the compiler.
// ---------------------------------------------------------------------------
struct ClangInvocation {
    std::string clang = "clang++";
    std::string standard = "c++20";
    std::vector<std::string> include_dirs;
    std::vector<std::string> extra_flags;
    // Replacement declaration-only system headers, see stdstub/README.md.
    std::string stdstub_dir;
    std::string target;   // empty means the compiler's default target
};

std::string build_clang_command(const ClangInvocation& inv,
                                const std::string& source,
                                const std::string& json_out,
                                const std::string& diag_out);

// Runs the compiler and returns the parsed dump. Throws when the compiler is
// missing or reports an error. `raw_bytes`, when given, receives the size of
// the dump as the compiler wrote it, which is the figure the measurements
// need and which is lost once the text is parsed.
Json dump_ast(const ClangInvocation& inv, const std::string& source,
              std::size_t* raw_bytes = nullptr);

// True when the configured compiler can be executed at all.
bool clang_available(const ClangInvocation& inv);

} // namespace bp

#endif
