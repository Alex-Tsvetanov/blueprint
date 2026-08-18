// Persistence annotations.
//
// A UML stereotype and its tagged values have to reach the tool from the
// source, and the source is the only place they can live without drifting
// away from the code the way an external mapping file does.
//
// The carrier is a `static constexpr` member, not a C++ attribute. That is
// not the first choice, it is the choice that survives the pipeline: Clang
// prints attributes into its JSON dump as a node with a source range and
// *without their arguments*, so `[[clang::annotate("table=books")]]` arrives
// at the reader as the fact that some annotation existed and nothing else. A
// constant with a string initialiser arrives whole, is ordinary standard
// C++20, costs nothing at run time and is readable to a person opening the
// header. The cost is that the annotation looks like a member; the reader
// recognises the `bp_` prefix and moves it onto the classifier as a
// stereotype or a tagged value, so it never appears as an attribute in the
// model.
#ifndef BLUEPRINT_ANNOTATIONS_HPP
#define BLUEPRINT_ANNOTATIONS_HPP

// Marks the class as persistent and names its table.
#define BP_TABLE(table_name) static constexpr const char* bp_table = table_name

// Selects the strategy used to map this hierarchy onto tables. One of
// "single-table", "table-per-class", "table-per-concrete".
#define BP_INHERITANCE(strategy) static constexpr const char* bp_inheritance = strategy

// Marks one attribute as the primary key.
#define BP_PK(field) static constexpr bool bp_pk_##field = true

// Overrides the SQL type of the column generated for one attribute.
#define BP_COLUMN(field, sql_type) static constexpr const char* bp_col_##field = sql_type

// Overrides the column name of one attribute.
#define BP_COLUMN_NAME(field, column) static constexpr const char* bp_name_##field = column

#endif
