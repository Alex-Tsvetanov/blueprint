#include "testing.hpp"

#include "blueprint/behavior.hpp"

using namespace bp;

namespace {

const char* kUseCaseSpec = R"(# the tool as its users see it
name Blueprint
system Blueprint
actor DEV : Developer
actor CI : Build pipeline
case UC1 : Extract a model from source
case UC2 : Emit a class diagram
case UC3 : Generate a relational schema
case UC4 : Check the diagram against the code
DEV -> UC1
DEV -> UC2
CI -> UC4
UC2 -> UC1 : include
UC3 -> UC1 : include
UC4 -> UC1 : extend
)";

const char* kActivitySpec = R"(name Round trip
start S
action A1 : Read the translation unit
decision D1 : Model complete
action A2 : Emit the diagram
action A3 : Report the gap
end E
S -> A1
A1 -> D1
D1 -> A2 : yes
D1 -> A3 : no
A2 -> E
A3 -> E
)";

} // namespace

TEST(behavior, reads_a_use_case_specification) {
    const UseCaseModel model = parse_use_case_spec(kUseCaseSpec);
    CHECK_EQ(model.name, std::string("Blueprint"));
    CHECK_EQ(model.system, std::string("Blueprint"));
    CHECK_EQ(model.actors.size(), std::size_t(2));
    CHECK_EQ(model.actors.front().name, std::string("Developer"));
    CHECK_EQ(model.use_cases.size(), std::size_t(4));
    CHECK_EQ(model.links.size(), std::size_t(6));
    CHECK(model.links[3].kind == UseCaseLinkKind::Include);
    CHECK(model.links[5].kind == UseCaseLinkKind::Extend);
}

TEST(behavior, emits_a_use_case_diagram_in_both_notations) {
    const UseCaseModel model = parse_use_case_spec(kUseCaseSpec);
    const std::string plantuml = emit_use_case_diagram(model, Notation::PlantUml);
    CHECK_CONTAINS(plantuml, "left to right direction");
    CHECK_CONTAINS(plantuml, "actor \"Developer\" as DEV");
    CHECK_CONTAINS(plantuml, "usecase \"Extract a model from source\" as UC1");
    CHECK_CONTAINS(plantuml, "UC2 ..> UC1 : <<include>>");

    // Mermaid has no use case diagram, so the model is rendered as a
    // flowchart. The loss is in the notation, not in the model.
    const std::string mermaid = emit_use_case_diagram(model, Notation::Mermaid);
    CHECK_CONTAINS(mermaid, "flowchart LR");
    CHECK_CONTAINS(mermaid, "UC1([\"Extract a model from source\"])");
    CHECK_CONTAINS(mermaid, "UC2 -.->|include| UC1");
}

TEST(behavior, reads_an_activity_specification_with_guards) {
    const ActivityModel model = parse_activity_spec(kActivitySpec);
    CHECK_EQ(model.name, std::string("Round trip"));
    CHECK_EQ(model.nodes.size(), std::size_t(6));
    CHECK_EQ(model.edges.size(), std::size_t(6));
    CHECK(model.node("S") != nullptr);
    CHECK(model.node("S")->kind == ActivityNodeKind::Start);
    CHECK(model.node("D1")->kind == ActivityNodeKind::Decision);
    CHECK_EQ(model.edges[2].guard, std::string("yes"));
    CHECK_EQ(model.edges[3].guard, std::string("no"));
}

TEST(behavior, emits_an_activity_diagram_in_both_notations) {
    const ActivityModel model = parse_activity_spec(kActivitySpec);
    const std::string plantuml = emit_activity_diagram(model, Notation::PlantUml);
    // The original PlantUML activity syntax is used because it accepts an
    // arbitrary directed graph, which the newer structured one does not.
    CHECK_CONTAINS(plantuml, "(*) --> \"Read the translation unit\"");
    CHECK_CONTAINS(plantuml, "-->[yes]");

    const std::string mermaid = emit_activity_diagram(model, Notation::Mermaid);
    CHECK_CONTAINS(mermaid, "flowchart TD");
    CHECK_CONTAINS(mermaid, "D1{\"Model complete\"}");
    CHECK_CONTAINS(mermaid, "D1 -->|yes| A2");
}
