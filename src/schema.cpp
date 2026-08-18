#include "blueprint/schema.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace bp {
namespace {

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

std::string last_component(const std::string& qualified) {
    const std::size_t pos = qualified.rfind("::");
    return pos == std::string::npos ? qualified : qualified.substr(pos + 2);
}

// A trailing underscore is the usual C++ marker for a data member and has no
// business in a column name.
std::string column_name_of(const Attribute& a) {
    for (const auto& t : a.tags) {
        if (t.name == "column") return t.value;
    }
    std::string name = a.name;
    while (!name.empty() && name.back() == '_') name.pop_back();
    return lower(name);
}

std::string table_name_of(const Classifier& c) {
    if (const TaggedValue* t = c.tag("table")) return t->value;
    return lower(last_component(c.qualified_name)) + "s";
}

bool is_persistent(const Classifier& c) { return c.has_stereotype("persistent"); }

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

} // namespace

std::string to_string(InheritanceStrategy s) {
    switch (s) {
    case InheritanceStrategy::SingleTable: return "single-table";
    case InheritanceStrategy::TablePerClass: return "table-per-class";
    case InheritanceStrategy::TablePerConcrete: return "table-per-concrete";
    }
    return "single-table";
}

InheritanceStrategy inheritance_strategy_from_string(const std::string& s) {
    if (s == "table-per-class") return InheritanceStrategy::TablePerClass;
    if (s == "table-per-concrete") return InheritanceStrategy::TablePerConcrete;
    return InheritanceStrategy::SingleTable;
}

const Column* Table::column(const std::string& n) const {
    for (const auto& c : columns) {
        if (c.name == n) return &c;
    }
    return nullptr;
}

const Table* Schema::table(const std::string& n) const {
    for (const auto& t : tables) {
        if (t.name == n) return &t;
    }
    return nullptr;
}

// A deliberately small table. Anything not listed is not a scalar the tool
// knows how to store, and the caller is told so rather than guessed at.
std::string cpp_type_to_sql(const std::string& cpp_type) {
    static const std::map<std::string, std::string> mapping = {
        {"bool", "BOOLEAN"},
        {"char", "CHAR(1)"},
        {"short", "SMALLINT"},
        {"int", "INTEGER"},
        {"unsigned int", "INTEGER"},
        {"unsigned", "INTEGER"},
        {"long", "BIGINT"},
        {"long long", "BIGINT"},
        {"unsigned long", "BIGINT"},
        {"unsigned long long", "BIGINT"},
        {"std::size_t", "BIGINT"},
        {"size_t", "BIGINT"},
        {"float", "REAL"},
        {"double", "DOUBLE PRECISION"},
        {"long double", "DOUBLE PRECISION"},
        {"std::string", "TEXT"},
        {"std::string_view", "TEXT"},
        {"const char *", "TEXT"},
        {"const char*", "TEXT"},
    };
    const auto it = mapping.find(trim(cpp_type));
    return it == mapping.end() ? std::string() : it->second;
}

std::string sql_type_to_cpp(const std::string& sql_type) {
    const std::string t = lower(trim(sql_type));
    if (starts_with(t, "boolean")) return "bool";
    if (starts_with(t, "smallint")) return "short";
    if (starts_with(t, "integer") || starts_with(t, "int")) return "int";
    if (starts_with(t, "bigint")) return "long long";
    if (starts_with(t, "real")) return "float";
    if (starts_with(t, "double")) return "double";
    if (starts_with(t, "char(1)")) return "char";
    return "std::string";
}

