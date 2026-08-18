#include "blueprint/diff.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace bp {

std::string to_string(DiffKind k) {
    switch (k) {
    case DiffKind::ClassAdded: return "class-added";
    case DiffKind::ClassRemoved: return "class-removed";
    case DiffKind::ClassChanged: return "class-changed";
    case DiffKind::MemberAdded: return "member-added";
    case DiffKind::MemberRemoved: return "member-removed";
    case DiffKind::MemberChanged: return "member-changed";
    case DiffKind::RelationAdded: return "relation-added";
    case DiffKind::RelationRemoved: return "relation-removed";
    case DiffKind::RelationChanged: return "relation-changed";
    }
    return "unknown";
}

std::string DiffReport::text() const {
    if (differences.empty()) return "the diagram and the code agree\n";
    std::ostringstream out;
    out << differences.size() << " difference"
        << (differences.size() == 1 ? "" : "s") << " between the code and the diagram\n";
    for (const auto& d : differences) {
        out << "  " << to_string(d.kind) << "  " << d.element;
        if (!d.detail.empty()) out << "  (" << d.detail << ")";
        out << "\n";
    }
    return out.str();
}

namespace {

// A relation is matched by its endpoints and its label, so that a change of
// kind or multiplicity reads as a change rather than as a removal plus an
// addition. That distinction is the whole value of the report.
std::string relation_identity(const Relation& r) {
    return r.from + " -> " + r.to + (r.label.empty() ? "" : " (" + r.label + ")");
}

std::string describe(const Attribute& a) {
    return to_string(a.visibility) + " " + a.name + " : " + a.type +
           (a.multiplicity == "1" ? "" : " [" + a.multiplicity + "]") +
           (a.scope == Scope::Classifier ? " {static}" : "");
}

std::string describe(const Operation& o) {
    std::string text = to_string(o.visibility) + " " + o.signature();
    if (!o.return_type.empty()) text += " : " + o.return_type;
    if (o.is_pure) text += " {abstract}";
    else if (o.is_virtual) text += " {virtual}";
    if (o.scope == Scope::Classifier) text += " {static}";
    return text;
}

std::string describe(const Relation& r) {
    std::string text = to_string(r.kind);
    if (!r.multiplicity.empty()) text += " [" + r.multiplicity + "]";
    if (r.is_virtual_base) text += " virtual";
    if (r.kind == RelationKind::Generalization && r.visibility != Visibility::Public) {
        text += " " + to_string(r.visibility);
    }
    return text;
}

void compare_members(const Classifier& code, const Classifier& design,
                     std::vector<Difference>& out) {
    std::map<std::string, const Attribute*> design_attributes;
    for (const auto& a : design.attributes) design_attributes[a.name] = &a;
    for (const auto& a : code.attributes) {
        const auto it = design_attributes.find(a.name);
        if (it == design_attributes.end()) {
            out.push_back({DiffKind::MemberAdded, code.qualified_name + "." + a.name,
                           describe(a)});
            continue;
        }
        if (describe(a) != describe(*it->second)) {
            out.push_back({DiffKind::MemberChanged, code.qualified_name + "." + a.name,
                           "code: " + describe(a) + "; diagram: " + describe(*it->second)});
        }
        design_attributes.erase(it);
    }
    for (const auto& [name, a] : design_attributes) {
        out.push_back({DiffKind::MemberRemoved, code.qualified_name + "." + name, describe(*a)});
    }

    std::map<std::string, const Operation*> design_operations;
    for (const auto& o : design.operations) design_operations[o.signature()] = &o;
    for (const auto& o : code.operations) {
        const auto it = design_operations.find(o.signature());
        if (it == design_operations.end()) {
            out.push_back({DiffKind::MemberAdded, code.qualified_name + "." + o.signature(),
                           describe(o)});
            continue;
        }
        if (describe(o) != describe(*it->second)) {
            out.push_back({DiffKind::MemberChanged, code.qualified_name + "." + o.signature(),
                           "code: " + describe(o) + "; diagram: " + describe(*it->second)});
        }
        design_operations.erase(it);
    }
    for (const auto& [signature, o] : design_operations) {
        out.push_back({DiffKind::MemberRemoved, code.qualified_name + "." + signature,
                       describe(*o)});
    }
}

} // namespace

std::size_t preserved_elements(const Model& code, const Model& returned) {
    std::size_t kept = 0;
    for (const auto& c : code.classifiers) {
        const Classifier* other = returned.find(c.qualified_name);
        if (!other) continue;
        if (c.kind == other->kind && c.is_abstract == other->is_abstract) ++kept;
        for (const auto& a : c.attributes) {
            for (const auto& b : other->attributes) {
                if (a.name == b.name && describe(a) == describe(b)) { ++kept; break; }
            }
        }
        for (const auto& o : c.operations) {
            for (const auto& p : other->operations) {
                if (o.signature() == p.signature() && describe(o) == describe(p)) { ++kept; break; }
            }
        }
    }
    for (const auto& r : code.relations) {
        for (const auto& s : returned.relations) {
            if (relation_identity(r) == relation_identity(s) && describe(r) == describe(s)) {
                ++kept;
                break;
            }
        }
    }
    return kept;
}

DiffReport compare(const Model& code, const Model& design) {
    DiffReport report;

    std::map<std::string, const Classifier*> design_classes;
    for (const auto& c : design.classifiers) design_classes[c.qualified_name] = &c;

    for (const auto& c : code.classifiers) {
        const auto it = design_classes.find(c.qualified_name);
        if (it == design_classes.end()) {
            report.differences.push_back(
                {DiffKind::ClassAdded, c.qualified_name, "in the code, not in the diagram"});
            continue;
        }
        const Classifier& other = *it->second;
        if (c.kind != other.kind || c.is_abstract != other.is_abstract) {
            report.differences.push_back(
                {DiffKind::ClassChanged, c.qualified_name,
                 "code: " + to_string(c.kind) + (c.is_abstract ? " abstract" : "") +
                     "; diagram: " + to_string(other.kind) + (other.is_abstract ? " abstract" : "")});
        }
        compare_members(c, other, report.differences);
        design_classes.erase(it);
    }
    for (const auto& [name, c] : design_classes) {
        report.differences.push_back(
            {DiffKind::ClassRemoved, name, "in the diagram, not in the code"});
        (void)c;
    }

    std::map<std::string, const Relation*> design_relations;
    for (const auto& r : design.relations) design_relations[relation_identity(r)] = &r;
    for (const auto& r : code.relations) {
        const std::string id = relation_identity(r);
        const auto it = design_relations.find(id);
        if (it == design_relations.end()) {
            report.differences.push_back({DiffKind::RelationAdded, id, describe(r)});
            continue;
        }
        if (describe(r) != describe(*it->second)) {
            report.differences.push_back(
                {DiffKind::RelationChanged, id,
                 "code: " + describe(r) + "; diagram: " + describe(*it->second)});
        }
        design_relations.erase(it);
    }
    for (const auto& [id, r] : design_relations) {
        report.differences.push_back({DiffKind::RelationRemoved, id, describe(*r)});
    }

    std::sort(report.differences.begin(), report.differences.end(),
              [](const Difference& a, const Difference& b) {
                  return std::tie(a.element, a.kind) < std::tie(b.element, b.kind);
              });
    return report;
}

} // namespace bp
