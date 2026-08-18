#include "blueprint/model.hpp"

#include <algorithm>

namespace bp {
namespace {

// A small two-way table beats two switch statements that can drift apart.
struct VisibilityName { Visibility value; const char* text; };
constexpr VisibilityName kVisibility[] = {
    {Visibility::Public, "public"},
    {Visibility::Protected, "protected"},
    {Visibility::Private, "private"},
    {Visibility::Package, "package"},
};

struct ScopeName { Scope value; const char* text; };
constexpr ScopeName kScope[] = {
    {Scope::Instance, "instance"},
    {Scope::Classifier, "classifier"},
};

struct KindName { ClassifierKind value; const char* text; };
constexpr KindName kKind[] = {
    {ClassifierKind::Class, "class"},
    {ClassifierKind::Struct, "struct"},
    {ClassifierKind::Interface, "interface"},
    {ClassifierKind::Enumeration, "enumeration"},
    {ClassifierKind::Template, "template"},
};

struct RelationName { RelationKind value; const char* text; };
constexpr RelationName kRelation[] = {
    {RelationKind::Generalization, "generalization"},
    {RelationKind::Realization, "realization"},
    {RelationKind::Association, "association"},
    {RelationKind::Aggregation, "aggregation"},
    {RelationKind::Composition, "composition"},
    {RelationKind::Dependency, "dependency"},
};

template <typename Table, typename Value>
std::string name_of(const Table& table, Value v) {
    for (const auto& row : table) {
        if (row.value == v) return row.text;
    }
    return "unknown";
}

template <typename Table>
auto value_of(const Table& table, const std::string& s) {
    for (const auto& row : table) {
        if (s == row.text) return row.value;
    }
    return table[0].value;
}

Json tags_to_json(const std::vector<TaggedValue>& tags) {
    Json arr = Json::array();
    for (const auto& t : tags) {
        Json o = Json::object();
        o.set("name", t.name);
        o.set("value", t.value);
        arr.push_back(std::move(o));
    }
    return arr;
}

std::vector<TaggedValue> tags_from_json(const Json& j) {
    std::vector<TaggedValue> out;
    for (const auto& item : j.items()) {
        out.push_back(TaggedValue{item.str("name"), item.str("value")});
    }
    return out;
}

Json strings_to_json(const std::vector<std::string>& v) {
    Json arr = Json::array();
    for (const auto& s : v) arr.push_back(s);
    return arr;
}

std::vector<std::string> strings_from_json(const Json& j) {
    std::vector<std::string> out;
    for (const auto& item : j.items()) out.push_back(item.as_string());
    return out;
}

} // namespace

std::string to_string(Visibility v) { return name_of(kVisibility, v); }
std::string to_string(Scope s) { return name_of(kScope, s); }
std::string to_string(ClassifierKind k) { return name_of(kKind, k); }
std::string to_string(RelationKind r) { return name_of(kRelation, r); }

Visibility visibility_from_string(const std::string& s) { return value_of(kVisibility, s); }
Scope scope_from_string(const std::string& s) { return value_of(kScope, s); }
ClassifierKind classifier_kind_from_string(const std::string& s) { return value_of(kKind, s); }
RelationKind relation_kind_from_string(const std::string& s) { return value_of(kRelation, s); }

std::string Operation::signature() const {
    std::string out = name;
    out += '(';
    for (std::size_t k = 0; k < parameters.size(); ++k) {
        if (k) out += ", ";
        out += parameters[k].type;
    }
    out += ')';
    if (is_const) out += " const";
    return out;
}

std::string Relation::key() const {
    return from + "|" + to_string(kind) + "|" + to + "|" + label;
}

const TaggedValue* Classifier::tag(const std::string& tag_name) const {
    for (const auto& t : tags) {
        if (t.name == tag_name) return &t;
    }
    return nullptr;
}

bool Classifier::has_stereotype(const std::string& s) const {
    return std::find(stereotypes.begin(), stereotypes.end(), s) != stereotypes.end();
}

const Classifier* Model::find(const std::string& qualified_name) const {
    for (const auto& c : classifiers) {
        if (c.qualified_name == qualified_name) return &c;
    }
    return nullptr;
}

Classifier* Model::find(const std::string& qualified_name) {
    for (auto& c : classifiers) {
        if (c.qualified_name == qualified_name) return &c;
    }
    return nullptr;
}

void Model::normalize() {
    for (auto& c : classifiers) {
        // Members keep declaration order inside one visibility section but the
        // canonical form sorts by name. Declaration order is not part of the
        // UML model and would make two equivalent models compare unequal.
        std::sort(c.attributes.begin(), c.attributes.end(),
                  [](const Attribute& a, const Attribute& b) { return a.name < b.name; });
        std::sort(c.operations.begin(), c.operations.end(),
                  [](const Operation& a, const Operation& b) {
                      return a.signature() < b.signature();
                  });
        std::sort(c.stereotypes.begin(), c.stereotypes.end());
        std::sort(c.tags.begin(), c.tags.end(),
                  [](const TaggedValue& a, const TaggedValue& b) { return a.name < b.name; });
    }
    std::sort(classifiers.begin(), classifiers.end(),
              [](const Classifier& a, const Classifier& b) {
                  return a.qualified_name < b.qualified_name;
              });
    std::sort(relations.begin(), relations.end(),
              [](const Relation& a, const Relation& b) { return a.key() < b.key(); });
    relations.erase(std::unique(relations.begin(), relations.end(),
                                [](const Relation& a, const Relation& b) {
                                    return a.key() == b.key();
                                }),
                    relations.end());
}

std::size_t Model::element_count() const {
    std::size_t n = classifiers.size() + relations.size();
    for (const auto& c : classifiers) {
        n += c.attributes.size() + c.operations.size();
    }
    return n;
}

Json Model::to_json() const {
    Json root = Json::object();
    root.set("blueprint_model", 1);
    root.set("name", name);

    Json classes = Json::array();
    for (const auto& c : classifiers) {
        Json o = Json::object();
        o.set("name", c.name);
        o.set("qualified_name", c.qualified_name);
        o.set("kind", to_string(c.kind));
        o.set("abstract", c.is_abstract);
        if (!c.template_parameters.empty())
            o.set("template_parameters", strings_to_json(c.template_parameters));
        if (!c.stereotypes.empty()) o.set("stereotypes", strings_to_json(c.stereotypes));
        if (!c.tags.empty()) o.set("tags", tags_to_json(c.tags));
        if (!c.enumerators.empty()) o.set("enumerators", strings_to_json(c.enumerators));

        Json attrs = Json::array();
        for (const auto& a : c.attributes) {
            Json ao = Json::object();
            ao.set("name", a.name);
            ao.set("type", a.type);
            ao.set("visibility", to_string(a.visibility));
            ao.set("scope", to_string(a.scope));
            ao.set("multiplicity", a.multiplicity);
            if (!a.tags.empty()) ao.set("tags", tags_to_json(a.tags));
            attrs.push_back(std::move(ao));
        }
        o.set("attributes", std::move(attrs));

        Json ops = Json::array();
        for (const auto& m : c.operations) {
            Json mo = Json::object();
            mo.set("name", m.name);
            mo.set("return_type", m.return_type);
            Json params = Json::array();
            for (const auto& p : m.parameters) {
                Json po = Json::object();
                po.set("name", p.name);
                po.set("type", p.type);
                params.push_back(std::move(po));
            }
            mo.set("parameters", std::move(params));
            mo.set("visibility", to_string(m.visibility));
            mo.set("scope", to_string(m.scope));
            mo.set("virtual", m.is_virtual);
            mo.set("pure", m.is_pure);
            mo.set("const", m.is_const);
            ops.push_back(std::move(mo));
        }
        o.set("operations", std::move(ops));

        if (!c.source_file.empty()) {
            o.set("source_file", c.source_file);
            o.set("source_line", c.source_line);
        }
        classes.push_back(std::move(o));
    }
    root.set("classifiers", std::move(classes));

    Json rels = Json::array();
    for (const auto& r : relations) {
        Json o = Json::object();
        o.set("from", r.from);
        o.set("to", r.to);
        o.set("kind", to_string(r.kind));
        o.set("label", r.label);
        o.set("multiplicity", r.multiplicity);
        o.set("virtual_base", r.is_virtual_base);
        o.set("visibility", to_string(r.visibility));
        rels.push_back(std::move(o));
    }
    root.set("relations", std::move(rels));
    return root;
}

Model Model::from_json(const Json& j) {
    Model m;
    m.name = j.str("name", "model");
    for (const auto& co : j.at("classifiers").items()) {
        Classifier c;
        c.name = co.str("name");
        c.qualified_name = co.str("qualified_name", c.name);
        c.kind = classifier_kind_from_string(co.str("kind", "class"));
        c.is_abstract = co.flag("abstract");
        c.template_parameters = strings_from_json(co.at("template_parameters"));
        c.stereotypes = strings_from_json(co.at("stereotypes"));
        c.tags = tags_from_json(co.at("tags"));
        c.enumerators = strings_from_json(co.at("enumerators"));
        c.source_file = co.str("source_file");
        c.source_line = static_cast<int>(co.at("source_line").as_int());
        for (const auto& ao : co.at("attributes").items()) {
            Attribute a;
            a.name = ao.str("name");
            a.type = ao.str("type");
            a.visibility = visibility_from_string(ao.str("visibility", "private"));
            a.scope = scope_from_string(ao.str("scope", "instance"));
            a.multiplicity = ao.str("multiplicity", "1");
            a.tags = tags_from_json(ao.at("tags"));
            c.attributes.push_back(std::move(a));
        }
        for (const auto& mo : co.at("operations").items()) {
            Operation op;
            op.name = mo.str("name");
            op.return_type = mo.str("return_type");
            for (const auto& po : mo.at("parameters").items()) {
                op.parameters.push_back(Parameter{po.str("name"), po.str("type")});
            }
            op.visibility = visibility_from_string(mo.str("visibility", "public"));
            op.scope = scope_from_string(mo.str("scope", "instance"));
            op.is_virtual = mo.flag("virtual");
            op.is_pure = mo.flag("pure");
            op.is_const = mo.flag("const");
            c.operations.push_back(std::move(op));
        }
        m.classifiers.push_back(std::move(c));
    }
    for (const auto& ro : j.at("relations").items()) {
        Relation r;
        r.from = ro.str("from");
        r.to = ro.str("to");
        r.kind = relation_kind_from_string(ro.str("kind", "association"));
        r.label = ro.str("label");
        r.multiplicity = ro.str("multiplicity", "1");
        r.is_virtual_base = ro.flag("virtual_base");
        r.visibility = visibility_from_string(ro.str("visibility", "public"));
        m.relations.push_back(std::move(r));
    }
    return m;
}

} // namespace bp