namespace {

// Resolves the strategy for one hierarchy: a tagged value on the root wins,
// otherwise the command line default applies.
InheritanceStrategy strategy_for(const Classifier& root, InheritanceStrategy fallback) {
    if (const TaggedValue* t = root.tag("inheritance")) {
        return inheritance_strategy_from_string(t->value);
    }
    return fallback;
}

struct Hierarchy {
    std::vector<std::string> bases_of(const std::string& qn) const {
        const auto it = parents.find(qn);
        return it == parents.end() ? std::vector<std::string>{} : it->second;
    }
    // Root of the inheritance tree that qn belongs to, itself when it has no base.
    std::string root_of(const std::string& qn) const {
        std::string current = qn;
        for (int guard = 0; guard < 64; ++guard) {
            const auto it = parents.find(current);
            if (it == parents.end() || it->second.empty()) return current;
            current = it->second.front();
        }
        return current;
    }
    std::map<std::string, std::vector<std::string>> parents;
    std::map<std::string, std::vector<std::string>> children;
};

Hierarchy build_hierarchy(const Model& model) {
    Hierarchy h;
    for (const auto& r : model.relations) {
        if (r.kind != RelationKind::Generalization) continue;
        h.parents[r.from].push_back(r.to);
        h.children[r.to].push_back(r.from);
    }
    return h;
}

void collect_descendants(const Hierarchy& h, const std::string& root,
                         std::vector<std::string>& out) {
    const auto it = h.children.find(root);
    if (it == h.children.end()) return;
    for (const auto& child : it->second) {
        out.push_back(child);
        collect_descendants(h, child, out);
    }
}

void collect_ancestors(const Hierarchy& h, const std::string& node,
                       std::vector<std::string>& out) {
    for (const auto& parent : h.bases_of(node)) {
        out.push_back(parent);
        collect_ancestors(h, parent, out);
    }
}

// Columns contributed by one class's own attributes.
std::vector<Column> columns_of(const Classifier& c, std::vector<std::string>& notes) {
    std::vector<Column> out;
    for (const auto& a : c.attributes) {
        if (a.scope == Scope::Classifier) continue;  // a class variable is not row state
        std::string sql;
        for (const auto& t : a.tags) {
            if (t.name == "column-type") sql = t.value;
        }
        if (sql.empty()) sql = cpp_type_to_sql(a.type);
        if (sql.empty()) continue;  // not a scalar; handled as a relation, or skipped
        Column col;
        col.name = column_name_of(a);
        col.type = sql;
        col.source_attribute = a.name;
        for (const auto& t : a.tags) {
            if (t.name == "pk" && t.value != "false") col.primary_key = true;
        }
        col.not_null = col.primary_key;
        out.push_back(std::move(col));
    }
    const bool has_pk = std::any_of(out.begin(), out.end(),
                                    [](const Column& c2) { return c2.primary_key; });
    if (!has_pk) {
        notes.push_back(c.qualified_name + ": no attribute is tagged as the primary key, " +
                        "a surrogate key column 'id' was added");
        Column key;
        key.name = "id";
        key.type = "INTEGER";
        key.primary_key = true;
        key.not_null = true;
        out.insert(out.begin(), std::move(key));
    }
    return out;
}

std::string primary_key_of(const Table& t) {
    for (const auto& c : t.columns) {
        if (c.primary_key) return c.name;
    }
    return "id";
}

} // namespace

