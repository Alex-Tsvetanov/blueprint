#include "blueprint/reader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

#include "blueprint/io.hpp"

namespace bp {
namespace {

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Path comparison has to survive mixed separators and, on Windows, mixed case.
std::string normalize_path(std::string p) {
    for (char& c : p) {
        if (c == '\\') c = '/';
#ifdef _WIN32
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    }
    return p;
}

// Removes one leading `const` or `volatile` and any trailing `const`.
std::string strip_cv(std::string t) {
    t = trim(std::move(t));
    for (const char* kw : {"const ", "volatile "}) {
        while (starts_with(t, kw)) t = trim(t.substr(std::string(kw).size()));
    }
    while (ends_with(t, " const") || ends_with(t, " volatile")) {
        t = trim(t.substr(0, t.rfind(' ')));
    }
    return t;
}

// Splits "A, B<C, D>, E" on the commas that are not inside angle brackets.
std::vector<std::string> split_template_args(const std::string& args) {
    std::vector<std::string> out;
    int depth = 0;
    std::string current;
    for (const char c : args) {
        if (c == '<' || c == '(') ++depth;
        if (c == '>' || c == ')') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!trim(current).empty()) out.push_back(trim(current));
    return out;
}

// Splits "std::vector<int>" into "std::vector" and "int". Returns false when
// the type is not a template specialisation.
bool split_template(const std::string& type, std::string& base, std::string& args) {
    const std::size_t open = type.find('<');
    if (open == std::string::npos || !ends_with(type, ">")) return false;
    base = trim(type.substr(0, open));
    args = trim(type.substr(open + 1, type.size() - open - 2));
    return true;
}

std::string last_component(const std::string& qualified) {
    const std::size_t pos = qualified.rfind("::");
    return pos == std::string::npos ? qualified : qualified.substr(pos + 2);
}

// Matches a type name against the model. Exact qualified match wins; a bare
// name matches a classifier whose last component is that name, which is what
// happens when Clang prints the type as written inside its own namespace.
std::string resolve_name(const std::string& name,
                         const std::vector<std::string>& known) {
    if (name.empty()) return {};
    for (const auto& k : known) {
        if (k == name) return k;
    }
    const std::string bare = last_component(name);
    std::string match;
    int hits = 0;
    for (const auto& k : known) {
        if (last_component(k) == bare) { match = k; ++hits; }
    }
    // An ambiguous bare name resolves to nothing rather than to a guess.
    return hits == 1 ? match : std::string();
}

const std::set<std::string>& owning_smart_pointers() {
    static const std::set<std::string> names = {"unique_ptr", "optional"};
    return names;
}

const std::set<std::string>& shared_smart_pointers() {
    static const std::set<std::string> names = {"shared_ptr", "weak_ptr"};
    return names;
}

const std::set<std::string>& sequence_containers() {
    static const std::set<std::string> names = {
        "vector", "list", "deque", "set", "multiset", "unordered_set",
        "forward_list", "array", "span", "initializer_list"};
    return names;
}

const std::set<std::string>& map_containers() {
    static const std::set<std::string> names = {"map", "multimap", "unordered_map",
                                                "unordered_multimap"};
    return names;
}

} // namespace

// The ownership rule of the tool, in one function.
//
//   T, std::optional<T>, std::unique_ptr<T>   exclusive ownership  composition
//   std::shared_ptr<T>                        shared ownership     aggregation
//   T*, T&                                    no ownership         association
//   container of the above                    the same, with *
//
// The reading follows UML: composition means the part dies with the whole, so
// a member held by value or by a pointer that owns it exclusively is a
// composition; a shared_ptr keeps the part alive past the whole, which is
// aggregation; and a raw pointer or reference asserts nothing about lifetime,
// which leaves a plain association.
MemberClassification classify_member_type(const std::string& type,
                                          const std::vector<std::string>& known) {
    MemberClassification result;
    std::string t = strip_cv(type);

    // References and raw pointers first: they bind tighter than any template
    // spelling around them.
    if (ends_with(t, "&&") || ends_with(t, "&")) {
        t = strip_cv(t.substr(0, t.size() - (ends_with(t, "&&") ? 2 : 1)));
        const std::string target = resolve_name(t, known);
        if (target.empty()) return result;
        result = {true, RelationKind::Association, target, "1"};
        return result;
    }
    if (ends_with(t, "*")) {
        t = strip_cv(t.substr(0, t.size() - 1));
        const std::string target = resolve_name(t, known);
        if (target.empty()) return result;
        result = {true, RelationKind::Association, target, "0..1"};
        return result;
    }

    std::string base;
    std::string args;
    if (split_template(t, base, args)) {
        const std::string tmpl = last_component(base);
        const std::vector<std::string> parts = split_template_args(args);
        if (parts.empty()) return result;

        if (owning_smart_pointers().count(tmpl)) {
            MemberClassification inner = classify_member_type(parts[0], known);
            if (!inner.is_relation) return result;
            // The pointer owns, so the inner value kind does not weaken it,
            // but a shared inner does: unique_ptr<shared_ptr<T>> is shared.
            inner.kind = inner.kind == RelationKind::Aggregation ? RelationKind::Aggregation
                                                                 : RelationKind::Composition;
            if (inner.kind == RelationKind::Composition &&
                classify_member_type(parts[0], known).kind == RelationKind::Association) {
                inner.kind = RelationKind::Association;
            }
            inner.multiplicity = "0..1";
            return inner;
        }
        if (shared_smart_pointers().count(tmpl)) {
            MemberClassification inner = classify_member_type(parts[0], known);
            if (!inner.is_relation) return result;
            inner.kind = RelationKind::Aggregation;
            inner.multiplicity = "0..1";
            return inner;
        }
        if (sequence_containers().count(tmpl) || map_containers().count(tmpl)) {
            // For a map the modelled end is the mapped type, not the key.
            const std::size_t index = map_containers().count(tmpl) && parts.size() > 1 ? 1 : 0;
            MemberClassification inner = classify_member_type(parts[index], known);
            if (!inner.is_relation) return result;
            inner.multiplicity = "*";
            return inner;
        }
        // Any other template used by value is a value member of that template.
        const std::string target = resolve_name(base, known);
        if (target.empty()) return result;
        return {true, RelationKind::Composition, target, "1"};
    }

    const std::string target = resolve_name(t, known);
    if (target.empty()) return result;
    return {true, RelationKind::Composition, target, "1"};
}

namespace {

// ---------------------------------------------------------------------------
// The traversal.
// ---------------------------------------------------------------------------
class AstWalker {
public:
    explicit AstWalker(const ReaderOptions& options) : options_(options) {
        for (const auto& r : options.roots) roots_.push_back(normalize_path(r));
    }

