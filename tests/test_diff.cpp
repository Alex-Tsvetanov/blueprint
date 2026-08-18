#include "testing.hpp"

#include "blueprint/diff.hpp"

using namespace bp;

namespace {

Model base_model() {
    Model model;
    Classifier engine;
    engine.name = "Engine";
    engine.qualified_name = "app::Engine";
    engine.attributes.push_back(
        Attribute{"hp", "int", Visibility::Private, Scope::Instance, "1", {}});
    model.classifiers.push_back(engine);

    Classifier car;
    car.name = "Car";
    car.qualified_name = "app::Car";
    car.attributes.push_back(
        Attribute{"engine_", "app::Engine", Visibility::Private, Scope::Instance, "1", {}});
    Operation mass;
    mass.name = "mass";
    mass.return_type = "double";
    mass.is_const = true;
    car.operations.push_back(mass);
    model.classifiers.push_back(car);

    Relation r;
    r.from = "app::Car";
    r.to = "app::Engine";
    r.kind = RelationKind::Composition;
    r.label = "engine_";
    model.relations.push_back(r);

    model.normalize();
    return model;
}

bool has(const DiffReport& report, DiffKind kind, const std::string& element) {
    for (const auto& d : report.differences) {
        if (d.kind == kind && d.element == element) return true;
    }
    return false;
}

} // namespace

TEST(diff, two_identical_models_produce_no_differences) {
    const DiffReport report = compare(base_model(), base_model());
    CHECK(report.consistent());
    CHECK_EQ(report.differences.size(), std::size_t(0));
    CHECK_CONTAINS(report.text(), "agree");
}

TEST(diff, an_attribute_added_to_the_code_is_reported_as_added) {
    Model code = base_model();
    code.find("app::Car")->attributes.push_back(
        Attribute{"colour", "std::string", Visibility::Public, Scope::Instance, "1", {}});
    code.normalize();
    const DiffReport report = compare(code, base_model());
    CHECK_EQ(report.differences.size(), std::size_t(1));
    CHECK(has(report, DiffKind::MemberAdded, "app::Car.colour"));
}

TEST(diff, an_operation_removed_from_the_code_is_reported_as_removed) {
    Model code = base_model();
    code.find("app::Car")->operations.clear();
    const DiffReport report = compare(code, base_model());
    CHECK(has(report, DiffKind::MemberRemoved, "app::Car.mass() const"));
}

TEST(diff, a_change_of_visibility_is_a_change_and_not_a_pair_of_add_and_remove) {
    Model code = base_model();
    code.find("app::Car")->attributes.front().visibility = Visibility::Public;
    const DiffReport report = compare(code, base_model());
    CHECK_EQ(report.differences.size(), std::size_t(1));
    CHECK(has(report, DiffKind::MemberChanged, "app::Car.engine_"));
    CHECK_CONTAINS(report.text(), "public");
    CHECK_CONTAINS(report.text(), "private");
}

TEST(diff, a_class_present_only_in_the_diagram_is_reported_as_removed_from_the_code) {
    Model design = base_model();
    Classifier ghost;
    ghost.name = "Ghost";
    ghost.qualified_name = "app::Ghost";
    design.classifiers.push_back(ghost);
    design.normalize();
    const DiffReport report = compare(base_model(), design);
    CHECK(has(report, DiffKind::ClassRemoved, "app::Ghost"));
    CHECK_CONTAINS(report.text(), "class-removed");
}

TEST(diff, a_base_class_removed_from_the_code_is_reported_on_the_relation) {
    Model design = base_model();
    Relation inherit;
    inherit.from = "app::Car";
    inherit.to = "app::Engine";
    inherit.kind = RelationKind::Generalization;
    inherit.multiplicity = "";
    design.relations.push_back(inherit);
    design.normalize();
    const DiffReport report = compare(base_model(), design);
    CHECK(has(report, DiffKind::RelationRemoved, "app::Car -> app::Engine"));
}

TEST(diff, a_change_of_multiplicity_keeps_the_relation_and_reports_the_change) {
    Model code = base_model();
    code.relations.front().multiplicity = "*";
    const DiffReport report = compare(code, base_model());
    CHECK_EQ(report.differences.size(), std::size_t(1));
    CHECK(has(report, DiffKind::RelationChanged, "app::Car -> app::Engine (engine_)"));
    CHECK_CONTAINS(report.text(), "[*]");
}

TEST(diff, a_change_of_relationship_kind_is_reported_as_a_change) {
    Model code = base_model();
    code.relations.front().kind = RelationKind::Aggregation;
    const DiffReport report = compare(code, base_model());
    CHECK(has(report, DiffKind::RelationChanged, "app::Car -> app::Engine (engine_)"));
    CHECK_CONTAINS(report.text(), "aggregation");
    CHECK_CONTAINS(report.text(), "composition");
}

TEST(diff, a_class_becoming_abstract_is_reported_without_touching_its_members) {
    Model code = base_model();
    code.find("app::Car")->is_abstract = true;
    const DiffReport report = compare(code, base_model());
    CHECK_EQ(report.differences.size(), std::size_t(1));
    CHECK(has(report, DiffKind::ClassChanged, "app::Car"));
}
