#include "blueprint/diagram.hpp"

#include <algorithm>
#include <cctype>
#include <map>
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

bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

// Splits on commas outside brackets, which is what a parameter list needs.
std::vector<std::string> split_top_level(const std::string& s, char sep) {
    std::vector<std::string> out;
    int depth = 0;
    std::string current;
    for (const char c : s) {
        if (c == '<' || c == '(' || c == '[' || c == '~') ++depth;
        if (c == '>' || c == ')' || c == ']') --depth;
        if (c == sep && depth <= 0) {
            out.push_back(trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!trim(current).empty()) out.push_back(trim(current));
    return out;
}

std::string untilde(std::string t) {
    // Mermaid writes generics with tildes; the model keeps C++ spelling.
    bool open = true;
    for (char& c : t) {
        if (c != '~') continue;
        c = open ? '<' : '>';
        open = !open;
    }
    return t;
}

// Pulls a trailing " [mult]" off a member line.
std::string take_multiplicity(std::string& text) {
    const std::string trimmed = trim(text);
    if (!ends_with(trimmed, "]")) return "1";
    const std::size_t open = trimmed.rfind('[');
    if (open == std::string::npos) return "1";
    const std::string mult = trimmed.substr(open + 1, trimmed.size() - open - 2);
    text = trim(trimmed.substr(0, open));
    return mult.empty() ? "1" : mult;
}

Visibility visibility_of(char c) {
    switch (c) {
    case '+': return Visibility::Public;
    case '#': return Visibility::Protected;
    case '-': return Visibility::Private;
    case '~': return Visibility::Package;
    default: return Visibility::Public;
    }
}

bool is_visibility_char(char c) {
    return c == '+' || c == '#' || c == '-' || c == '~';
}

// Splits "unsigned int count" into type "unsigned int" and name "count".
void split_type_and_name(const std::string& text, std::string& type, std::string& name) {
    const std::string t = trim(text);
    const std::size_t space = t.rfind(' ');
    if (space == std::string::npos) { type = t; name.clear(); return; }
    type = trim(t.substr(0, space));
    name = trim(t.substr(space + 1));
}

// A parameter may have no name, which Mermaid writes as a bare type. The two
// cases are told apart by the shape of the text: a declaration that ends in a
// reference or pointer token, or that has no space at all, is all type. The
// rule is a heuristic and it is stated as one; it misreads an unnamed
// parameter of a multi-word value type such as "unsigned int", which does not
// occur in a declaration Blueprint has produced.
void split_parameter(const std::string& text, std::string& type, std::string& name) {
    const std::string t = trim(text);
    if (t.empty() || t.back() == '&' || t.back() == '*' ||
        t.find(' ') == std::string::npos) {
        type = t;
        name.clear();
        return;
    }
    split_type_and_name(t, type, name);
}

// ---------------------------------------------------------------------------
// The parser. One line scanner, two member syntaxes.
// ---------------------------------------------------------------------------
class DiagramParser {
public:
    DiagramParser(Notation notation) : notation_(notation) {}

    Model parse(const std::string& text) {
        for (const std::string& raw : lines_of(text)) {
            const std::string line = trim(raw);
            if (line.empty()) continue;
            if (starts_with(line, "@") || starts_with(line, "'") || starts_with(line, "%%")) {
                take_title(line);
                continue;
            }
            if (line == "classDiagram" || starts_with(line, "direction ") ||
                starts_with(line, "skinparam") || starts_with(line, "hide ")) {
                continue;
            }
            if (starts_with(line, "title ")) { model_.name = trim(line.substr(6)); continue; }
            if (starts_with(line, "note ")) { take_note(line); continue; }
            if (line == "}") { close_classifier(); continue; }
            if (open_classifier(line)) continue;
            if (current_ != kNoClassifier) { member_line(line); continue; }
            relation_line(line);
        }
        finish();
        model_.normalize();
        return std::move(model_);
    }

private:
    void take_title(const std::string& line) {
        const std::size_t marker = line.find("title:");
        if (marker != std::string::npos) model_.name = trim(line.substr(marker + 6));
    }

    // Both notations name a classifier with a quoted label and an identifier.
    bool open_classifier(const std::string& line) {
        if (!ends_with(line, "{")) return false;
        const std::size_t first_quote = line.find('"');
        const std::size_t last_quote = line.rfind('"');
        if (first_quote == std::string::npos || last_quote == first_quote) return false;

        Classifier c;
        c.qualified_name = line.substr(first_quote + 1, last_quote - first_quote - 1);
        const std::size_t colons = c.qualified_name.rfind("::");
        c.name = colons == std::string::npos ? c.qualified_name : c.qualified_name.substr(colons + 2);

        std::string alias;
        if (notation_ == Notation::PlantUml) {
            const std::size_t as_pos = line.find(" as ", last_quote);
            alias = as_pos == std::string::npos
                        ? c.qualified_name
                        : trim(line.substr(as_pos + 4, line.find_first_of(" <{", as_pos + 4) -
                                                            (as_pos + 4)));
            c.kind = starts_with(line, "interface")  ? ClassifierKind::Interface
                     : starts_with(line, "enum")     ? ClassifierKind::Enumeration
                                                     : ClassifierKind::Class;
            // An interface is abstract by definition, so neither notation
            // marks it as such and the reader has to restore the fact.
            c.is_abstract = starts_with(line, "abstract") || c.kind == ClassifierKind::Interface;
            for (const std::string& s : stereotypes_of(line)) apply_stereotype(c, s);
        } else {
            const std::size_t bracket = line.find('[');
            const std::size_t class_kw = line.find("class ");
            if (class_kw == std::string::npos || bracket == std::string::npos) return false;
            alias = trim(line.substr(class_kw + 6, bracket - class_kw - 6));
        }
        alias_to_name_[alias] = c.qualified_name;
        classifiers_.push_back(std::move(c));
        current_ = classifiers_.size() - 1;
        return true;
    }

    static std::vector<std::string> stereotypes_of(const std::string& line) {
        std::vector<std::string> out;
        std::size_t pos = 0;
        while ((pos = line.find("<<", pos)) != std::string::npos) {
            const std::size_t end = line.find(">>", pos);
            if (end == std::string::npos) break;
            out.push_back(trim(line.substr(pos + 2, end - pos - 2)));
            pos = end + 2;
        }
        return out;
    }

    static void apply_stereotype(Classifier& c, const std::string& s) {
        if (s == "struct") { c.kind = ClassifierKind::Struct; return; }
        if (s == "template") { c.kind = ClassifierKind::Template; return; }
        if (s == "interface") { c.kind = ClassifierKind::Interface; c.is_abstract = true; return; }
        if (s == "enumeration") { c.kind = ClassifierKind::Enumeration; return; }
        if (s == "abstract") { c.is_abstract = true; return; }
        c.stereotypes.push_back(s);
    }

    void close_classifier() { current_ = kNoClassifier; }

    void member_line(const std::string& line) {
        if (starts_with(line, "<<")) {
            for (const std::string& s : stereotypes_of(line)) apply_stereotype(current(), s);
            return;
        }
        if (!is_visibility_char(line.front())) {
            current().enumerators.push_back(line);
            return;
        }
        const Visibility vis = visibility_of(line.front());
        std::string body = trim(line.substr(1));
        if (body.find('(') != std::string::npos) {
            operation_line(body, vis);
        } else {
            attribute_line(body, vis);
        }
    }

    void attribute_line(std::string body, Visibility vis) {
        Attribute a;
        a.visibility = vis;
        a.multiplicity = take_multiplicity(body);
        if (notation_ == Notation::PlantUml) {
            if (starts_with(body, "{static}")) { a.scope = Scope::Classifier; body = trim(body.substr(8)); }
            const std::size_t sep = body.find(" : ");
            if (sep == std::string::npos) return;
            a.name = trim(body.substr(0, sep));
            a.type = trim(body.substr(sep + 3));
        } else {
            if (ends_with(body, "$")) { a.scope = Scope::Classifier; body = trim(body.substr(0, body.size() - 1)); }
            split_type_and_name(body, a.type, a.name);
            a.type = untilde(a.type);
        }
        if (a.name.empty()) return;
        current().attributes.push_back(std::move(a));
    }

    void operation_line(std::string body, Visibility vis) {
        Operation op;
        op.visibility = vis;
        if (notation_ == Notation::PlantUml) {
            while (true) {
                if (starts_with(body, "{static}")) { op.scope = Scope::Classifier; body = trim(body.substr(8)); continue; }
                if (starts_with(body, "{abstract}")) { op.is_pure = op.is_virtual = true; body = trim(body.substr(10)); continue; }
                if (starts_with(body, "{virtual}")) { op.is_virtual = true; body = trim(body.substr(9)); continue; }
                break;
            }
        }
        const std::size_t open = body.find('(');
        const std::size_t close = body.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open) return;
        op.name = trim(body.substr(0, open));
        const std::string params = body.substr(open + 1, close - open - 1);
        std::string tail = trim(body.substr(close + 1));

        if (notation_ == Notation::Mermaid) {
            if (starts_with(tail, "*")) { op.is_pure = op.is_virtual = true; tail = trim(tail.substr(1)); }
            else if (starts_with(tail, "$")) { op.scope = Scope::Classifier; tail = trim(tail.substr(1)); }
        }
        if (starts_with(tail, "const")) { op.is_const = true; tail = trim(tail.substr(5)); }
        if (notation_ == Notation::PlantUml) {
            if (starts_with(tail, ":")) op.return_type = trim(tail.substr(1));
        } else {
            op.return_type = untilde(tail);
        }

        for (const std::string& p : split_top_level(params, ',')) {
            Parameter parameter;
            if (notation_ == Notation::PlantUml) {
                const std::size_t sep = p.find(" : ");
                if (sep == std::string::npos) { parameter.type = p; }
                else { parameter.name = trim(p.substr(0, sep)); parameter.type = trim(p.substr(sep + 3)); }
            } else {
                split_parameter(p, parameter.type, parameter.name);
                parameter.type = untilde(parameter.type);
            }
            op.parameters.push_back(std::move(parameter));
        }
        current().operations.push_back(std::move(op));
    }

    void take_note(const std::string& line) {
        const std::size_t marker = line.find("bp:");
        if (marker == std::string::npos) return;
        std::string alias;
        if (starts_with(line, "note for ")) {
            alias = trim(line.substr(9, line.find('"', 9) - 9));
        } else {
            const std::size_t of_pos = line.find(" of ");
            if (of_pos == std::string::npos) return;
            alias = trim(line.substr(of_pos + 4, line.find(" :", of_pos) - (of_pos + 4)));
        }
        std::string payload = trim(line.substr(marker + 3));
        if (ends_with(payload, "\"")) payload.pop_back();
        notes_[alias] = trim(payload);
    }

    void relation_line(const std::string& line) {
        struct Arrow { const char* text; RelationKind kind; bool reversed; };
        // Order matters: "<|--" has to be tried before "--".
        static const Arrow arrows[] = {
            {" <|.. ", RelationKind::Realization, true},
            {" <|-- ", RelationKind::Generalization, true},
            {" ..> ", RelationKind::Dependency, false},
            {" *--", RelationKind::Composition, false},
            {" o--", RelationKind::Aggregation, false},
            {" -->", RelationKind::Association, false},
        };
        for (const Arrow& arrow : arrows) {
            const std::size_t pos = line.find(arrow.text);
            if (pos == std::string::npos) continue;
            Relation r;
            r.kind = arrow.kind;
            const std::string left = trim(line.substr(0, pos));
            std::string right = trim(line.substr(pos + std::string(arrow.text).size()));

            const std::size_t colon = right.find(" : ");
            std::string label;
            if (colon != std::string::npos) {
                label = trim(right.substr(colon + 3));
                right = trim(right.substr(0, colon));
            }
            if (starts_with(right, "\"")) {
                const std::size_t end = right.find('"', 1);
                r.multiplicity = right.substr(1, end - 1);
                right = trim(right.substr(end + 1));
            } else {
                r.multiplicity = arrow.kind == RelationKind::Generalization ||
                                 arrow.kind == RelationKind::Realization ||
                                 arrow.kind == RelationKind::Dependency
                                     ? ""
                                     : "1";
            }
            if (arrow.reversed) { r.from = right; r.to = left; }
            else { r.from = left; r.to = right; }

            if (arrow.kind == RelationKind::Generalization && !label.empty()) {
                if (label.find("virtual") != std::string::npos) r.is_virtual_base = true;
                for (const char* v : {"private", "protected", "public"}) {
                    if (label.find(v) != std::string::npos) r.visibility = visibility_from_string(v);
                }
            } else {
                r.label = label;
            }
            relations_.push_back(std::move(r));
            return;
        }
    }

    // Aliases become qualified names, and the notes are unpacked, only once
    // the whole file has been read: a relation may name a class declared later.
    void finish() {
        const auto resolve = [this](const std::string& alias) {
            const auto it = alias_to_name_.find(alias);
            return it == alias_to_name_.end() ? alias : it->second;
        };
        model_.classifiers = std::move(classifiers_);
        for (auto& r : relations_) {
            r.from = resolve(r.from);
            r.to = resolve(r.to);
        }
        model_.relations = std::move(relations_);

        for (const auto& [alias, payload] : notes_) {
            Classifier* c = model_.find(resolve(alias));
            if (!c) continue;
            for (const std::string& entry : split_top_level(payload, ';')) {
                const std::size_t eq = entry.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = trim(entry.substr(0, eq));
                const std::string value = trim(entry.substr(eq + 1));
                if (key == "template") {
                    for (const std::string& p : split_top_level(value, ',')) {
                        c->template_parameters.push_back(p);
                    }
                    continue;
                }
                const std::size_t dot = key.find('.');
                if (dot == std::string::npos) {
                    c->tags.push_back(TaggedValue{key, value});
                    continue;
                }
                const std::string field = key.substr(0, dot);
                for (auto& a : c->attributes) {
                    if (a.name == field) a.tags.push_back(TaggedValue{key.substr(dot + 1), value});
                }
            }
        }

        // The multiplicity of a member lives on the relation in a diagram, so
        // it is copied back onto the attribute that produced the relation.
        for (const auto& r : model_.relations) {
            if (r.label.empty() || r.multiplicity.empty()) continue;
            Classifier* c = model_.find(r.from);
            if (!c) continue;
            for (auto& a : c->attributes) {
                if (a.name == r.label) a.multiplicity = r.multiplicity;
            }
        }
    }

    static constexpr std::size_t kNoClassifier = static_cast<std::size_t>(-1);
    Classifier& current() { return classifiers_[current_]; }

    Notation notation_;
    Model model_;
    std::vector<Classifier> classifiers_;
    std::vector<Relation> relations_;
    std::map<std::string, std::string> alias_to_name_;
    std::map<std::string, std::string> notes_;
    std::size_t current_ = kNoClassifier;
};

} // namespace

Notation detect_notation(const std::string& text) {
    if (text.find("@startuml") != std::string::npos) return Notation::PlantUml;
    if (text.find("classDiagram") != std::string::npos) return Notation::Mermaid;
    return Notation::PlantUml;
}

Model read_class_diagram(const std::string& text) {
    return read_class_diagram(text, detect_notation(text));
}

Model read_class_diagram(const std::string& text, Notation notation) {
    return DiagramParser(notation).parse(text);
}

} // namespace bp