    Model run(const Json& ast) {
        model_.name = options_.model_name;
        walk(ast, "");
        resolve_relations();
        classify_interfaces();
        model_.normalize();
        return std::move(model_);
    }

private:
    // Clang prints `loc.file` only when the file changes during the dump, so
    // the current file has to be carried along the traversal in document
    // order. Missing this is the difference between a model of the project
    // and a model of every system header it touches.
    void track_file(const Json& node) {
        const Json& loc = node.at("loc");
        std::string file = loc.str("file");
        if (file.empty()) file = loc.at("expansionLoc").str("file");
        if (!file.empty()) current_file_ = file;
        const long long line = loc.at("line").as_int(current_line_);
        if (line) current_line_ = line;
    }

    bool in_scope() const {
        if (current_file_.empty()) return false;
        const std::string path = normalize_path(current_file_);
        if (path.find("/stdstub/") != std::string::npos) return false;
        if (roots_.empty()) return true;
        return std::any_of(roots_.begin(), roots_.end(), [&](const std::string& r) {
            return path.find(r) != std::string::npos;
        });
    }

    static bool is_implicit(const Json& node) { return node.flag("isImplicit"); }

    void walk(const Json& node, const std::string& prefix) {
        track_file(node);
        const std::string kind = node.str("kind");

        if (kind == "NamespaceDecl") {
            const std::string name = node.str("name");
            const std::string inner_prefix =
                name.empty() ? prefix : prefix + name + "::";
            for (const auto& child : node.at("inner").items()) walk(child, inner_prefix);
            return;
        }
        if (kind == "LinkageSpecDecl" || kind == "ExportDecl") {
            for (const auto& child : node.at("inner").items()) walk(child, prefix);
            return;
        }
        if (kind == "TranslationUnitDecl") {
            for (const auto& child : node.at("inner").items()) walk(child, prefix);
            return;
        }
        if (kind == "ClassTemplateDecl") {
            read_class_template(node, prefix);
            return;
        }
        if (kind == "CXXRecordDecl" || kind == "ClassTemplateSpecializationDecl" ||
            kind == "ClassTemplatePartialSpecializationDecl") {
            read_record(node, prefix, {});
            return;
        }
        if (kind == "EnumDecl") {
            read_enum(node, prefix);
            return;
        }
        // Anything else may still contain declarations under it.
        for (const auto& child : node.at("inner").items()) walk(child, prefix);
    }