SchemaReport model_to_schema(const Model& model, InheritanceStrategy fallback) {
    SchemaReport report;
    report.schema.name = model.name;
    report.schema.strategy = fallback;
    const Hierarchy hierarchy = build_hierarchy(model);

    std::set<std::string> persistent;
    for (const auto& c : model.classifiers) {
        if (is_persistent(c)) persistent.insert(c.qualified_name);
    }
    if (persistent.empty()) {
        report.notes.push_back(
            "no classifier carries the persistent stereotype, so no table was generated");
        return report;
    }

    // Which class owns a table depends on the strategy of its hierarchy.
    std::map<std::string, std::string> table_of_class;  // class -> table name

    for (const auto& c : model.classifiers) {
        if (!persistent.count(c.qualified_name)) continue;
        const std::string root_name = hierarchy.root_of(c.qualified_name);
        const Classifier* root = model.find(root_name);
        const InheritanceStrategy strategy = strategy_for(root ? *root : c, fallback);
        if (root_name == c.qualified_name) report.schema.strategy = strategy;

        const bool is_root = root_name == c.qualified_name;
        if (strategy == InheritanceStrategy::SingleTable && !is_root) continue;
        if (strategy == InheritanceStrategy::TablePerConcrete && c.is_abstract) {
            report.notes.push_back(c.qualified_name +
                                   ": abstract, and table-per-concrete gives it no table");
            continue;
        }

        Table t;
        t.name = table_name_of(c);
        t.source_classifier = c.qualified_name;
        t.columns = columns_of(c, report.notes);

        if (strategy == InheritanceStrategy::SingleTable) {
            std::vector<std::string> descendants;
            collect_descendants(hierarchy, c.qualified_name, descendants);
            if (!descendants.empty()) {
                Column discriminator;
                discriminator.name = "class_type";
                discriminator.type = "TEXT";
                discriminator.not_null = true;
                t.columns.push_back(std::move(discriminator));
            }
            for (const auto& d : descendants) {
                const Classifier* dc = model.find(d);
                if (!dc || !persistent.count(d)) continue;
                for (Column col : columns_of(*dc, report.notes)) {
                    if (col.primary_key) continue;  // the root already carries the key
                    if (t.column(col.name)) continue;
                    // Every subclass column has to be nullable: a row of one
                    // subclass leaves the other subclasses' columns empty.
                    col.not_null = false;
                    t.columns.push_back(std::move(col));
                }
                table_of_class[d] = t.name;
                report.notes.push_back(d + ": folded into " + t.name +
                                       " by the single-table strategy");
            }
        } else if (strategy == InheritanceStrategy::TablePerConcrete) {
            std::vector<std::string> ancestors;
            collect_ancestors(hierarchy, c.qualified_name, ancestors);
            for (const auto& a : ancestors) {
                const Classifier* ac = model.find(a);
                if (!ac) continue;
                for (Column col : columns_of(*ac, report.notes)) {
                    if (t.column(col.name)) continue;
                    if (col.primary_key && std::any_of(t.columns.begin(), t.columns.end(),
                                                       [](const Column& x) { return x.primary_key; })) {
                        continue;
                    }
                    t.columns.push_back(std::move(col));
                }
            }
        } else if (strategy == InheritanceStrategy::TablePerClass && !is_root) {
            // The child's key is also a foreign key onto the parent's row.
            for (const auto& base : hierarchy.bases_of(c.qualified_name)) {
                const Classifier* bc = model.find(base);
                if (!bc || !persistent.count(base)) continue;
                for (auto& col : t.columns) {
                    if (!col.primary_key) continue;
                    col.references_table = table_name_of(*bc);
                    col.references_column = "id";
                }
            }
        }

        table_of_class[c.qualified_name] = t.name;
        report.schema.tables.push_back(std::move(t));
    }

    // Fix up table-per-class parent references now that every table exists.
    for (auto& t : report.schema.tables) {
        for (auto& col : t.columns) {
            if (col.references_table.empty()) continue;
            const Table* target = report.schema.table(col.references_table);
            if (target) col.references_column = primary_key_of(*target);
        }
    }

    // Associations. One to one becomes a foreign key on the owning side, one
    // to many becomes a join table, because a diagram that shows only one end
    // does not say which side is the many.
    std::vector<Table> join_tables;
    for (const auto& r : model.relations) {
        if (r.kind == RelationKind::Generalization || r.kind == RelationKind::Realization ||
            r.kind == RelationKind::Dependency) {
            continue;
        }
        const auto from_it = table_of_class.find(r.from);
        const auto to_it = table_of_class.find(r.to);
        if (from_it == table_of_class.end() || to_it == table_of_class.end()) continue;

        Table* from_table = nullptr;
        for (auto& t : report.schema.tables) {
            if (t.name == from_it->second) from_table = &t;
        }
        const Table* to_table = report.schema.table(to_it->second);
        if (!from_table || !to_table) continue;

        const std::string base = r.label.empty() ? last_component(r.to) : r.label;
        std::string field = lower(base);
        while (!field.empty() && field.back() == '_') field.pop_back();

        if (r.multiplicity == "*") {
            Table join;
            join.name = from_table->name + "_" + field;
            join.is_join_table = true;
            join.source_classifier = r.from;
            Column left;
            left.name = lower(last_component(r.from)) + "_id";
            left.type = "INTEGER";
            left.not_null = true;
            left.references_table = from_table->name;
            left.references_column = primary_key_of(*from_table);
            Column right;
            right.name = field + "_id";
            right.type = "INTEGER";
            right.not_null = true;
            right.references_table = to_table->name;
            right.references_column = primary_key_of(*to_table);
            join.columns.push_back(std::move(left));
            join.columns.push_back(std::move(right));
            join_tables.push_back(std::move(join));
            report.notes.push_back(r.from + "." + r.label + ": multiplicity * mapped to join table " +
                                   from_table->name + "_" + field);
        } else {
            Column fk;
            fk.name = field + "_id";
            fk.type = "INTEGER";
            fk.not_null = r.multiplicity == "1";
            fk.references_table = to_table->name;
            fk.references_column = primary_key_of(*to_table);
            fk.source_attribute = r.label;
            if (!from_table->column(fk.name)) from_table->columns.push_back(std::move(fk));
        }
    }
    for (auto& t : join_tables) report.schema.tables.push_back(std::move(t));

    return report;
}

