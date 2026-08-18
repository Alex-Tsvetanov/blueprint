// Use case and activity models.
//
// Neither can be extracted from source: a use case is a statement about
// intent and an activity is a statement about a flow of control that the
// structure of the code does not carry. They are therefore written by hand in
// a small text form and emitted to the same two notations as the class
// diagram, so that one model still feeds every output.
#ifndef BLUEPRINT_BEHAVIOR_HPP
#define BLUEPRINT_BEHAVIOR_HPP

#include <string>
#include <vector>

#include "blueprint/emit.hpp"

namespace bp {

struct Actor {
    std::string id;
    std::string name;
};

struct UseCase {
    std::string id;
    std::string name;
};

enum class UseCaseLinkKind { Association, Include, Extend, Generalization };

struct UseCaseLink {
    std::string from;
    std::string to;
    UseCaseLinkKind kind = UseCaseLinkKind::Association;
};

struct UseCaseModel {
    std::string name = "use cases";
    std::string system;
    std::vector<Actor> actors;
    std::vector<UseCase> use_cases;
    std::vector<UseCaseLink> links;
};

enum class ActivityNodeKind { Start, End, Action, Decision, Merge, Fork, Join };

struct ActivityNode {
    std::string id;
    std::string label;
    ActivityNodeKind kind = ActivityNodeKind::Action;
};

struct ActivityEdge {
    std::string from;
    std::string to;
    std::string guard;
};

struct ActivityModel {
    std::string name = "activity";
    std::vector<ActivityNode> nodes;
    std::vector<ActivityEdge> edges;

    const ActivityNode* node(const std::string& id) const;
};

// The text form. One statement per line, "#" starts a comment.
//
//   use case:  system <name> | actor <id> : <label> | case <id> : <label>
//              <a> -> <b> [: include|extend|generalization]
//   activity:  action <id> : <label> | decision <id> : <label>
//              start <id> | end <id> | fork <id> | join <id> | merge <id>
//              <a> -> <b> [: guard]
UseCaseModel parse_use_case_spec(const std::string& text);
ActivityModel parse_activity_spec(const std::string& text);

std::string emit_use_case_diagram(const UseCaseModel& model, Notation notation);
std::string emit_activity_diagram(const ActivityModel& model, Notation notation);

} // namespace bp

#endif
