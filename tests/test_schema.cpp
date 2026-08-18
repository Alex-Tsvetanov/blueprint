#include "testing.hpp"

#include "blueprint/schema.hpp"

using namespace bp;

namespace {

// A three-class hierarchy plus two association targets, which is the smallest
// model that exercises all three inheritance strategies and both association
// mappings.
Model domain_model(const std::string& strategy) {
    Model model;
    model.name = "domain";

    Classifier author;
    author.name = "Author";
    author.qualified_name = "lib::Author";
    author.stereotypes.push_back("persistent");
    author.tags.push_back(TaggedValue{"table", "authors"});
    Attribute author_id{"id", "int", Visibility::Public, Scope::Instance, "1", {}};
    author_id.tags.push_back(TaggedValue{"pk", "true"});
    author.attributes.push_back(author_id);
    author.attributes.push_back(
        Attribute{"name", "std::string", Visibility::Public, Scope::Instance, "1", {}});
    model.classifiers.push_back(author);

    Classifier item;
    item.name = "Item";
    item.qualified_name = "lib::Item";
    item.is_abstract = true;
    item.stereotypes.push_back("persistent");
    item.tags.push_back(TaggedValue{"table", "items"});
    if (!strategy.empty()) item.tags.push_back(TaggedValue{"inheritance", strategy});
    Attribute item_id{"id", "int", Visibility::Public, Scope::Instance, "1", {}};
    item_id.tags.push_back(TaggedValue{"pk", "true"});
    item.attributes.push_back(item_id);
    Attribute title{"title_", "std::string", Visibility::Public, Scope::Instance, "1", {}};
    title.tags.push_back(TaggedValue{"column-type", "VARCHAR(200)"});
    item.attributes.push_back(title);
    model.classifiers.push_back(item);

    Classifier book;
    book.name = "Book";
    book.qualified_name = "lib::Book";
    book.stereotypes.push_back("persistent");
    book.tags.push_back(TaggedValue{"table", "books"});
    book.attributes.push_back(
        Attribute{"isbn", "std::string", Visibility::Public, Scope::Instance, "1", {}});
    book.attributes.push_back(
        Attribute{"pages", "int", Visibility::Public, Scope::Instance, "1", {}});
    model.classifiers.push_back(book);

    Classifier shelf;
    shelf.name = "Shelf";
    shelf.qualified_name = "lib::Shelf";
    shelf.stereotypes.push_back("persistent");
    shelf.tags.push_back(TaggedValue{"table", "shelves"});
    Attribute shelf_id{"id", "int", Visibility::Public, Scope::Instance, "1", {}};
    shelf_id.tags.push_back(TaggedValue{"pk", "true"});
    shelf.attributes.push_back(shelf_id);
    model.classifiers.push_back(shelf);

    Relation inherit;
    inherit.from = "lib::Book";
    inherit.to = "lib::Item";
    inherit.kind = RelationKind::Generalization;
    inherit.multiplicity = "";
    model.relations.push_back(inherit);

    Relation one;
    one.from = "lib::Item";
    one.to = "lib::Shelf";
    one.kind = RelationKind::Association;
    one.label = "shelf";
    one.multiplicity = "0..1";
    model.relations.push_back(one);

    Relation many;
    many.from = "lib::Author";
    many.to = "lib::Book";
    many.kind = RelationKind::Aggregation;
    many.label = "written";
    many.multiplicity = "*";
    model.relations.push_back(many);

    model.normalize();
    return model;
}

} // namespace

TEST(schema, maps_the_scalar_types_in_both_directions) {
    CHECK_EQ(cpp_type_to_sql("int"), std::string("INTEGER"));
    CHECK_EQ(cpp_type_to_sql("std::string"), std::string("TEXT"));
    CHECK_EQ(cpp_type_to_sql("double"), std::string("DOUBLE PRECISION"));
    CHECK_EQ(cpp_type_to_sql("bool"), std::string("BOOLEAN"));
    // A class type is not a scalar and has no column of its own; it becomes a
    // foreign key instead, which is decided elsewhere.
    CHECK_EQ(cpp_type_to_sql("lib::Author"), std::string(""));

    CHECK_EQ(sql_type_to_cpp("INTEGER"), std::string("int"));
    CHECK_EQ(sql_type_to_cpp("VARCHAR(200)"), std::string("std::string"));
    CHECK_EQ(sql_type_to_cpp("DOUBLE PRECISION"), std::string("double"));
    CHECK_EQ(sql_type_to_cpp("BOOLEAN"), std::string("bool"));
}

TEST(schema, single_table_folds_the_hierarchy_and_adds_a_discriminator) {
    const SchemaReport report =
        model_to_schema(domain_model("single-table"), InheritanceStrategy::SingleTable);
    CHECK(report.schema.table("items") != nullptr);
    CHECK(report.schema.table("books") == nullptr);
    const Table& items = *report.schema.table("items");
    CHECK(items.column("class_type") != nullptr);
    CHECK(items.column("isbn") != nullptr);
    CHECK(items.column("pages") != nullptr);
    // A subclass column has to be nullable, because a row of another subclass
    // leaves it empty.
    CHECK_EQ(items.column("isbn")->not_null, false);
    CHECK_EQ(items.column("title")->type, std::string("VARCHAR(200)"));
}

