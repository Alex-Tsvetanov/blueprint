# Blueprint

Round-trip engineering between C++ source, UML class diagrams and relational schemas.
Course project for **UML Object-Oriented Design**, MEng in Computer and Software
Engineering, Faculty of Computer Systems and Technologies, Technical University of Sofia.

## What it is

Blueprint reads a C++ translation unit through the Clang front end and builds a structural
model of it: classes, attributes, operations, visibility, scope, template parameters and the
relationships between classes. It emits that model as a UML class diagram in PlantUML and in
Mermaid, derives a relational schema from classes marked for persistence, and works the other
way too, generating class skeletons from a schema and reading a diagram back into a model. A
consistency checker compares a model extracted from code against a model read from a diagram
and reports where the two have drifted apart.

The problem it solves is ordinary and universal: the diagram stops matching the code within
days of being drawn, and nothing detects that.

## Goals

- Extract a UML class model from a C++ translation unit using semantic analysis, not text matching.
- Distinguish association, aggregation and composition by ownership of the member, not by guesswork.
- Emit the same model to two independent notations, so that the model is provably format-agnostic.
- Derive a relational schema from annotated classes, under any of the three standard inheritance strategies.
- Generate class declaration skeletons from a schema, closing the loop.
- Report every difference between a model extracted from code and a model read from a diagram.

## Dependencies: none

The tool builds on a clean machine from a **C++20 compiler and CMake**, and nothing else. No
package manager, no network fetch at configure time, no third-party library.

- The JSON reader is in `src/json.cpp`, about 300 lines.
- The test runner is `tests/testing.hpp`, about 100 lines, wired to CTest with `add_test`.
- The timing harness uses `std::chrono::steady_clock`.

Clang is a **run-time** input of the extractor, not a build-time dependency. Blueprint runs
`clang++ -Xclang -ast-dump=json -fsyntax-only` and parses what comes back, so the binary alone
is enough and there is no libclang to link against, no version-matched headers and no ABI to
match. Everything except extraction works on a machine with no Clang at all.

## Technologies

| Technology | Version / standard | Why |
| --- | --- | --- |
| C++ | ISO/IEC 14882:2020 (C++20) | Designated initialisers and defaulted comparisons keep the model types short; the tool operates on C++, so staying in the language avoids a second toolchain. |
| CMake | 3.20 or newer | Configure, build, CTest, and the `demo` and `measure` targets. |
| Clang | any version that prints `-ast-dump=json` | Resolved names, post-substitution types and the ownership spelling of every member, which is what separates aggregation from composition. Driven as a binary, not linked. |
| PlantUML | current language reference | Text notation, diffable, renders through existing renderers, covers UML class-diagram semantics closely. |
| Mermaid | current | Second emitter, and a check that the internal model is not shaped around one output format. |

## Build

Verified on Windows 11 with GCC 15.2.0 (MinGW-w64), CMake 4.3.2, Ninja 1.13.2, Clang 22.1.0.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

58 test cases in 7 suites. CTest reports one entry per suite; each entry prints its own cases.

## Run

One command runs the whole thing over Blueprint's own source and over the bundled annotated
example, and closes the round trip in front of you:

```bash
cmake --build build --target demo
```

It writes into `diagrams/` and prints: the class diagram extracted from the tool's own public
headers, the relational schema under all three inheritance strategies, the C++ skeletons
regenerated from that schema, proof that the compiler accepts them, the consistency check
passing on an untouched diagram and failing on a hand-edited one, the use case and activity
diagrams, and the measured round-trip fidelity.

To reproduce every number in the report:

```bash
cmake --build build --target measure
```

### The subcommands

```
blueprint extract   <source.hpp> [-I dir]... [--root dir] [--stdstub dir]
                    [--clang path] [--std c++20] [--name text] [-o model.json]
blueprint diagram   <model.json> [--format plantuml|mermaid] [-o out]
blueprint import    <diagram>    [-o model.json]
blueprint ddl       <model.json> [--inheritance single-table|table-per-class|
                    table-per-concrete] [-o schema.sql]
blueprint skeleton  <schema.sql> [-o out.hpp]
blueprint check     <code> <design>       exit 1 when they disagree
blueprint usecase   <spec>  [--format ...] [-o out]
blueprint activity  <spec>  [--format ...] [-o out]
blueprint bench     <source>... [--repeat 3]
blueprint roundtrip <source>    [--inheritance ...]
```

A model file and a diagram file are accepted interchangeably wherever a model is asked for;
the format is detected from the content. `check` exits non-zero on divergence, which is what
lets it run inside a build.

Worked example, by hand:

