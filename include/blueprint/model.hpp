// The internal UML model.
//
// Everything else in the tool either produces this or consumes it. It knows
// nothing about C++, nothing about PlantUML or Mermaid and nothing about SQL.
// That is the whole point of the architecture: a new input language touches
// only the reader, a new notation touches only an emitter.
//
// Classifiers are linked by qualified name, never by pointer. Names survive
// serialisation, pointers do not, and the model has to survive a trip through
// a file for the round-trip experiment to be possible at all.
#ifndef BLUEPRINT_MODEL_HPP
#define BLUEPRINT_MODEL_HPP

#include <optional>
#include <string>
#include <vector>

#include "blueprint/json.hpp"

namespace bp {

enum class Visibility { Public, Protected, Private, Package };

// UML calls a static member "classifier scoped" and a normal one "instance
// scoped". The model keeps the UML word, not the C++ one.
enum class Scope { Instance, Classifier };

enum class ClassifierKind { Class, Struct, Interface, Enumeration, Template };

// Relationship kinds of the UML class diagram that this tool infers.
enum class RelationKind {
    Generalization,  // inheritance
    Realization,     // template specialisation of a general template
    Association,     // reference without ownership
    Aggregation,     // shared ownership, the part outlives the whole
    Composition,     // exclusive ownership, the part dies with the whole
    Dependency       // used in a signature, not held as a member
};

std::string to_string(Visibility v);
std::string to_string(Scope s);
std::string to_string(ClassifierKind k);
std::string to_string(RelationKind r);
Visibility visibility_from_string(const std::string& s);
Scope scope_from_string(const std::string& s);
ClassifierKind classifier_kind_from_string(const std::string& s);
RelationKind relation_kind_from_string(const std::string& s);

// A tagged value in the UML sense: a name and a value attached to an element,
// used here to carry the mapping onto a relational schema.
struct TaggedValue {
    std::string name;
    std::string value;
    bool operator==(const TaggedValue&) const = default;
};

struct Attribute {
    std::string name;
    std::string type;          // as written in the source
    Visibility visibility = Visibility::Private;
    Scope scope = Scope::Instance;
    std::string multiplicity = "1";
    std::vector<TaggedValue> tags;
    bool operator==(const Attribute&) const = default;
};

struct Parameter {
    std::string name;
    std::string type;
    bool operator==(const Parameter&) const = default;
};

struct Operation {
    std::string name;
    std::string return_type;
    std::vector<Parameter> parameters;
    Visibility visibility = Visibility::Public;
    Scope scope = Scope::Instance;
    bool is_virtual = false;
    bool is_pure = false;
    bool is_const = false;
    bool operator==(const Operation&) const = default;

    // "name(T, U) const" without the return type: the identity of an
    // operation for overload resolution, and what the checker matches on.
    std::string signature() const;
};

struct Relation {
    std::string from;   // qualified name of the source classifier
    std::string to;     // qualified name of the target classifier
    RelationKind kind = RelationKind::Association;
    std::string label;              // member name that produced it, when any
    std::string multiplicity = "1"; // at the target end
    bool is_virtual_base = false;   // virtual inheritance
    Visibility visibility = Visibility::Public; // access of the base, for generalization
    bool operator==(const Relation&) const = default;

    // Two relations are the same edge when endpoints, kind and label agree.
    std::string key() const;
};

struct Classifier {
    std::string name;            // unqualified
    std::string qualified_name;  // with namespaces, "::" separated
    ClassifierKind kind = ClassifierKind::Class;
    std::vector<std::string> template_parameters;
    std::vector<std::string> stereotypes;
    std::vector<TaggedValue> tags;
    std::vector<Attribute> attributes;
    std::vector<Operation> operations;
    std::vector<std::string> enumerators;
    bool is_abstract = false;
    std::string source_file;
    int source_line = 0;
    bool operator==(const Classifier&) const = default;

    const TaggedValue* tag(const std::string& name) const;
    bool has_stereotype(const std::string& s) const;
};

struct Model {
    std::string name = "model";
    std::vector<Classifier> classifiers;
    std::vector<Relation> relations;

    const Classifier* find(const std::string& qualified_name) const;
    Classifier* find(const std::string& qualified_name);
    bool contains(const std::string& qualified_name) const { return find(qualified_name) != nullptr; }

    // Sorts classifiers, members and relations into a canonical order so that
    // two models built by different paths compare and print identically.
    void normalize();

    // Number of comparable elements: classifiers, attributes, operations and
    // relations. The denominator of the round-trip fidelity measure.
    std::size_t element_count() const;

    Json to_json() const;
    static Model from_json(const Json& j);
};

} // namespace bp

#endif
