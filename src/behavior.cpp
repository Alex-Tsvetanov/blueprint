#include "blueprint/behavior.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace bp {
namespace {

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(trim(line));
    }
    return out;
}

// "id : label" splits into the two halves; a line without a colon is all id.
void split_pair(const std::string& text, std::string& left, std::string& right) {
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) { left = trim(text); right.clear(); return; }
    left = trim(text.substr(0, colon));
    right = trim(text.substr(colon + 1));
}

bool take_keyword(const std::string& line, const char* keyword, std::string& rest) {
    const std::string prefix = std::string(keyword) + " ";
    if (!starts_with(line, prefix)) return false;
    rest = trim(line.substr(prefix.size()));
    return true;
}

} // namespace

const ActivityNode* ActivityModel::node(const std::string& id) const {
    for (const auto& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

UseCaseModel parse_use_case_spec(const std::string& text) {
    UseCaseModel model;
    for (const std::string& line : split_lines(text)) {
        if (line.empty() || starts_with(line, "#")) continue;
        std::string rest;
        if (take_keyword(line, "name", rest)) { model.name = rest; continue; }
        if (take_keyword(line, "system", rest)) { model.system = rest; continue; }
        if (take_keyword(line, "actor", rest)) {
            Actor a;
            split_pair(rest, a.id, a.name);
            if (a.name.empty()) a.name = a.id;
            model.actors.push_back(std::move(a));
            continue;
        }
        if (take_keyword(line, "case", rest)) {
            UseCase u;
            split_pair(rest, u.id, u.name);
            if (u.name.empty()) u.name = u.id;
            model.use_cases.push_back(std::move(u));
            continue;
        }
        const std::size_t arrow = line.find("->");
        if (arrow == std::string::npos) continue;
        UseCaseLink link;
        link.from = trim(line.substr(0, arrow));
        std::string tail = trim(line.substr(arrow + 2));
        std::string target;
        std::string kind;
        split_pair(tail, target, kind);
        link.to = target;
        if (kind == "include") link.kind = UseCaseLinkKind::Include;
        else if (kind == "extend") link.kind = UseCaseLinkKind::Extend;
        else if (kind == "generalization") link.kind = UseCaseLinkKind::Generalization;
        model.links.push_back(std::move(link));
    }
    return model;
}

ActivityModel parse_activity_spec(const std::string& text) {
    ActivityModel model;
    const auto add_node = [&model](const std::string& rest, ActivityNodeKind kind) {
        ActivityNode n;
        n.kind = kind;
        split_pair(rest, n.id, n.label);
        if (n.label.empty()) n.label = n.id;
        model.nodes.push_back(std::move(n));
    };

    for (const std::string& line : split_lines(text)) {
        if (line.empty() || starts_with(line, "#")) continue;
        std::string rest;
        if (take_keyword(line, "name", rest)) { model.name = rest; continue; }
        if (take_keyword(line, "start", rest)) { add_node(rest, ActivityNodeKind::Start); continue; }
        if (take_keyword(line, "end", rest)) { add_node(rest, ActivityNodeKind::End); continue; }
        if (take_keyword(line, "action", rest)) { add_node(rest, ActivityNodeKind::Action); continue; }
        if (take_keyword(line, "decision", rest)) { add_node(rest, ActivityNodeKind::Decision); continue; }
        if (take_keyword(line, "merge", rest)) { add_node(rest, ActivityNodeKind::Merge); continue; }
        if (take_keyword(line, "fork", rest)) { add_node(rest, ActivityNodeKind::Fork); continue; }
        if (take_keyword(line, "join", rest)) { add_node(rest, ActivityNodeKind::Join); continue; }

        const std::size_t arrow = line.find("->");
        if (arrow == std::string::npos) continue;
        ActivityEdge e;
        e.from = trim(line.substr(0, arrow));
        std::string tail = trim(line.substr(arrow + 2));
        split_pair(tail, e.to, e.guard);
        model.edges.push_back(std::move(e));
    }
    return model;
}

namespace {

std::string escape_quotes(std::string s) {
    std::string out;
    for (const char c : s) {
        if (c == '"') out += "'";
        else out.push_back(c);
    }
    return out;
}

std::string use_case_plantuml(const UseCaseModel& model) {
    std::ostringstream out;
    out << "@startuml\n' generated by Blueprint\n";
    out << "left to right direction\n";
    out << "title " << model.name << "\n\n";
    for (const auto& a : model.actors) {
        out << "actor \"" << escape_quotes(a.name) << "\" as " << diagram_alias(a.id) << "\n";
    }
    out << "\n";
    if (!model.system.empty()) out << "rectangle \"" << escape_quotes(model.system) << "\" {\n";
    for (const auto& u : model.use_cases) {
        out << (model.system.empty() ? "" : "  ") << "usecase \"" << escape_quotes(u.name)
            << "\" as " << diagram_alias(u.id) << "\n";
    }
    if (!model.system.empty()) out << "}\n";
    out << "\n";
    for (const auto& l : model.links) {
        const std::string from = diagram_alias(l.from);
        const std::string to = diagram_alias(l.to);
        switch (l.kind) {
        case UseCaseLinkKind::Association: out << from << " --> " << to << "\n"; break;
        case UseCaseLinkKind::Include: out << from << " ..> " << to << " : <<include>>\n"; break;
        case UseCaseLinkKind::Extend: out << from << " ..> " << to << " : <<extend>>\n"; break;
        case UseCaseLinkKind::Generalization: out << to << " <|-- " << from << "\n"; break;
        }
    }
    out << "@enduml\n";
    return out.str();
}

// Mermaid has no use case diagram, so the model is rendered as a flowchart:
// actors as rounded nodes, use cases as stadium nodes, the system as a
// subgraph. The loss is in the notation, not in the model, which is precisely
// the point of keeping the two apart.
std::string use_case_mermaid(const UseCaseModel& model) {
    std::ostringstream out;
    out << "%% generated by Blueprint\n";
    out << "flowchart LR\n";
    out << "  %% title: " << model.name << "\n";
    for (const auto& a : model.actors) {
        out << "  " << diagram_alias(a.id) << "[\"" << escape_quotes(a.name) << "\"]\n";
    }
    if (!model.system.empty()) out << "  subgraph " << diagram_alias(model.system) << "[\""
                                   << escape_quotes(model.system) << "\"]\n";
    for (const auto& u : model.use_cases) {
        out << (model.system.empty() ? "  " : "    ") << diagram_alias(u.id) << "([\""
            << escape_quotes(u.name) << "\"])\n";
    }
    if (!model.system.empty()) out << "  end\n";
    for (const auto& l : model.links) {
        const std::string from = diagram_alias(l.from);
        const std::string to = diagram_alias(l.to);
        switch (l.kind) {
        case UseCaseLinkKind::Association: out << "  " << from << " --- " << to << "\n"; break;
        case UseCaseLinkKind::Include: out << "  " << from << " -.->|include| " << to << "\n"; break;
        case UseCaseLinkKind::Extend: out << "  " << from << " -.->|extend| " << to << "\n"; break;
        case UseCaseLinkKind::Generalization: out << "  " << to << " --> " << from << "\n"; break;
        }
    }
    return out.str();
}

// PlantUML's original activity syntax is used rather than the newer one,
// because it accepts an arbitrary directed graph. The newer syntax is written
// as structured control flow and cannot express a graph read from a file.
std::string activity_plantuml(const ActivityModel& model) {
    std::ostringstream out;
    out << "@startuml\n' generated by Blueprint\n";
    out << "title " << model.name << "\n\n";
    const auto render = [&model](const std::string& id) -> std::string {
        const ActivityNode* n = model.node(id);
        if (!n) return "\"" + escape_quotes(id) + "\"";
        if (n->kind == ActivityNodeKind::Start || n->kind == ActivityNodeKind::End) return "(*)";
        if (n->kind == ActivityNodeKind::Decision) return "\"" + escape_quotes(n->label) + "?\"";
        return "\"" + escape_quotes(n->label) + "\"";
    };
    for (const auto& e : model.edges) {
        out << render(e.from) << " -->";
        if (!e.guard.empty()) out << "[" << escape_quotes(e.guard) << "]";
        out << " " << render(e.to) << "\n";
    }
    out << "@enduml\n";
    return out.str();
}

std::string activity_mermaid(const ActivityModel& model) {
    std::ostringstream out;
    out << "%% generated by Blueprint\n";
    out << "flowchart TD\n";
    out << "  %% title: " << model.name << "\n";
    for (const auto& n : model.nodes) {
        const std::string id = diagram_alias(n.id);
        const std::string label = escape_quotes(n.label);
        switch (n.kind) {
        case ActivityNodeKind::Start:
        case ActivityNodeKind::End: out << "  " << id << "((\"" << label << "\"))\n"; break;
        case ActivityNodeKind::Decision: out << "  " << id << "{\"" << label << "\"}\n"; break;
        case ActivityNodeKind::Fork:
        case ActivityNodeKind::Join:
        case ActivityNodeKind::Merge: out << "  " << id << "[/\"" << label << "\"/]\n"; break;
        case ActivityNodeKind::Action: out << "  " << id << "[\"" << label << "\"]\n"; break;
        }
    }
    for (const auto& e : model.edges) {
        out << "  " << diagram_alias(e.from) << " -->";
        if (!e.guard.empty()) out << "|" << escape_quotes(e.guard) << "|";
        out << " " << diagram_alias(e.to) << "\n";
    }
    return out.str();
}

} // namespace

std::string emit_use_case_diagram(const UseCaseModel& model, Notation notation) {
    return notation == Notation::Mermaid ? use_case_mermaid(model) : use_case_plantuml(model);
}

std::string emit_activity_diagram(const ActivityModel& model, Notation notation) {
    return notation == Notation::Mermaid ? activity_mermaid(model) : activity_plantuml(model);
}

} // namespace bp