```bash
./build/blueprint extract examples/library.hpp \
    --clang clang++ --stdstub stdstub -I include \
    --root examples --name library -o build/library.model.json
./build/blueprint diagram build/library.model.json --format plantuml -o build/library.puml
./build/blueprint ddl     build/library.model.json --inheritance single-table -o build/library.sql
./build/blueprint skeleton build/library.sql -o build/library_generated.hpp
./build/blueprint import  build/library.puml -o build/library.from-diagram.json
./build/blueprint check   build/library.model.json build/library.from-diagram.json
```

## How ownership becomes a relationship

The rule is one function, `bp::classify_member_type` in `src/reader.cpp`, and it reads UML
literally: composition means the part dies with the whole.

| Member spelling | Ownership | Relationship | Multiplicity |
| --- | --- | --- | --- |
| `T value` | exclusive | composition | 1 |
| `std::unique_ptr<T>` | exclusive | composition | 0..1 |
| `std::optional<T>` | exclusive | composition | 0..1 |
| `std::shared_ptr<T>` | shared | aggregation | 0..1 |
| `T*` | none | association | 0..1 |
| `T&` | none | association | 1 |
| container of any of the above | as the element | as the element | * |
| a type in a signature only | none | dependency | |

A member whose type is not a classifier of the model stays a plain attribute.

## Annotating a class for persistence

`include/blueprint/annotations.hpp` defines four macros. They expand to `static constexpr`
members, not to C++ attributes, and the reason is in the header: Clang's JSON dump prints an
attribute as a node with a source range and **without its arguments**, so the payload of
`[[clang::annotate("table=books")]]` never arrives. A constant with a string initialiser
arrives whole, is ordinary C++20 and costs nothing at run time. The reader recognises the
`bp_` prefix and moves them onto the classifier as a UML stereotype and tagged values, so they
never show up as attributes in the model.

```cpp
class Book {
    BP_TABLE("books");
    BP_INHERITANCE("single-table");
    BP_PK(id);
    BP_COLUMN(title, "VARCHAR(200)");
public:
    int id;
    std::string title;
};
```

## The modelling prelude

`stdstub/` holds declaration-only replacements for the standard library headers the extractor
meets. They are never compiled into the tool and never linked; the extractor passes
`-nostdinc++ -isystem stdstub` to Clang and runs it with `-fsyntax-only`.

The reason is measured. One translation unit that includes the real `<memory>`, `<vector>` and
`<string>` dumps about 280 MB of JSON, almost all of it standard library internals that no
class diagram would ever show. With the replacements the same unit dumps in well under a
megabyte. See `stdstub/README.md`.

## Layout

```
include/blueprint/   public headers, one per module
src/                 the library, plus main.cpp for the command line
tests/               the runner and 58 cases
examples/            the annotated example, the self-model unit, the behaviour specs
stdstub/             the modelling prelude
cmake/               the demo and measurement scripts
tools/               the scaling input generator, used only for measurements
docs/                the project report
diagrams/            everything the demo produces
```

Every dependency points at the model. No emitter knows the reader exists.

## Documentation

The project report lives in `docs/` and is written in Bulgarian, because the subject is taught
in Bulgarian and the layout is normative for the faculty. It follows the TU-Sofia FKST
formatting rules: A4, Times metrics at 12pt, 1.5 line spacing, Roman-numbered sections, tables
captioned above and figures below.

```bash
cd docs
latexmk -pdf Main.tex     # output: build/Main.pdf
```

`latexmk` exits 0 even when the bibliography fails silently, so check `build/Main.blg` for the
entry count rather than trusting the exit code. Remaining unfilled facts are marked with
`\TODO{...}`:

```bash
grep -rn 'TODO' docs/chapters docs/Main.tex docs/references.bib
```

## Status

- [x] Reader: Clang JSON abstract syntax tree into the internal model
- [x] Relationship inference: association, aggregation, composition, dependency, realization
- [x] Multiple and virtual inheritance, with the access of each base
- [x] Template classes and specialisations
- [x] PlantUML emitter
- [x] Mermaid emitter
- [x] Persistence annotations and relational schema generation
- [x] All three inheritance strategies, selectable per hierarchy
- [x] Foreign keys and join tables from associations
- [x] Class skeleton generation from a schema
- [x] Diagram reader for both notations
- [x] Consistency checker with a non-zero exit code
- [x] Use case and activity models with emitters for both notations
- [x] 58 unit tests wired to CTest
- [x] Experiments run and measured

Known limits, stated rather than hidden: the diagram reader accepts the dialect Blueprint
emits, not every diagram a person could draw; a Mermaid class diagram has no marker for an
operation that is virtual but overridable, so that one flag does not survive that notation;
and name resolution is quadratic in the number of classifiers, which is visible in the
measurements above 400 classes.

## License

MIT. See [LICENSE](LICENSE).