std::string schema_to_ddl(const Schema& schema) {
    std::ostringstream out;
    out << "-- generated by Blueprint from model '" << schema.name << "'\n";
    out << "-- inheritance strategy: " << to_string(schema.strategy) << "\n\n";
    for (const auto& t : schema.tables) {
        // The class name travels in a comment. Recovering a class name from a
        // table name would mean guessing an English plural, and a guess is
        // exactly what a round trip must not contain.
        if (!t.source_classifier.empty()) {
            out << "-- class: " << t.source_classifier << (t.is_join_table ? " (join)" : "") << "\n";
        }
        out << "CREATE TABLE " << t.name << " (\n";
        std::vector<std::string> lines;
        std::vector<std::string> keys;
        for (const auto& c : t.columns) {
            std::string line = "  " + c.name + " " + c.type;
            if (c.not_null) line += " NOT NULL";
            if (!c.source_attribute.empty() && c.source_attribute != c.name) {
                line += " -- attribute: " + c.source_attribute;
            }
            lines.push_back(std::move(line));
            if (c.primary_key) keys.push_back(c.name);
        }
        if (!keys.empty()) {
            std::string line = "  PRIMARY KEY (";
            for (std::size_t k = 0; k < keys.size(); ++k) {
                if (k) line += ", ";
                line += keys[k];
            }
            line += ")";
            lines.push_back(std::move(line));
        }
        for (const auto& c : t.columns) {
            if (c.references_table.empty()) continue;
            lines.push_back("  FOREIGN KEY (" + c.name + ") REFERENCES " + c.references_table +
                            "(" + c.references_column + ")");
        }
        for (std::size_t k = 0; k < lines.size(); ++k) {
            // A comment after a column has to come after the comma, or the
            // comma ends up commented out and the statement is malformed.
            const std::size_t comment = lines[k].find(" -- ");
            std::string body = comment == std::string::npos ? lines[k] : lines[k].substr(0, comment);
            const std::string tail = comment == std::string::npos ? std::string()
                                                                  : lines[k].substr(comment);
            out << body << (k + 1 < lines.size() ? "," : "") << tail << "\n";
        }
        out << ");\n\n";
    }
    return out.str();
}

