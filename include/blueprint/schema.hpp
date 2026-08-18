// Forward engineering: model to relational schema, and schema back to code.
//
// This is the half of the round trip the subject names explicitly. A class
// marked persistent becomes a table, its attributes become columns, and its
// associations become foreign keys or join tables. Inheritance has no direct
// counterpart in the relational model, so one of the three established
// strategies is chosen, and the choice is a tagged value on the base class
// rather than something built into the tool.
#ifndef BLUEPRINT_SCHEMA_HPP
#define BLUEPRINT_SCHEMA_HPP

#include <string>
#include <vector>

#include "blueprint/model.hpp"

namespace bp {

enum class InheritanceStrategy {
    SingleTable,       // one table for the whole hierarchy, plus a discriminator
    TablePerClass,     // a table per class, the child keyed to the parent
    TablePerConcrete   // a table per instantiable class, inherited columns copied
};

std::string to_string(InheritanceStrategy s);
InheritanceStrategy inheritance_strategy_from_string(const std::string& s);

struct Column {
    std::string name;
    std::string type;
    bool primary_key = false;
    bool not_null = false;
    std::string references_table;
    std::string references_column;
    // The attribute this column came from, so the reverse direction can
    // rebuild a class rather than guess one.
    std::string source_attribute;
    bool operator==(const Column&) const = default;
};

struct Table {
    std::string name;
    std::vector<Column> columns;
    std::string source_classifier;   // qualified name of the class it came from
    bool is_join_table = false;
    bool operator==(const Table&) const = default;

    const Column* column(const std::string& name) const;
};

struct Schema {
    std::string name = "schema";
    InheritanceStrategy strategy = InheritanceStrategy::SingleTable;
    std::vector<Table> tables;

    const Table* table(const std::string& name) const;
};

// The mapping is reported as well as performed: a class that cannot be mapped
// is named, not silently dropped.
struct SchemaReport {
    Schema schema;
    std::vector<std::string> notes;
};

SchemaReport model_to_schema(const Model& model, InheritanceStrategy fallback);

std::string schema_to_ddl(const Schema& schema);
Schema ddl_to_schema(const std::string& ddl);

// Closing the loop: a schema becomes C++ class declarations, annotated so
// that extracting them again yields a comparable model.
std::string schema_to_cpp(const Schema& schema);

// A model view of a schema, for the consistency checker.
Model schema_to_model(const Schema& schema);

// The type mappings, exposed because they are the part worth testing.
std::string cpp_type_to_sql(const std::string& cpp_type);
std::string sql_type_to_cpp(const std::string& sql_type);

} // namespace bp

#endif
