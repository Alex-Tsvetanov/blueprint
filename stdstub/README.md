# Modelling prelude

Declaration-only replacements for the standard library headers that the public
headers of Blueprint include. The extractor passes `-nostdinc++ -isystem
stdstub` to Clang so that these are found instead of the real ones.

The reason is measured, not stylistic. Dumping the abstract syntax tree of one
translation unit that includes the real `<memory>`, `<vector>` and `<string>`
produces about 280 MB of JSON, almost all of it standard library internals
that no class diagram would ever show. With these replacements the same
translation unit dumps in well under a megabyte.

The replacements declare only what a class *declaration* needs in order to
type check: names, template shapes and the member functions that appear in a
signature. They contain no definitions, they are never linked and they are
never compiled into the tool. The extractor runs Clang with `-fsyntax-only`.

One consequence is worth stating. A type Blueprint does not know about here
still parses, it simply stays an opaque attribute type instead of becoming a
relation. Adding a container to `stdstub/vector` and to the container list in
`src/reader.cpp` is what teaches the tool a new one.
