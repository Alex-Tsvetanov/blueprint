// A small annotated domain, used as the forward-engineering example.
//
// It is deliberately ordinary: a lending library. What matters is that every
// construction the reader has to distinguish appears here exactly once, so
// that a failure in the generated schema points at one rule rather than at a
// tangle of them.
//
//   Author         value member, plain persistent class
//   Shelf          referenced without ownership, an association
//   LibraryItem    abstract base of the hierarchy, carries the strategy
//   Book, Journal  concrete subclasses
//   Catalogue      owns items exclusively and shares a location, and holds
//                  a collection, which is where multiplicity comes from
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "blueprint/annotations.hpp"

namespace library {

class Author {
    BP_TABLE("authors");
    BP_PK(id);
    BP_COLUMN(name, "VARCHAR(120)");

public:
    int id;
    std::string name;
    int birth_year;
};

// A shelf exists independently of anything standing on it, so every reference
// to it is an association and never an ownership claim.
class Shelf {
    BP_TABLE("shelves");
    BP_PK(id);
    BP_COLUMN(code, "CHAR(8)");

public:
    int id;
    std::string code;
};

// The strategy tag lives on the base class, so a reader of the source can see
// how the hierarchy will be stored without opening the tool.
class LibraryItem {
    BP_TABLE("items");
    BP_INHERITANCE("single-table");
    BP_PK(id);
    BP_COLUMN(title, "VARCHAR(200)");

public:
    virtual ~LibraryItem();
    virtual double late_fee_per_day() const = 0;
    virtual bool is_lendable() const;

    int id;
    std::string title;

protected:
    // A value member: the author record lives and dies with the item, which
    // is composition.
    Author author;
    // A pointer without ownership: the shelf outlives the item, association.
    Shelf* shelf;
};

class Book : public LibraryItem {
    BP_TABLE("books");
    BP_COLUMN(isbn, "CHAR(13)");

public:
    double late_fee_per_day() const override;
    std::string isbn;
    int page_count;
};

class Journal : public LibraryItem {
    BP_TABLE("journals");
    BP_COLUMN(issn, "CHAR(9)");

public:
    double late_fee_per_day() const override;
    bool is_lendable() const override;
    std::string issn;
    int volume;
};

// The four ownership spellings side by side. Reading the generated diagram
// against this class is the quickest check that the inference still works.
class Catalogue {
    BP_TABLE("catalogues");
    BP_PK(id);

public:
    int id;
    std::string name;

    void add(std::unique_ptr<LibraryItem> item);
    Shelf* locate(const std::string& title) const;

private:
    std::vector<std::unique_ptr<LibraryItem>> items;  // composition, many
    std::shared_ptr<Shelf> default_shelf;             // aggregation, at most one
    Shelf* overflow;                                  // association, at most one
    Author curator;                                   // composition, exactly one
};

}  // namespace library