    void read_class_template(const Json& node, const std::string& prefix) {
        track_file(node);
        std::vector<std::string> params;
        const Json* pattern = nullptr;
        for (const auto& child : node.at("inner").items()) {
            const std::string kind = child.str("kind");
            if (kind == "TemplateTypeParmDecl") {
                params.push_back((child.str("tagUsed", "typename")) + " " +
                                 child.str("name", "T"));
            } else if (kind == "NonTypeTemplateParmDecl") {
                params.push_back(child.at("type").str("qualType") + " " + child.str("name"));
            } else if (kind == "CXXRecordDecl" && !pattern) {
                pattern = &child;
            }
        }
        if (pattern) read_record(*pattern, prefix, params);

        // Explicit specialisations sit next to the pattern and are separate
        // classifiers realising the general template.
        for (const auto& child : node.at("inner").items()) {
            const std::string kind = child.str("kind");
            if (kind == "ClassTemplateSpecializationDecl" ||
                kind == "ClassTemplatePartialSpecializationDecl") {
                read_record(child, prefix, {});
            }
        }
    }

    static std::string specialization_suffix(const Json& node) {
        std::string args;
        for (const auto& child : node.at("inner").items()) {
            if (child.str("kind") != "TemplateArgument") continue;
            const std::string text = child.at("type").str("qualType");
            if (text.empty()) continue;
            if (!args.empty()) args += ", ";
            args += text;
        }
        return args.empty() ? std::string() : "<" + args + ">";
    }

    void read_record(const Json& node, const std::string& prefix,
                     const std::vector<std::string>& template_params) {
        track_file(node);
        if (is_implicit(node) && options_.skip_implicit) return;
        if (!node.flag("completeDefinition")) return;
        if (!in_scope()) return;

        const std::string name = node.str("name");
        if (name.empty()) return;  // anonymous struct, not a UML classifier

        const std::string kind = node.str("kind");
        const bool is_specialization = kind == "ClassTemplateSpecializationDecl" ||
                                       kind == "ClassTemplatePartialSpecializationDecl";
        const std::string suffix = is_specialization ? specialization_suffix(node) : std::string();

        Classifier c;
        c.name = name + suffix;
        c.qualified_name = prefix + c.name;
        c.template_parameters = template_params;
        c.kind = !template_params.empty()
                     ? ClassifierKind::Template
                     : (node.str("tagUsed") == "struct" ? ClassifierKind::Struct
                                                        : ClassifierKind::Class);
        c.source_file = current_file_;
        c.source_line = static_cast<int>(current_line_);
        c.is_abstract = node.at("definitionData").flag("isAbstract");
        if (kind == "ClassTemplatePartialSpecializationDecl") {
            c.tags.push_back(TaggedValue{"constraint", "partial specialization"});
        }
        if (is_specialization) {
            pending_realizations_.emplace_back(c.qualified_name, prefix + name);
        }

        // Access defaults to private for `class` and public for `struct`,
        // then follows every access specifier in declaration order.
        Visibility access = node.str("tagUsed") == "struct" ? Visibility::Public
                                                            : Visibility::Private;

        const std::string inner_prefix = c.qualified_name + "::";
        std::vector<const Json*> nested;
        for (const auto& child : node.at("inner").items()) {
            track_file(child);
            const std::string child_kind = child.str("kind");
            if (child_kind == "AccessSpecDecl") {
                access = visibility_from_string(child.str("access", "private"));
                continue;
            }
            if (is_implicit(child) && options_.skip_implicit) continue;
            if (options_.skip_private && access == Visibility::Private) continue;

            if (child_kind == "FieldDecl") {
                read_field(child, access, c);
            } else if (child_kind == "VarDecl" && child.str("storageClass") == "static") {
                read_static_member(child, access, c);
            } else if (child_kind == "CXXMethodDecl" || child_kind == "CXXConstructorDecl" ||
                       child_kind == "CXXDestructorDecl" || child_kind == "CXXConversionDecl") {
                read_method(child, access, c);
            } else if (child_kind == "FunctionTemplateDecl") {
                for (const auto& gc : child.at("inner").items()) {
                    if (gc.str("kind") == "CXXMethodDecl") read_method(gc, access, c);
                }
            } else if (child_kind == "CXXRecordDecl" || child_kind == "EnumDecl" ||
                       child_kind == "ClassTemplateDecl") {
                nested.push_back(&child);
            }
        }

        for (const auto& base : node.at("bases").items()) {
            Relation r;
            r.from = c.qualified_name;
            r.to = strip_cv(base.at("type").str("qualType"));
            r.kind = RelationKind::Generalization;
            r.is_virtual_base = base.flag("isVirtual");
            r.visibility = visibility_from_string(base.str("access", "public"));
            r.multiplicity = "";
            pending_relations_.push_back(r);
        }

        promote_annotations(c);
        // The abstractness flag is not always present in the dump, so a pure
        // operation is taken as sufficient evidence on its own.
        if (!c.is_abstract) {
            c.is_abstract = std::any_of(c.operations.begin(), c.operations.end(),
                                        [](const Operation& o) { return o.is_pure; });
        }
        model_.classifiers.push_back(std::move(c));

        for (const Json* child : nested) walk(*child, inner_prefix);
    }

