# The third experiment: is a deliberately introduced divergence detected, and
# is anything else reported alongside it?
#
#     cmake --build build --target divergence
#
# One agreed pair of code and diagram is taken, one edit is made to the
# diagram, and the checker is run. A perfect result is one difference, of the
# expected kind, on the expected element. Anything beyond that is a spurious
# message, and spurious messages are counted separately because a checker that
# reports everything detects everything and is useless.

if(NOT DEFINED BLUEPRINT_EXE)
    message(FATAL_ERROR "BLUEPRINT_EXE is not set; run this through the divergence target")
endif()

find_program(CLANG_EXE NAMES clang++ clang++.exe)
if(NOT CLANG_EXE)
    message(FATAL_ERROR "clang++ was not found on PATH")
endif()

set(WORK "${BINARY_DIR}/divergence")
file(MAKE_DIRECTORY "${WORK}")

execute_process(COMMAND "${BLUEPRINT_EXE}" extract "${SOURCE_DIR}/examples/library.hpp"
                        --clang "${CLANG_EXE}" --stdstub "${SOURCE_DIR}/stdstub"
                        -I "${SOURCE_DIR}/include" --root "${SOURCE_DIR}/examples"
                        --name library -o "${WORK}/code.json"
                ERROR_QUIET)
execute_process(COMMAND "${BLUEPRINT_EXE}" diagram "${WORK}/code.json" --format plantuml
                        -o "${WORK}/design.puml"
                ERROR_QUIET)

# Sanity: the untouched pair has to agree, or nothing below means anything.
execute_process(COMMAND "${BLUEPRINT_EXE}" check "${WORK}/code.json" "${WORK}/design.puml"
                OUTPUT_VARIABLE baseline RESULT_VARIABLE baseline_rc)
if(NOT baseline_rc EQUAL 0)
    message(FATAL_ERROR "the untouched pair already disagrees:\n${baseline}")
endif()
message(STATUS "baseline: ${baseline}")

file(READ "${WORK}/design.puml" ORIGINAL)

# Fields are separated by @@ because a PlantUML generalization arrow contains
# a vertical bar and a bar separator would split the case in the wrong place.
#   name @@ text to find @@ replacement @@ expected kind @@ expected element
set(CASES
    "added-attribute@@  + volume : int
@@@@member-added@@library::Journal.volume"
    "removed-operation@@+ is_lendable() const : bool@@+ is_lendable() const : bool
  + ghost() : void@@member-removed@@library::Journal.ghost()"
    "changed-visibility@@  + isbn : std::string@@  - isbn : std::string@@member-changed@@library::Book.isbn"
    "removed-base@@library__LibraryItem <|-- library__Journal
@@@@relation-added@@library::Journal -> library::LibraryItem"
    "changed-multiplicity@@o-- \"0..1\" library__Shelf : default_shelf@@o-- \"1\" library__Shelf : default_shelf@@relation-changed@@library::Catalogue -> library::Shelf (default_shelf)"
)

message(STATUS "")
message(STATUS "change,expected_kind,detected,reported,spurious")
foreach(case IN LISTS CASES)
    string(REPLACE "@@" ";" parts "${case}")
    list(GET parts 0 name)
    list(GET parts 1 needle)
    list(GET parts 2 replacement)
    list(GET parts 3 kind)
    list(GET parts 4 element)

    set(mutated "${ORIGINAL}")
    string(REPLACE "${needle}" "${replacement}" mutated "${mutated}")
    if(mutated STREQUAL ORIGINAL)
        message(FATAL_ERROR "the edit for '${name}' changed nothing; the experiment would be a lie")
    endif()
    file(WRITE "${WORK}/${name}.puml" "${mutated}")

    execute_process(COMMAND "${BLUEPRINT_EXE}" check "${WORK}/code.json" "${WORK}/${name}.puml"
                    OUTPUT_VARIABLE report RESULT_VARIABLE rc)

    # The expected line, and how many lines were reported in total.
    set(detected no)
    string(FIND "${report}" "${kind}  ${element}" hit)
    if(NOT hit EQUAL -1)
        set(detected yes)
    endif()
    string(REGEX MATCHALL "\n  [a-z]+-[a-z]+  " lines "${report}")
    list(LENGTH lines reported)
    math(EXPR spurious "${reported} - 1")
    if(spurious LESS 0)
        set(spurious 0)
    endif()
    message(STATUS "${name},${kind},${detected},${reported},${spurious}")
endforeach()