Schema ddl_to_schema(const std::string& ddl) {
    Schema schema;
    Table current;
    bool inside = false;
    std::string pending_class;
    bool pending_join = false;

    for (const std::string& raw : split_lines(ddl)) {
        std::string line = trim(raw);
        if (line.empty()) continue;

        if (starts_with(line, "--")) {
            const std::string body = trim(line.substr(2));
            if (starts_with(body, "class:")) {
                pending_class = trim(body.substr(6));
                pending_join = ends_with(pending_class, "(join)");
                if (pending_join) pending_class = trim(pending_class.substr(0, pending_class.size() - 6));
            } else if (starts_with(body, "inheritance strategy:")) {
                schema.strategy = inheritance_strategy_from_string(trim(body.substr(21)));
            } else if (starts_with(body, "generated by Blueprint from model")) {
                const std::size_t quote = body.find('\'');
                if (quote != std::string::npos) {
                    schema.name = body.substr(quote + 1, body.rfind('\'') - quote - 1);
                }
            }
            continue;
        }

        if (starts_with(lower(line), "create table")) {
            current = Table{};
            current.name = trim(line.substr(12));
            if (ends_with(current.name, "(")) current.name = trim(current.name.substr(0, current.name.size() - 1));
            current.source_classifier = pending_class;
            current.is_join_table = pending_join;
            pending_class.clear();
            pending_join = false;
            inside = true;
            continue;
        }
        if (!inside) continue;
        if (starts_with(line, ");") || line == ")") {
            schema.tables.push_back(std::move(current));
            current = Table{};
            inside = false;
            continue;
        }

        // Strip the trailing comma and any attribute comment.
        std::string attribute_hint;
        const std::size_t comment = line.find("--");
        if (comment != std::string::npos) {
            const std::string body = trim(line.substr(comment + 2));
            if (starts_with(body, "attribute:")) attribute_hint = trim(body.substr(10));
            line = trim(line.substr(0, comment));
        }
        if (!line.empty() && line.back() == ',') line.pop_back();
        line = trim(line);
        if (line.empty()) continue;

        const std::string upper_head = lower(line);
        if (starts_with(upper_head, "primary key")) {
            const std::size_t open = line.find('(');
            const std::size_t close = line.rfind(')');
            if (open == std::string::npos || close == std::string::npos) continue;
            std::istringstream keys(line.substr(open + 1, close - open - 1));
            std::string key;
            while (std::getline(keys, key, ',')) {
                key = trim(key);
                for (auto& c : current.columns) {
                    if (c.name == key) { c.primary_key = true; c.not_null = true; }
                }
            }
            continue;
        }
        if (starts_with(upper_head, "foreign key")) {
            const std::size_t open = line.find('(');
            const std::size_t close = line.find(')');
            const std::size_t ref = lower(line).find("references");
            if (open == std::string::npos || close == std::string::npos || ref == std::string::npos) continue;
            const std::string column = trim(line.substr(open + 1, close - open - 1));
            const std::string tail = trim(line.substr(ref + 10));
            const std::size_t ropen = tail.find('(');
            const std::string target_table = trim(tail.substr(0, ropen));
            const std::string target_column =
                ropen == std::string::npos ? "id"
                                           : trim(tail.substr(ropen + 1, tail.rfind(')') - ropen - 1));
            for (auto& c : current.columns) {
                if (c.name != column) continue;
                c.references_table = target_table;
                c.references_column = target_column;
            }
            continue;
        }

        Column col;
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        col.name = line.substr(0, space);
        col.type = trim(line.substr(space + 1));
        const std::string not_null = " NOT NULL";
        if (ends_with(col.type, not_null)) {
            col.not_null = true;
            col.type = trim(col.type.substr(0, col.type.size() - not_null.size()));
        }
        col.source_attribute = attribute_hint.empty() ? col.name : attribute_hint;
        current.columns.push_back(std::move(col));
    }
    return schema;
}

