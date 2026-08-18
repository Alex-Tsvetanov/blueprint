#include "testing.hpp"

#include "blueprint/diagram.hpp"
#include "blueprint/diff.hpp"
#include "blueprint/emit.hpp"
#include "blueprint/model.hpp"

using namespace bp;

namespace {

Model round_trip_model() {
    Model model;
    model.name = "trip";

    Classifier shape;
    shape.name = "Shape";
    shape.qualified_name = "geo::Shape";
    shape.kind = ClassifierKind::Interface;
    shape.is_abstract = true;
    Operation area;
    area.name = "area";
    area.return_type = "double";
    area.is_virtual = true;
    area.is_pure = true;
    area.is_const = true;
    shape.operations.push_back(area);
    model.classifiers.push_back(shape);

    Classifier point;
    point.name = "Point";
    point.qualified_name = "geo::Point";
    point.kind = ClassifierKind::Struct;
    point.attributes.push_back(Attribute{"x", "double", Visibility::Public, Scope::Instance, "1", {}});
    point.attributes.push_back(Attribute{"y", "double", Visibility::Public, Scope::Instance, "1", {}});
    model.classifiers.push_back(point);

    Classifier polygon;
    polygon.name = "Polygon";
    polygon.qualified_name = "geo::Polygon";
    polygon.tags.push_back(TaggedValue{"table", "polygons"});
    polygon.stereotypes.push_back("persistent");
    polygon.attributes.push_back(
        Attribute{"vertices_", "std::vector<geo::Point>", Visibility::Private, Scope::Instance, "*", {}});
    polygon.attributes.push_back(
        Attribute{"count_", "int", Visibility::Protected, Scope::Classifier, "1", {}});
    Operation area_impl;
    area_impl.name = "area";
    area_impl.return_type = "double";
    area_impl.is_virtual = true;
    area_impl.is_const = true;
    polygon.operations.push_back(area_impl);
    Operation add;
    add.name = "add";
    add.return_type = "void";
    add.parameters.push_back(Parameter{"p", "const geo::Point &"});
    polygon.operations.push_back(add);
    model.classifiers.push_back(polygon);

    Classifier box;
    box.name = "Box";
    box.qualified_name = "geo::Box";
    box.kind = ClassifierKind::Template;
    box.template_parameters.push_back("class T");
    model.classifiers.push_back(box);

    Relation inherit;
    inherit.from = "geo::Polygon";
    inherit.to = "geo::Shape";
    inherit.kind = RelationKind::Generalization;
    inherit.multiplicity = "";
    model.relations.push_back(inherit);

    Relation holds;
    holds.from = "geo::Polygon";
    holds.to = "geo::Point";
    holds.kind = RelationKind::Composition;
    holds.label = "vertices_";
    holds.multiplicity = "*";
    model.relations.push_back(holds);

    model.normalize();
    return model;
}

} // namespace

TEST(diagram, the_notation_is_detected_from_the_text) {
    CHECK(detect_notation("@startuml\n@enduml\n") == Notation::PlantUml);
    CHECK(detect_notation("classDiagram\n  class A[\"A\"] {\n  }\n") == Notation::Mermaid);
}

TEST(diagram, a_model_survives_the_trip_through_plantuml) {
    const Model original = round_trip_model();
    const Model returned =
        read_class_diagram(emit_class_diagram(original, Notation::PlantUml), Notation::PlantUml);
    const DiffReport report = compare(original, returned);
    CHECK_CONTAINS(report.text(), "agree");
    CHECK(report.consistent());
}

TEST(diagram, the_trip_through_mermaid_loses_exactly_one_thing_and_it_is_known) {
    // Mermaid marks an operation as abstract or as static and has nothing for
    // an operation that is virtual but overridable, so that marker does not
    // survive. The test states the loss rather than hiding it: an unmeasured
    // loss is the failure mode the whole project exists to remove.
    const Model original = round_trip_model();
    const Model returned =
        read_class_diagram(emit_class_diagram(original, Notation::Mermaid), Notation::Mermaid);
    const DiffReport report = compare(original, returned);
    CHECK_EQ(report.differences.size(), std::size_t(1));
    CHECK(report.differences.front().kind == DiffKind::MemberChanged);
    CHECK_EQ(report.differences.front().element, std::string("geo::Polygon.area() const"));
    CHECK_CONTAINS(report.differences.front().detail, "{virtual}");
}

TEST(diagram, the_template_parameters_and_tagged_values_come_back_from_the_notes) {
    const Model original = round_trip_model();
    const Model returned = read_class_diagram(emit_class_diagram(original, Notation::PlantUml));
    const auto* box = returned.find("geo::Box");
    CHECK(box != nullptr);
    CHECK_EQ(box->template_parameters.size(), std::size_t(1));
    CHECK_EQ(box->template_parameters.front(), std::string("class T"));
    const auto* polygon = returned.find("geo::Polygon");
    CHECK(polygon != nullptr);
    CHECK(polygon->tag("table") != nullptr);
    CHECK_EQ(polygon->tag("table")->value, std::string("polygons"));
}

TEST(diagram, reading_a_diagram_recovers_visibility_scope_and_multiplicity) {
    const Model returned = read_class_diagram(
        "@startuml\n"
        "class \"app::Car\" as app__Car {\n"
        "  - engine_ : app::Engine\n"
        "  # {static} built_ : int [*]\n"
        "  + {abstract} mass() const : double\n"
        "}\n"
        "class \"app::Engine\" as app__Engine {\n"
        "}\n"
        "app__Car o-- \"0..1\" app__Engine : engine_\n"
        "@enduml\n");
    const auto* car = returned.find("app::Car");
    CHECK(car != nullptr);
    CHECK_EQ(car->attributes.size(), std::size_t(2));
    for (const auto& a : car->attributes) {
        if (a.name == "engine_") {
            CHECK(a.visibility == Visibility::Private);
            // The multiplicity of a member lives on the relation in a diagram
            // and is copied back onto the attribute when the file is read.
            CHECK_EQ(a.multiplicity, std::string("0..1"));
        }
        if (a.name == "built_") {
            CHECK(a.scope == Scope::Classifier);
            CHECK_EQ(a.multiplicity, std::string("*"));
        }
    }
    CHECK_EQ(car->operations.size(), std::size_t(1));
    CHECK_EQ(car->operations.front().is_pure, true);
    CHECK_EQ(car->operations.front().is_const, true);
    CHECK_EQ(returned.relations.size(), std::size_t(1));
    CHECK(returned.relations.front().kind == RelationKind::Aggregation);
}
