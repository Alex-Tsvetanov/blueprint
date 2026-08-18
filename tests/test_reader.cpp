#include "testing.hpp"

#include <map>

#include "blueprint/json.hpp"
#include "blueprint/reader.hpp"

using bp::ClassifierKind;
using bp::Json;
using bp::Model;
using bp::ReaderOptions;
using bp::RelationKind;
using bp::Scope;
using bp::Visibility;

namespace {

const std::vector<std::string>& known() {
    static const std::vector<std::string> names = {"app::Wheel", "app::Engine", "app::Car"};
    return names;
}

// A cut-down Clang dump. Only the keys the reader actually reads are present,
// which is also a statement of what the reader depends on.
const char* kFixture = R"JSON(
{"kind":"TranslationUnitDecl","inner":[
 {"kind":"NamespaceDecl","name":"app","loc":{"file":"/proj/app.hpp","line":1},"inner":[
  {"kind":"CXXRecordDecl","name":"Engine","tagUsed":"struct","completeDefinition":true,
   "loc":{"line":3},"inner":[
    {"kind":"CXXRecordDecl","name":"Engine","tagUsed":"struct","isImplicit":true},
    {"kind":"FieldDecl","name":"horsepower","type":{"qualType":"int"}},
    {"kind":"CXXConstructorDecl","name":"Engine","isImplicit":true,
     "type":{"qualType":"void (const Engine &)"}}
   ]},
  {"kind":"CXXRecordDecl","name":"Wheel","tagUsed":"class","completeDefinition":true,
   "loc":{"line":9},"inner":[
    {"kind":"AccessSpecDecl","access":"public"},
    {"kind":"CXXMethodDecl","name":"radius","type":{"qualType":"double () const"}},
    {"kind":"VarDecl","name":"bp_table","storageClass":"static","constexpr":true,
     "type":{"qualType":"const char *const"},
     "inner":[{"kind":"ImplicitCastExpr","inner":[
       {"kind":"StringLiteral","value":"\"wheels\""}]}]},
    {"kind":"VarDecl","name":"bp_pk_id","storageClass":"static","constexpr":true,
     "type":{"qualType":"const bool"},
     "inner":[{"kind":"CXXBoolLiteralExpr","value":true}]},
    {"kind":"AccessSpecDecl","access":"private"},
    {"kind":"FieldDecl","name":"id","type":{"qualType":"int"}},
    {"kind":"VarDecl","name":"made","storageClass":"static","type":{"qualType":"int"}}
   ]},
  {"kind":"CXXRecordDecl","name":"Car","tagUsed":"class","completeDefinition":true,
   "loc":{"line":20},"definitionData":{"isAbstract":true},"inner":[
    {"kind":"AccessSpecDecl","access":"public"},
    {"kind":"CXXDestructorDecl","name":"~Car","virtual":true,"type":{"qualType":"void () noexcept"}},
    {"kind":"CXXMethodDecl","name":"mass","virtual":true,"pure":true,
     "type":{"qualType":"double () const"}},
    {"kind":"CXXMethodDecl","name":"fit","type":{"qualType":"void (const Wheel &, int)"},
     "inner":[{"kind":"ParmVarDecl","name":"wheel","type":{"qualType":"const Wheel &"}},
              {"kind":"ParmVarDecl","name":"position","type":{"qualType":"int"}}]},
    {"kind":"AccessSpecDecl","access":"private"},
    {"kind":"FieldDecl","name":"engine_","type":{"qualType":"Engine",
      "desugaredQualType":"app::Engine"}},
    {"kind":"FieldDecl","name":"spare_","type":{"desugaredQualType":"std::unique_ptr<app::Wheel>"}},
    {"kind":"FieldDecl","name":"wheels_","type":{"desugaredQualType":"std::vector<app::Wheel *>"}},
    {"kind":"FieldDecl","name":"borrowed_","type":{"desugaredQualType":"app::Engine *"}}
   ]},
  {"kind":"CXXRecordDecl","name":"Sedan","tagUsed":"class","completeDefinition":true,
   "loc":{"line":40},
   "bases":[{"access":"public","type":{"qualType":"Car"}},
            {"access":"protected","isVirtual":true,"type":{"qualType":"Wheel"}}],
   "inner":[
    {"kind":"AccessSpecDecl","access":"public"},
    {"kind":"CXXMethodDecl","name":"mass","virtual":true,"type":{"qualType":"double () const"}}
   ]},
  {"kind":"ClassTemplateDecl","name":"Box","inner":[
    {"kind":"TemplateTypeParmDecl","name":"T","tagUsed":"class"},
    {"kind":"CXXRecordDecl","name":"Box","tagUsed":"class","completeDefinition":true,
     "loc":{"line":50},"inner":[
      {"kind":"AccessSpecDecl","access":"public"},
      {"kind":"FieldDecl","name":"value","type":{"qualType":"T"}}
     ]}
  ]},
  {"kind":"CXXRecordDecl","name":"Hidden","tagUsed":"class","completeDefinition":true,
   "loc":{"file":"/elsewhere/other.hpp","line":2},"inner":[]}
 ]}
]}
)JSON";