    void read_enum(const Json& node, const std::string& prefix) {
        track_file(node);
        if (is_implicit(node) && options_.skip_implicit) return;
        if (!in_scope()) return;
        const std::string name = node.str("name");
        if (name.empty()) return;

        Classifier c;
        c.name = name;
        c.qualified_name = prefix + name;
        c.kind = ClassifierKind::Enumeration;
        c.source_file = current_file_;
        c.source_line = static_cast<int>(current_line_);
        for (const auto& child : node.at("inner").items()) {
            if (child.str("kind") == "EnumConstantDecl") c.enumerators.push_back(child.str("name"));
        }
        model_.classifiers.push_back(std::move(c));
    }

    static std::string written_type(const Json& node) {
        const Json& type = node.at("type");
        const std::string desugared = type.str("desugaredQualType");
        return desugared.empty() ? type.str("qualType") : desugared;
    }

    void read_field(const Json& node, Visibility access, Classifier& c) {
        Attribute a;
        a.name = node.str("name");
        a.type = written_type(node);
        a.visibility = access;
        a.scope = Scope::Instance;
        if (a.name.empty()) return;
        pending_members_.push_back(PendingMember{c.qualified_name, a.name, a.type});
        c.attributes.push_back(std::move(a));
    }

    // A `static constexpr` member is either a real classifier-scoped attribute
    // or one of the bp_ annotations. Both are read here; promote_annotations
    // then moves the annotations out of the attribute list.
    void read_static_member(const Json& node, Visibility access, Classifier& c) {
        Attribute a;
        a.name = node.str("name");
        a.type = written_type(node);
        a.visibility = access;
        a.scope = Scope::Classifier;
        if (a.name.empty()) return;
        if (starts_with(a.name, "bp_")) {
            a.tags.push_back(TaggedValue{"literal", literal_of(node)});
        } else {
            pending_members_.push_back(PendingMember{c.qualified_name, a.name, a.type});
        }
        c.attributes.push_back(std::move(a));
    }

    // Descends to the first literal under an initialiser. Clang wraps a string
    // literal in an ImplicitCastExpr, so a direct child lookup is not enough.
    static std::string literal_of(const Json& node) {
        const std::string kind = node.str("kind");
        if (kind == "StringLiteral") {
            std::string v = node.at("value").as_string();
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                v = v.substr(1, v.size() - 2);
            }
            return v;
        }
        if (kind == "CXXBoolLiteralExpr") return node.at("value").as_bool() ? "true" : "false";
        if (kind == "IntegerLiteral" || kind == "FloatingLiteral") {
            const Json& v = node.at("value");
            return v.is_string() ? v.as_string() : std::to_string(v.as_int());
        }
        for (const auto& child : node.at("inner").items()) {
            const std::string found = literal_of(child);
            if (!found.empty()) return found;
        }
        return {};
    }

    void read_method(const Json& node, Visibility access, Classifier& c) {
        Operation op;
        op.name = node.str("name");
        if (op.name.empty()) return;
        op.visibility = access;
        op.scope = node.str("storageClass") == "static" ? Scope::Classifier : Scope::Instance;
        op.is_virtual = node.flag("virtual");
        op.is_pure = node.flag("pure");

        const std::string signature = node.at("type").str("qualType");
        op.is_const = signature.find(") const") != std::string::npos;
        const std::size_t open = signature.find('(');
        std::string ret = open == std::string::npos ? signature : trim(signature.substr(0, open));
        const std::string kind = node.str("kind");
        if (kind == "CXXConstructorDecl" || kind == "CXXDestructorDecl") ret.clear();
        op.return_type = ret;

        for (const auto& child : node.at("inner").items()) {
            if (child.str("kind") != "ParmVarDecl") continue;
            op.parameters.push_back(Parameter{child.str("name"), written_type(child)});
        }
        pending_signatures_.push_back(PendingSignature{c.qualified_name, op});
        c.operations.push_back(std::move(op));
    }

