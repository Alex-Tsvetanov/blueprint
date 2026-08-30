// Live extraction through the clang++ binary. These cases need the front end
// on PATH. When it is missing they SKIP with a stated reason; they never pass
// quietly and they never fail for absence of the binary.
#include "testing.hpp"

#include <string>

#include "blueprint/reader.hpp"

#ifndef BLUEPRINT_SOURCE_DIR
#error "BLUEPRINT_SOURCE_DIR must be set by CMake; live extraction needs the tree"
#endif

using bp::Model;
using bp::ReaderOptions;
using bp::RelationKind;

namespace {

const std::string& source_dir() {
    static const std::string dir = BLUEPRINT_SOURCE_DIR;
    return dir;
}

bp::ClangInvocation invocation() {
    bp::ClangInvocation inv;
    inv.clang = "clang++";
    inv.stdstub_dir = source_dir() + "/stdstub";
    inv.include_dirs.push_back(source_dir() + "/include");
    return inv;
}

void require_clang() {
    if (!bp::clang_available(invocation())) {
        SKIP("clang++ is not available on PATH; live extraction cannot run");
    }
}

Model extract_library() {
    require_clang();
    const std::string source = source_dir() + "/examples/library.hpp";
    const bp::Json ast = bp::dump_ast(invocation(), source);
    ReaderOptions options;
    options.roots.push_back(source_dir() + "/examples");
    options.model_name = "library";
    return bp::read_ast_json(ast, options);
}

} // namespace

TEST(extract, clang_is_reported_available_when_present) {
    // Distinct from the SKIP path: when clang++ is here, availability is true.
    // When it is not, this case skips rather than asserting false, so a missing
    // front end cannot be mistaken for a green suite.
    require_clang();
    CHECK(bp::clang_available(invocation()));
}

TEST(extract, library_example_yields_the_annotated_classifiers) {
    const Model model = extract_library();
    CHECK(model.find("library::Author") != nullptr);
    CHECK(model.find("library::Shelf") != nullptr);
    CHECK(model.find("library::LibraryItem") != nullptr);
    CHECK(model.find("library::Book") != nullptr);
    CHECK(model.find("library::Journal") != nullptr);
    CHECK(model.find("library::Catalogue") != nullptr);

    const auto* item = model.find("library::LibraryItem");
    CHECK(item->is_abstract);
    CHECK(item->has_stereotype("persistent"));
    CHECK(item->tag("table") != nullptr);
    CHECK_EQ(item->tag("table")->value, std::string("items"));
    CHECK(item->tag("inheritance") != nullptr);
    CHECK_EQ(item->tag("inheritance")->value, std::string("single-table"));
}

TEST(extract, library_example_infers_ownership_from_member_spellings) {
    const Model model = extract_library();
    const auto* catalogue = model.find("library::Catalogue");
    CHECK(catalogue != nullptr);

    bool saw_items = false;
    bool saw_default_shelf = false;
    bool saw_overflow = false;
    bool saw_curator = false;
    for (const auto& r : model.relations) {
        if (r.from != "library::Catalogue") continue;
        if (r.label == "items") {
            saw_items = true;
            CHECK(r.kind == RelationKind::Composition);
            CHECK_EQ(r.multiplicity, std::string("*"));
            CHECK_EQ(r.to, std::string("library::LibraryItem"));
        } else if (r.label == "default_shelf") {
            saw_default_shelf = true;
            CHECK(r.kind == RelationKind::Aggregation);
            CHECK_EQ(r.multiplicity, std::string("0..1"));
            CHECK_EQ(r.to, std::string("library::Shelf"));
        } else if (r.label == "overflow") {
            saw_overflow = true;
            CHECK(r.kind == RelationKind::Association);
            CHECK_EQ(r.multiplicity, std::string("0..1"));
            CHECK_EQ(r.to, std::string("library::Shelf"));
        } else if (r.label == "curator") {
            saw_curator = true;
            CHECK(r.kind == RelationKind::Composition);
            CHECK_EQ(r.multiplicity, std::string("1"));
            CHECK_EQ(r.to, std::string("library::Author"));
        }
    }
    CHECK(saw_items);
    CHECK(saw_default_shelf);
    CHECK(saw_overflow);
    CHECK(saw_curator);
}

TEST(extract, library_example_records_generalization_of_the_hierarchy) {
    const Model model = extract_library();
    bool book_extends = false;
    bool journal_extends = false;
    for (const auto& r : model.relations) {
        if (r.kind != RelationKind::Generalization) continue;
        if (r.from == "library::Book" && r.to == "library::LibraryItem") book_extends = true;
        if (r.from == "library::Journal" && r.to == "library::LibraryItem") journal_extends = true;
    }
    CHECK(book_extends);
    CHECK(journal_extends);
}