TEST(schema, table_per_class_gives_the_child_its_own_table_keyed_to_the_parent) {
    const SchemaReport report =
        model_to_schema(domain_model("table-per-class"), InheritanceStrategy::TablePerClass);
    CHECK(report.schema.table("items") != nullptr);
    const Table* books = report.schema.table("books");
    CHECK(books != nullptr);
    CHECK(books->column("isbn") != nullptr);
    CHECK(books->column("title") == nullptr);  // inherited, not copied
    const Column* key = books->column("id");
    CHECK(key != nullptr);
    CHECK_EQ(key->primary_key, true);
    CHECK_EQ(key->references_table, std::string("items"));
}

TEST(schema, table_per_concrete_copies_inherited_columns_and_skips_the_abstract_base) {
    const SchemaReport report =
        model_to_schema(domain_model("table-per-concrete"), InheritanceStrategy::TablePerConcrete);
    CHECK(report.schema.table("items") == nullptr);
    const Table* books = report.schema.table("books");
    CHECK(books != nullptr);
    CHECK(books->column("isbn") != nullptr);
    CHECK(books->column("title") != nullptr);  // copied down from the base
    bool mentioned = false;
    for (const auto& note : report.notes) {
        if (note.find("lib::Item") != std::string::npos) mentioned = true;
    }
    CHECK(mentioned);
}

TEST(schema, an_association_of_one_becomes_a_foreign_key) {
    const SchemaReport report =
        model_to_schema(domain_model("single-table"), InheritanceStrategy::SingleTable);
    const Table& items = *report.schema.table("items");
    const Column* fk = items.column("shelf_id");
    CHECK(fk != nullptr);
    CHECK_EQ(fk->references_table, std::string("shelves"));
    CHECK_EQ(fk->references_column, std::string("id"));
    CHECK_EQ(fk->not_null, false);  // 0..1, so the column may be empty
}

TEST(schema, an_association_of_many_becomes_a_join_table) {
    const SchemaReport report =
        model_to_schema(domain_model("table-per-class"), InheritanceStrategy::TablePerClass);
    const Table* join = report.schema.table("authors_written");
    CHECK(join != nullptr);
    CHECK_EQ(join->is_join_table, true);
    CHECK_EQ(join->columns.size(), std::size_t(2));
    CHECK_EQ(join->columns[0].references_table, std::string("authors"));
    CHECK_EQ(join->columns[1].references_table, std::string("books"));
}

TEST(schema, a_class_without_a_key_gets_a_surrogate_one_and_the_choice_is_reported) {
    const SchemaReport report =
        model_to_schema(domain_model("table-per-class"), InheritanceStrategy::TablePerClass);
    const Table* books = report.schema.table("books");
    CHECK(books != nullptr);
    CHECK(books->column("id") != nullptr);
    bool reported = false;
    for (const auto& note : report.notes) {
        if (note.find("surrogate key") != std::string::npos) reported = true;
    }
    CHECK(reported);
}

TEST(schema, the_ddl_survives_being_read_back) {
    const SchemaReport report =
        model_to_schema(domain_model("table-per-class"), InheritanceStrategy::TablePerClass);
    const std::string ddl = schema_to_ddl(report.schema);
    CHECK_CONTAINS(ddl, "CREATE TABLE items (");
    CHECK_CONTAINS(ddl, "PRIMARY KEY (id)");
    CHECK_CONTAINS(ddl, "FOREIGN KEY (shelf_id) REFERENCES shelves(id)");
    CHECK_CONTAINS(ddl, "-- class: lib::Item");

    const Schema reread = ddl_to_schema(ddl);
    CHECK_EQ(reread.tables.size(), report.schema.tables.size());
    CHECK(reread.strategy == InheritanceStrategy::TablePerClass);
    const Table* items = reread.table("items");
    CHECK(items != nullptr);
    CHECK_EQ(items->source_classifier, std::string("lib::Item"));
    CHECK(items->column("title") != nullptr);
    CHECK_EQ(items->column("title")->source_attribute, std::string("title_"));
    CHECK_EQ(items->column("id")->primary_key, true);
    CHECK_EQ(items->column("shelf_id")->references_table, std::string("shelves"));
}

TEST(schema, generating_code_from_a_schema_reproduces_the_annotations) {
    const SchemaReport report =
        model_to_schema(domain_model("table-per-class"), InheritanceStrategy::TablePerClass);
    const std::string code = schema_to_cpp(report.schema);
    CHECK_CONTAINS(code, "namespace lib {");
    CHECK_CONTAINS(code, "class Item {");
    CHECK_CONTAINS(code, "BP_TABLE(\"items\")");
    CHECK_CONTAINS(code, "BP_PK(id)");
    CHECK_CONTAINS(code, "BP_COLUMN(title_, \"VARCHAR(200)\")");
    CHECK_CONTAINS(code, "std::string title_;");
    // No join table becomes a class; a join table is an artefact of the
    // relational model and has no counterpart in the object model.
    CHECK(code.find("class Authors_written") == std::string::npos);
}

TEST(schema, a_model_with_nothing_persistent_produces_no_tables_and_says_so) {
    Model plain;
    Classifier c;
    c.name = "Loose";
    c.qualified_name = "Loose";
    plain.classifiers.push_back(c);
    const SchemaReport report = model_to_schema(plain, InheritanceStrategy::SingleTable);
    CHECK_EQ(report.schema.tables.size(), std::size_t(0));
    CHECK_EQ(report.notes.size(), std::size_t(1));
    CHECK_CONTAINS(report.notes.front(), "persistent");
}