    // Moves the bp_ annotations out of the attribute list and onto the
    // classifier and its attributes as UML stereotypes and tagged values.
    static void promote_annotations(Classifier& c) {
        std::vector<Attribute> kept;
        std::vector<std::pair<std::string, std::string>> field_tags;  // field, tag=value
        for (auto& a : c.attributes) {
            if (!starts_with(a.name, "bp_")) { kept.push_back(std::move(a)); continue; }
            const std::string value = a.tags.empty() ? std::string() : a.tags.front().value;
            const std::string key = a.name.substr(3);
            if (key == "table") {
                c.stereotypes.push_back("persistent");
                c.tags.push_back(TaggedValue{"table", value});
            } else if (key == "inheritance") {
                c.tags.push_back(TaggedValue{"inheritance", value});
            } else if (starts_with(key, "pk_")) {
                field_tags.emplace_back(key.substr(3), "pk=" + value);
            } else if (starts_with(key, "col_")) {
                field_tags.emplace_back(key.substr(4), "column-type=" + value);
            } else if (starts_with(key, "name_")) {
                field_tags.emplace_back(key.substr(5), "column=" + value);
            } else {
                c.tags.push_back(TaggedValue{key, value});
            }
        }
        c.attributes = std::move(kept);
        for (const auto& [field, tag] : field_tags) {
            const std::size_t eq = tag.find('=');
            for (auto& a : c.attributes) {
                if (a.name != field) continue;
                a.tags.push_back(TaggedValue{tag.substr(0, eq), tag.substr(eq + 1)});
            }
        }
    }

    // ------------------------------------------------------------------
    // Second pass. Names become relations only once every classifier is
    // known, because a member can name a class declared further down.
    // ------------------------------------------------------------------
    void resolve_relations() {
        std::vector<std::string> known;
        known.reserve(model_.classifiers.size());
        for (const auto& c : model_.classifiers) known.push_back(c.qualified_name);

        for (const auto& r : pending_relations_) {
            Relation resolved = r;
            const std::string target = resolve_name(r.to, known);
            if (target.empty()) continue;  // a base outside the model, e.g. a library type
            resolved.to = target;
            model_.relations.push_back(resolved);
        }
        for (const auto& [special, general] : pending_realizations_) {
            const std::string target = resolve_name(general, known);
            if (target.empty() || target == special) continue;
            Relation r;
            r.from = special;
            r.to = target;
            r.kind = RelationKind::Realization;
            r.multiplicity = "";
            model_.relations.push_back(r);
        }

        std::set<std::string> member_pairs;
        for (const auto& m : pending_members_) {
            const MemberClassification info = classify_member_type(m.type, known);
            if (!info.is_relation || info.target == m.owner) continue;
            Relation r;
            r.from = m.owner;
            r.to = info.target;
            r.kind = info.kind;
            r.label = m.name;
            r.multiplicity = info.multiplicity;
            model_.relations.push_back(r);
            member_pairs.insert(m.owner + "->" + info.target);
            if (Classifier* owner = model_.find(m.owner)) {
                for (auto& a : owner->attributes) {
                    if (a.name == m.name) a.multiplicity = info.multiplicity;
                }
            }
        }

        // A type that appears only in a signature is a dependency, not a
        // member, and is reported only when no stronger edge already exists.
        for (const auto& s : pending_signatures_) {
            std::vector<std::string> types;
            types.push_back(s.op.return_type);
            for (const auto& p : s.op.parameters) types.push_back(p.type);
            for (const auto& t : types) {
                const MemberClassification info = classify_member_type(t, known);
                if (!info.is_relation || info.target == s.owner) continue;
                if (member_pairs.count(s.owner + "->" + info.target)) continue;
                Relation r;
                r.from = s.owner;
                r.to = info.target;
                r.kind = RelationKind::Dependency;
                r.multiplicity = "";
                model_.relations.push_back(r);
            }
        }
    }

