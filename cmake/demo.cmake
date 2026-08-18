# The demonstration. Run with:  cmake --build build --target demo
#
# Blueprint is pointed at its own source, then at the bundled annotated
# example, and the full cycle is closed and checked in front of the reader.
# A tool that cannot document itself is not one to trust with anything else.

if(NOT DEFINED BLUEPRINT_EXE)
    message(FATAL_ERROR "BLUEPRINT_EXE is not set; run this through the demo target")
endif()

set(OUT "${SOURCE_DIR}/diagrams")
file(MAKE_DIRECTORY "${OUT}")

find_program(CLANG_EXE NAMES clang++ clang++.exe)
if(NOT CLANG_EXE)
    message(FATAL_ERROR
        "clang++ was not found on PATH.\n"
        "Blueprint drives the Clang front end to read C++; it does not link libclang, "
        "so the binary alone is enough. Install LLVM or put clang++ on PATH and run "
        "the demo again. Everything that does not read C++ still works without it: "
        "try  blueprint diagram, blueprint ddl, blueprint check.")
endif()

set(COMMON --clang "${CLANG_EXE}" --stdstub "${SOURCE_DIR}/stdstub" -I "${SOURCE_DIR}/include")

set(STEP 0)
function(banner text)
    math(EXPR NEXT "${STEP} + 1")
    set(STEP ${NEXT} PARENT_SCOPE)
    message(STATUS "")
    message(STATUS "=== ${NEXT}. ${text}")
endfunction()

# Runs Blueprint and stops the demo if it fails for a reason other than an
# intentional divergence report.
function(blueprint)
    cmake_parse_arguments(RUN "ALLOW_FAILURE" "" "ARGS" ${ARGN})
    execute_process(COMMAND "${BLUEPRINT_EXE}" ${RUN_ARGS} RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0 AND NOT RUN_ALLOW_FAILURE)
        message(FATAL_ERROR "blueprint ${RUN_ARGS} failed with ${rc}")
    endif()
endfunction()

message(STATUS "Blueprint demonstration")
message(STATUS "  tool    ${BLUEPRINT_EXE}")
message(STATUS "  clang   ${CLANG_EXE}")
message(STATUS "  output  ${OUT}")

# ---------------------------------------------------------------------------
banner("Reverse engineering: Blueprint reads its own public headers")
blueprint(ARGS extract "${SOURCE_DIR}/examples/self.hpp" ${COMMON}
               --root "${SOURCE_DIR}/include" --name blueprint
               -o "${OUT}/blueprint.model.json")

banner("One model, two notations")
blueprint(ARGS diagram "${OUT}/blueprint.model.json" --format plantuml
               -o "${OUT}/blueprint.class.puml")
blueprint(ARGS diagram "${OUT}/blueprint.model.json" --format mermaid
               -o "${OUT}/blueprint.class.mmd")

banner("The annotated example, extracted")
blueprint(ARGS extract "${SOURCE_DIR}/examples/library.hpp" ${COMMON}
               --root "${SOURCE_DIR}/examples" --name library
               -o "${OUT}/library.model.json")
blueprint(ARGS diagram "${OUT}/library.model.json" --format plantuml
               -o "${OUT}/library.class.puml")
blueprint(ARGS diagram "${OUT}/library.model.json" --format mermaid
               -o "${OUT}/library.class.mmd")
execute_process(COMMAND "${BLUEPRINT_EXE}" diagram "${OUT}/library.model.json" --format plantuml
                OUTPUT_VARIABLE library_diagram)
message("${library_diagram}")

banner("Forward engineering: the same model under all three inheritance strategies")
foreach(strategy single-table table-per-class table-per-concrete)
    blueprint(ARGS ddl "${OUT}/library.model.json" --inheritance ${strategy}
                   -o "${OUT}/library.${strategy}.sql")
endforeach()
file(READ "${OUT}/library.single-table.sql" single_table_sql)
message("${single_table_sql}")

banner("Closing the loop: C++ skeletons regenerated from the schema")
blueprint(ARGS skeleton "${OUT}/library.single-table.sql" -o "${OUT}/library_generated.hpp")
file(READ "${OUT}/library_generated.hpp" generated)
message("${generated}")

banner("The generated header is real C++: the compiler accepts it")
execute_process(COMMAND "${CLANG_EXE}" -std=c++20 -nostdinc++
                        -isystem "${SOURCE_DIR}/stdstub" -I "${SOURCE_DIR}/include"
                        -fsyntax-only "${OUT}/library_generated.hpp"
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the generated header does not compile, the round trip is broken")
endif()
message(STATUS "clang -fsyntax-only accepted the generated header")

banner("The diagram read back agrees with the code it came from")
blueprint(ARGS import "${OUT}/library.class.puml" -o "${OUT}/library.from-diagram.json")
blueprint(ARGS check "${OUT}/library.model.json" "${OUT}/library.from-diagram.json")

banner("Now the diagram is edited by hand and the checker catches the drift")
file(READ "${OUT}/library.class.puml" drifted)
# Three edits: a member disappears, a visibility changes, a class is renamed.
string(REPLACE "  + volume : int\n" "" drifted "${drifted}")
string(REPLACE "  + isbn : std::string" "  - isbn : std::string" drifted "${drifted}")
string(REPLACE "\"library::Shelf\"" "\"library::Rack\"" drifted "${drifted}")
file(WRITE "${OUT}/library.drifted.puml" "${drifted}")
blueprint(ALLOW_FAILURE ARGS check "${OUT}/library.model.json" "${OUT}/library.drifted.puml")

banner("Use case and activity diagrams, from the model the syllabus asks for")
blueprint(ARGS usecase "${SOURCE_DIR}/examples/blueprint.usecase" --format plantuml
               -o "${OUT}/blueprint.usecase.puml")
blueprint(ARGS usecase "${SOURCE_DIR}/examples/blueprint.usecase" --format mermaid
               -o "${OUT}/blueprint.usecase.mmd")
blueprint(ARGS activity "${SOURCE_DIR}/examples/roundtrip.activity" --format plantuml
               -o "${OUT}/roundtrip.activity.puml")
blueprint(ARGS activity "${SOURCE_DIR}/examples/roundtrip.activity" --format mermaid
               -o "${OUT}/roundtrip.activity.mmd")
file(READ "${OUT}/roundtrip.activity.mmd" activity)
message("${activity}")

banner("Round trip fidelity, measured on this machine")
blueprint(ARGS roundtrip "${SOURCE_DIR}/examples/library.hpp" ${COMMON}
               --root "${SOURCE_DIR}/examples" --name library)

message(STATUS "")
message(STATUS "Everything written to ${OUT}")
