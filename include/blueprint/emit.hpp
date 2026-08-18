// Emitters: one model, two notations.
//
// Both emitters share a single traversal of the model and differ only in the
// strings they produce for each element. That is not an aesthetic decision:
// two independent notations produced from one walk is the check that the
// model is not secretly shaped around one output format.
#ifndef BLUEPRINT_EMIT_HPP
#define BLUEPRINT_EMIT_HPP

#include <string>

#include "blueprint/model.hpp"

namespace bp {

enum class Notation { PlantUml, Mermaid };

std::string to_string(Notation n);
Notation notation_from_string(const std::string& s);

std::string emit_class_diagram(const Model& model, Notation notation);

// The identifier a classifier is referred to by inside a diagram. Qualified
// C++ names contain "::", which neither notation accepts in an identifier, so
// every classifier gets a generated alias and its real name as a label.
std::string diagram_alias(const std::string& qualified_name);

} // namespace bp

#endif