    // UML calls a classifier with no attributes and only abstract operations
    // an interface. That is a reading of the model, not of the source, so it
    // happens here rather than in the traversal.
    void classify_interfaces() {
        for (auto& c : model_.classifiers) {
            if (c.kind != ClassifierKind::Class && c.kind != ClassifierKind::Struct) continue;
            if (!c.attributes.empty() || c.operations.empty()) continue;
            const bool all_pure = std::all_of(
                c.operations.begin(), c.operations.end(), [](const Operation& o) {
                    // A virtual destructor does not stop a class being an interface.
                    return o.is_pure || (o.is_virtual && o.name.front() == '~');
                });
            if (all_pure) c.kind = ClassifierKind::Interface;
        }
    }

    struct PendingMember {
        std::string owner;
        std::string name;
        std::string type;
    };
    struct PendingSignature {
        std::string owner;
        Operation op;
    };

    ReaderOptions options_;
    std::vector<std::string> roots_;
    std::string current_file_;
    long long current_line_ = 0;
    Model model_;
    std::vector<Relation> pending_relations_;
    std::vector<std::pair<std::string, std::string>> pending_realizations_;
    std::vector<PendingMember> pending_members_;
    std::vector<PendingSignature> pending_signatures_;
};

} // namespace

Model read_ast_json(const Json& ast, const ReaderOptions& options) {
    return AstWalker(options).run(ast);
}

// ---------------------------------------------------------------------------
// Driving the compiler.
// ---------------------------------------------------------------------------
namespace {

std::string quote(const std::string& s) { return "\"" + s + "\""; }

int run_command(const std::string& command) {
#ifdef _WIN32
    // std::system on Windows goes through cmd.exe, which strips the outermost
    // pair of quotes from the whole line. A command whose first token is a
    // quoted path with spaces therefore needs an extra pair around everything.
    const std::string wrapped = "\"" + command + "\"";
#else
    const std::string wrapped = command;
#endif
    return std::system(wrapped.c_str());
}

} // namespace

std::string build_clang_command(const ClangInvocation& inv, const std::string& source,
                                const std::string& json_out, const std::string& diag_out) {
    std::string cmd = quote(inv.clang);
    cmd += " -std=" + inv.standard;
    if (!inv.target.empty()) cmd += " --target=" + inv.target;
    if (!inv.stdstub_dir.empty()) {
        // The replacement headers are the reason the dump stays small enough
        // to parse: with the real standard library the JSON of a single
        // translation unit runs to hundreds of megabytes of library internals
        // that no class diagram would ever show.
        cmd += " -nostdinc++ -isystem " + quote(inv.stdstub_dir);
    }
    for (const auto& dir : inv.include_dirs) cmd += " -I" + quote(dir);
    for (const auto& flag : inv.extra_flags) cmd += " " + flag;
    cmd += " -Xclang -ast-dump=json -fsyntax-only ";
    cmd += quote(source);
    cmd += " > " + quote(json_out);
    cmd += " 2> " + quote(diag_out);
    return cmd;
}

bool clang_available(const ClangInvocation& inv) {
    const std::filesystem::path probe =
        std::filesystem::temp_directory_path() / "blueprint_clang_probe.txt";
    const std::string cmd = quote(inv.clang) + " --version > " + quote(probe.string()) + " 2>&1";
    const int rc = run_command(cmd);
    std::error_code ec;
    std::filesystem::remove(probe, ec);
    return rc == 0;
}

Json dump_ast(const ClangInvocation& inv, const std::string& source,
              std::size_t* raw_bytes) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::filesystem::path json_out = dir / "blueprint_ast.json";
    const std::filesystem::path diag_out = dir / "blueprint_ast.err";
    const std::string cmd =
        build_clang_command(inv, source, json_out.string(), diag_out.string());
    const int rc = run_command(cmd);

    std::string diagnostics;
    try { diagnostics = read_file(diag_out.string()); } catch (const std::exception&) {}
    if (rc != 0) {
        throw std::runtime_error("clang failed on " + source + "\n" + diagnostics);
    }
    const std::string text = read_file(json_out.string());
    if (text.empty()) {
        throw std::runtime_error("clang produced no AST for " + source + "\n" + diagnostics);
    }
    if (raw_bytes) *raw_bytes = text.size();
    Json ast = Json::parse(text);
    std::error_code ec;
    std::filesystem::remove(json_out, ec);
    std::filesystem::remove(diag_out, ec);
    return ast;
}

} // namespace bp
