// The consistency checker.
//
// Two models in, a list of differences out. This is the part that turns the
// tool from a drawing aid into something that can fail a build: as long as
// both directions are automated, the drift between a diagram and the code it
// claims to describe is measurable rather than merely suspected.
#ifndef BLUEPRINT_DIFF_HPP
#define BLUEPRINT_DIFF_HPP

#include <string>
#include <vector>

#include "blueprint/model.hpp"

namespace bp {

enum class DiffKind {
    ClassAdded,      // present in the code, missing from the diagram
    ClassRemoved,    // present in the diagram, missing from the code
    ClassChanged,    // same class, different kind or abstractness
    MemberAdded,
    MemberRemoved,
    MemberChanged,
    RelationAdded,
    RelationRemoved,
    RelationChanged
};

std::string to_string(DiffKind k);

struct Difference {
    DiffKind kind = DiffKind::ClassChanged;
    std::string element;   // qualified name, with the member appended when any
    std::string detail;
    bool operator==(const Difference&) const = default;
};

struct DiffReport {
    std::vector<Difference> differences;
    bool consistent() const { return differences.empty(); }
    std::string text() const;
};

// `code` is the model extracted from the source, `design` the model read from
// the diagram. The direction matters for the wording: a class in the code but
// not in the diagram is added, the reverse is removed.
DiffReport compare(const Model& code, const Model& design);

// How many of the elements of `code` reappear unchanged in `returned`. This
// is the numerator of the round-trip fidelity figure, and it is counted
// element by element rather than derived from the number of differences: when
// a whole class fails to come back, its members never get compared at all,
// and a count of differences would quietly call that a small loss.
std::size_t preserved_elements(const Model& code, const Model& returned);

} // namespace bp

#endif