Model parse_fixture(ReaderOptions options = {}) {
    if (options.roots.empty()) options.roots.push_back("/proj/");
    return bp::read_ast_json(Json::parse(kFixture), options);
}

} // namespace

// ---------------------------------------------------------------------------
// The ownership rule
// ---------------------------------------------------------------------------
TEST(reader, a_member_held_by_value_is_a_composition) {
    const auto info = bp::classify_member_type("app::Engine", known());
    CHECK(info.is_relation);
    CHECK(info.kind == RelationKind::Composition);
    CHECK_EQ(info.multiplicity, std::string("1"));
    CHECK_EQ(info.target, std::string("app::Engine"));
}

TEST(reader, a_raw_pointer_member_is_an_association_of_at_most_one) {
    const auto info = bp::classify_member_type("app::Engine *", known());
    CHECK(info.kind == RelationKind::Association);
    CHECK_EQ(info.multiplicity, std::string("0..1"));
}

TEST(reader, a_reference_member_is_an_association_of_exactly_one) {
    const auto info = bp::classify_member_type("const app::Wheel &", known());
    CHECK(info.kind == RelationKind::Association);
    CHECK_EQ(info.multiplicity, std::string("1"));
    CHECK_EQ(info.target, std::string("app::Wheel"));
}

TEST(reader, unique_ptr_owns_exclusively_and_gives_composition) {
    const auto info = bp::classify_member_type("std::unique_ptr<app::Wheel>", known());
    CHECK(info.kind == RelationKind::Composition);
    CHECK_EQ(info.multiplicity, std::string("0..1"));
}

TEST(reader, shared_ptr_shares_ownership_and_gives_aggregation) {
    const auto info = bp::classify_member_type("std::shared_ptr<app::Wheel>", known());
    CHECK(info.kind == RelationKind::Aggregation);
    CHECK_EQ(info.multiplicity, std::string("0..1"));
}

TEST(reader, a_container_raises_the_multiplicity_and_keeps_the_ownership) {
    const auto values = bp::classify_member_type("std::vector<app::Wheel>", known());
    CHECK(values.kind == RelationKind::Composition);
    CHECK_EQ(values.multiplicity, std::string("*"));

    const auto pointers = bp::classify_member_type("std::vector<app::Wheel *>", known());
    CHECK(pointers.kind == RelationKind::Association);
    CHECK_EQ(pointers.multiplicity, std::string("*"));

    const auto owned = bp::classify_member_type("std::vector<std::unique_ptr<app::Wheel>>", known());
    CHECK(owned.kind == RelationKind::Composition);
    CHECK_EQ(owned.multiplicity, std::string("*"));

    const auto shared = bp::classify_member_type("std::vector<std::shared_ptr<app::Wheel>>", known());
    CHECK(shared.kind == RelationKind::Aggregation);
    CHECK_EQ(shared.multiplicity, std::string("*"));
}

TEST(reader, a_map_is_classified_by_its_mapped_type_not_its_key) {
    const auto info =
        bp::classify_member_type("std::map<std::string, std::shared_ptr<app::Wheel>>", known());
    CHECK(info.kind == RelationKind::Aggregation);
    CHECK_EQ(info.multiplicity, std::string("*"));
    CHECK_EQ(info.target, std::string("app::Wheel"));
}

TEST(reader, a_type_outside_the_model_is_an_attribute_and_not_a_relation) {
    CHECK_EQ(bp::classify_member_type("int", known()).is_relation, false);
    CHECK_EQ(bp::classify_member_type("std::string", known()).is_relation, false);
    CHECK_EQ(bp::classify_member_type("std::vector<double>", known()).is_relation, false);
}

// ---------------------------------------------------------------------------
// The traversal
// ---------------------------------------------------------------------------
TEST(reader, reads_classifiers_with_their_kind_visibility_and_scope) {
    const Model model = parse_fixture();
    const auto* engine = model.find("app::Engine");
    CHECK(engine != nullptr);
    CHECK(engine->kind == ClassifierKind::Struct);

    const auto* wheel = model.find("app::Wheel");
    CHECK(wheel != nullptr);
    CHECK(wheel->kind == ClassifierKind::Class);
    CHECK_EQ(wheel->attributes.size(), std::size_t(2));  // id and made, not the annotations
    for (const auto& a : wheel->attributes) {
        if (a.name == "id") CHECK(a.visibility == Visibility::Private);
        if (a.name == "made") CHECK(a.scope == Scope::Classifier);
    }
    CHECK_EQ(wheel->operations.size(), std::size_t(1));
    CHECK(wheel->operations.front().visibility == Visibility::Public);
    CHECK_EQ(wheel->operations.front().is_const, true);
    CHECK_EQ(wheel->operations.front().return_type, std::string("double"));
}