namespace {

// Splits "app::Book" into the namespace parts and the class name.
void split_qualified(const std::string& qualified, std::vector<std::string>& namespaces,
                     std::string& name) {
    std::string rest = qualified;
    std::size_t pos;
    while ((pos = rest.find("::")) != std::string::npos) {
        namespaces.push_back(rest.substr(0, pos));
        rest = rest.substr(pos + 2);
    }
    name = rest;
}

std::string capitalize(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// Fallback when the DDL carries no class comment: turn "books" into "Book".
std::string class_name_from_table(const std::string& table) {
    std::string s = table;
    if (s.size() > 1 && s.back() == 's') s.pop_back();
    return capitalize(s);
}

} // namespace

std::string schema_to_cpp(const Schema& schema) {
    std::ostringstream out;
    out << "// Generated by Blueprint from schema '" << schema.name << "'.\n";
    out << "// Inheritance strategy of the source schema: " << to_string(schema.strategy) << ".\n";
    out << "//\n";
    out << "// Declarations only. Blueprint never writes an operation body and never\n";
    out << "// overwrites a file that already holds one.\n";
    out << "#pragma once\n\n";
    out << "#include <string>\n\n";
    out << "#include \"blueprint/annotations.hpp\"\n\n";

    // Tables are grouped by namespace so the output is a compilable header.
    std::map<std::string, std::vector<const Table*>> by_namespace;
    for (const auto& t : schema.tables) {
        if (t.is_join_table) continue;
        std::vector<std::string> namespaces;
        std::string name;
        split_qualified(t.source_classifier, namespaces, name);
        std::string joined;
        for (std::size_t k = 0; k < namespaces.size(); ++k) {
            if (k) joined += "::";
            joined += namespaces[k];
        }
        by_namespace[joined].push_back(&t);
    }

    for (const auto& [ns, tables] : by_namespace) {
        if (!ns.empty()) out << "namespace " << ns << " {\n\n";
        for (const Table* t : tables) {
            std::vector<std::string> namespaces;
            std::string name;
            split_qualified(t->source_classifier, namespaces, name);
            if (name.empty()) name = class_name_from_table(t->name);

            out << "class " << name << " {\n";
            out << "    BP_TABLE(\"" << t->name << "\");\n";
            for (const auto& c : t->columns) {
                const std::string field =
                    c.source_attribute.empty() ? c.name : c.source_attribute;
                if (c.primary_key) out << "    BP_PK(" << field << ");\n";
                out << "    BP_COLUMN(" << field << ", \"" << c.type << "\");\n";
            }
            out << "\npublic:\n";
            for (const auto& c : t->columns) {
                const std::string field =
                    c.source_attribute.empty() ? c.name : c.source_attribute;
                out << "    " << sql_type_to_cpp(c.type) << " " << field << ";";
                if (!c.references_table.empty()) {
                    out << "  // foreign key onto " << c.references_table << "("
                        << c.references_column << ")";
                }
                out << "\n";
            }
            out << "};\n\n";
        }
        if (!ns.empty()) out << "} // namespace " << ns << "\n\n";
    }
    return out.str();
}

Model schema_to_model(const Schema& schema) {
    Model model;
    model.name = schema.name;
    for (const auto& t : schema.tables) {
        if (t.is_join_table) continue;
        Classifier c;
        c.qualified_name =
            t.source_classifier.empty() ? class_name_from_table(t.name) : t.source_classifier;
        c.name = last_component(c.qualified_name);
        c.stereotypes.push_back("persistent");
        c.tags.push_back(TaggedValue{"table", t.name});
        for (const auto& col : t.columns) {
            Attribute a;
            a.name = col.source_attribute.empty() ? col.name : col.source_attribute;
            a.type = sql_type_to_cpp(col.type);
            a.visibility = Visibility::Public;
            if (col.primary_key) a.tags.push_back(TaggedValue{"pk", "true"});
            a.tags.push_back(TaggedValue{"column-type", col.type});
            c.attributes.push_back(std::move(a));
        }
        model.classifiers.push_back(std::move(c));
    }
    model.normalize();
    return model;
}

} // namespace bp
