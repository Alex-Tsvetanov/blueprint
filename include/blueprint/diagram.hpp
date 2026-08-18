// Reading a diagram back into a model.
//
// The parser accepts the dialect Blueprint itself emits, in either notation.
// That is a deliberate limit and is stated as one: the point of the reader is
// to close the round trip and to give the consistency checker a model of the
// documented design, not to accept every diagram a person could draw.
#ifndef BLUEPRINT_DIAGRAM_HPP
#define BLUEPRINT_DIAGRAM_HPP

#include <string>

#include "blueprint/emit.hpp"
#include "blueprint/model.hpp"

namespace bp {

// Guesses the notation from the text: "@startuml" or "classDiagram".
Notation detect_notation(const std::string& text);

Model read_class_diagram(const std::string& text);
Model read_class_diagram(const std::string& text, Notation notation);

} // namespace bp

#endif