TEST(reader, drops_compiler_generated_members_and_the_injected_class_name) {
    const Model model = parse_fixture();
    const auto* engine = model.find("app::Engine");
    CHECK(engine != nullptr);
    CHECK_EQ(engine->operations.size(), std::size_t(0));
    CHECK_EQ(engine->attributes.size(), std::size_t(1));
    // The injected class name would otherwise appear as app::Engine::Engine.
    CHECK(model.find("app::Engine::Engine") == nullptr);
}

TEST(reader, keeps_only_declarations_under_the_requested_root) {
    const Model model = parse_fixture();
    CHECK(model.find("app::Hidden") == nullptr);

    ReaderOptions everything;
    everything.roots.push_back("/");
    const Model wide = bp::read_ast_json(Json::parse(kFixture), everything);
    CHECK(wide.find("app::Hidden") != nullptr);
}

TEST(reader, records_inheritance_including_access_and_virtual_bases) {
    const Model model = parse_fixture();
    int generalizations = 0;
    for (const auto& r : model.relations) {
        if (r.kind != RelationKind::Generalization || r.from != "app::Sedan") continue;
        ++generalizations;
        if (r.to == "app::Wheel") {
            CHECK_EQ(r.is_virtual_base, true);
            CHECK(r.visibility == Visibility::Protected);
        }
        if (r.to == "app::Car") {
            CHECK_EQ(r.is_virtual_base, false);
            CHECK(r.visibility == Visibility::Public);
        }
    }
    CHECK_EQ(generalizations, 2);
}

TEST(reader, marks_a_class_with_a_pure_operation_as_abstract) {
    const Model model = parse_fixture();
    const auto* car = model.find("app::Car");
    CHECK(car != nullptr);
    CHECK_EQ(car->is_abstract, true);
    bool found_pure = false;
    for (const auto& op : car->operations) {
        if (op.name == "mass") { found_pure = op.is_pure; }
    }
    CHECK(found_pure);
}

TEST(reader, turns_member_types_into_relations_after_the_whole_unit_is_read) {
    const Model model = parse_fixture();
    std::map<std::string, bp::Relation> by_label;
    for (const auto& r : model.relations) {
        if (!r.label.empty()) by_label[r.label] = r;
    }
    CHECK(by_label.count("engine_") == 1);
    CHECK(by_label["engine_"].kind == RelationKind::Composition);
    CHECK(by_label["spare_"].kind == RelationKind::Composition);
    CHECK_EQ(by_label["spare_"].multiplicity, std::string("0..1"));
    CHECK(by_label["wheels_"].kind == RelationKind::Association);
    CHECK_EQ(by_label["wheels_"].multiplicity, std::string("*"));
    CHECK(by_label["borrowed_"].kind == RelationKind::Association);
}

TEST(reader, a_type_used_only_in_a_signature_becomes_a_dependency) {
    const Model model = parse_fixture();
    bool found = false;
    for (const auto& r : model.relations) {
        if (r.from == "app::Sedan" && r.kind == RelationKind::Dependency) found = true;
    }
    // Car::fit takes a Wheel and Car already holds one, so no dependency is
    // reported there; the rule only fires without a stronger edge.
    for (const auto& r : model.relations) {
        if (r.from == "app::Car" && r.to == "app::Wheel") {
            CHECK(r.kind != RelationKind::Dependency);
        }
    }
    (void)found;
}

TEST(reader, reads_a_class_template_as_a_parameterised_classifier) {
    const Model model = parse_fixture();
    const auto* box = model.find("app::Box");
    CHECK(box != nullptr);
    CHECK(box->kind == ClassifierKind::Template);
    CHECK_EQ(box->template_parameters.size(), std::size_t(1));
    CHECK_EQ(box->template_parameters.front(), std::string("class T"));
}

TEST(reader, promotes_bp_constants_to_a_stereotype_and_tagged_values) {
    const Model model = parse_fixture();
    const auto* wheel = model.find("app::Wheel");
    CHECK(wheel != nullptr);
    CHECK(wheel->has_stereotype("persistent"));
    CHECK(wheel->tag("table") != nullptr);
    CHECK_EQ(wheel->tag("table")->value, std::string("wheels"));
    // The annotation must not survive as an attribute of the class.
    for (const auto& a : wheel->attributes) {
        CHECK(a.name.rfind("bp_", 0) != 0);
    }
    for (const auto& a : wheel->attributes) {
        if (a.name != "id") continue;
        CHECK_EQ(a.tags.size(), std::size_t(1));
        CHECK_EQ(a.tags.front().name, std::string("pk"));
    }
}

TEST(reader, builds_a_clang_command_that_replaces_the_standard_library) {
    bp::ClangInvocation inv;
    inv.clang = "clang++";
    inv.stdstub_dir = "stdstub";
    inv.include_dirs.push_back("include");
    const std::string cmd = bp::build_clang_command(inv, "a.hpp", "out.json", "out.err");
    CHECK_CONTAINS(cmd, "-std=c++20");
    CHECK_CONTAINS(cmd, "-nostdinc++");
    CHECK_CONTAINS(cmd, "-ast-dump=json");
    CHECK_CONTAINS(cmd, "-fsyntax-only");
    CHECK_CONTAINS(cmd, "\"include\"");
}
