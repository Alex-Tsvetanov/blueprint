#include "testing.hpp"

#include "blueprint/emit.hpp"
#include "blueprint/model.hpp"

using namespace bp;

namespace {

Model sample_model() {
    Model model;
    model.name = "sample";

    Classifier engine;
    engine.name = "Engine";
    engine.qualified_name = "app::Engine";
    engine.kind = ClassifierKind::Struct;
    engine.attributes.push_back(Attribute{"horsepower", "int", Visibility::Public, Scope::Instance, "1", {}});
    model.classifiers.push_back(engine);

    Classifier car;
    car.name = "Car";
    car.qualified_name = "app::Car";
    car.is_abstract = true;
    car.stereotypes.push_back("persistent");
    car.tags.push_back(TaggedValue{"table", "cars"});
    car.attributes.push_back(Attribute{"engine_", "app::Engine", Visibility::Private, Scope::Instance, "1", {}});
    car.attributes.push_back(Attribute{"built_", "std::vector<int>", Visibility::Protected, Scope::Classifier, "*", {}});
    Operation mass;
    mass.name = "mass";
    mass.return_type = "double";
    mass.is_virtual = true;
    mass.is_pure = true;
    mass.is_const = true;
    car.operations.push_back(mass);
    Operation fit;
    fit.name = "fit";
    fit.return_type = "void";
    fit.parameters.push_back(Parameter{"wheel", "const app::Engine &"});
    car.operations.push_back(fit);
    model.classifiers.push_back(car);

    Relation composition;
    composition.from = "app::Car";
    composition.to = "app::Engine";
    composition.kind = RelationKind::Composition;
    composition.label = "engine_";
    model.relations.push_back(composition);

    Relation generalization;
    generalization.from = "app::Car";
    generalization.to = "app::Engine";
    generalization.kind = RelationKind::Generalization;
    generalization.multiplicity = "";
    generalization.is_virtual_base = true;
    generalization.visibility = Visibility::Protected;
    model.relations.push_back(generalization);

    model.normalize();
    return model;
}

} // namespace

TEST(emit, plantuml_carries_kind_visibility_scope_and_abstractness) {
    const std::string text = emit_class_diagram(sample_model(), Notation::PlantUml);
    CHECK_CONTAINS(text, "@startuml");
    CHECK_CONTAINS(text, "abstract class \"app::Car\" as app__Car");
    CHECK_CONTAINS(text, "<<persistent>>");
    CHECK_CONTAINS(text, "- engine_ : app::Engine");
    CHECK_CONTAINS(text, "# {static} built_ : std::vector<int> [*]");
    CHECK_CONTAINS(text, "+ {abstract} mass() const : double");
    CHECK_CONTAINS(text, "+ fit(wheel : const app::Engine &) : void");
    CHECK_CONTAINS(text, "@enduml");
}

TEST(emit, plantuml_uses_the_right_arrow_for_every_relationship) {
    const std::string text = emit_class_diagram(sample_model(), Notation::PlantUml);
    CHECK_CONTAINS(text, "app__Car *-- \"1\" app__Engine : engine_");
    CHECK_CONTAINS(text, "app__Engine <|-- app__Car : virtual protected");
}

TEST(emit, mermaid_rewrites_generics_because_it_reads_angle_brackets_itself) {
    const std::string text = emit_class_diagram(sample_model(), Notation::Mermaid);
    CHECK_CONTAINS(text, "classDiagram");
    CHECK_CONTAINS(text, "class app__Car[\"app::Car\"]");
    CHECK_CONTAINS(text, "std::vector~int~");
    CHECK(text.find("std::vector<int>") == std::string::npos);
}

TEST(emit, a_tagged_value_travels_in_a_note_because_neither_notation_has_a_slot_for_it) {
    const std::string plantuml = emit_class_diagram(sample_model(), Notation::PlantUml);
    const std::string mermaid = emit_class_diagram(sample_model(), Notation::Mermaid);
    CHECK_CONTAINS(plantuml, "note top of app__Car : bp: table=cars");
    CHECK_CONTAINS(mermaid, "note for app__Car \"bp: table=cars\"");
}

TEST(emit, the_alias_is_a_pure_function_of_the_qualified_name) {
    CHECK_EQ(diagram_alias("app::Car"), std::string("app__Car"));
    CHECK_EQ(diagram_alias("Box<int>"), std::string("Box_int_"));
    CHECK_EQ(diagram_alias("app::Car"), diagram_alias("app::Car"));
}
