// The command line. A thin layer over the library: every subcommand reads
// files, calls one library entry point and writes files. Nothing decides
// anything here, which is why the tests can link the library and never touch
// this file.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "blueprint/behavior.hpp"
#include "blueprint/diagram.hpp"
#include "blueprint/diff.hpp"
#include "blueprint/emit.hpp"
#include "blueprint/io.hpp"
#include "blueprint/json.hpp"
#include "blueprint/model.hpp"
#include "blueprint/reader.hpp"
#include "blueprint/schema.hpp"

namespace {

struct Options {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> flags;
    std::vector<std::string> include_dirs;

    bool has(const std::string& name) const { return flags.count(name) > 0; }
    std::string get(const std::string& name, const std::string& fallback = {}) const {
        const auto it = flags.find(name);
        return it == flags.end() ? fallback : it->second;
    }
};

Options parse_options(int argc, char** argv) {
    Options options;
    if (argc > 1) options.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
            options.include_dirs.push_back(arg.substr(2));
        } else if (arg == "-I" && i + 1 < argc) {
            options.include_dirs.push_back(argv[++i]);
        } else if (arg.rfind("--", 0) == 0) {
            const std::string name = arg.substr(2);
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("-", 0) != 0) {
                options.flags[name] = argv[++i];
            } else {
                options.flags[name] = "true";
            }
        } else if (arg == "-o" && i + 1 < argc) {
            options.flags["output"] = argv[++i];
        } else {
            options.positional.push_back(arg);
        }
    }
    return options;
}

void emit_result(const Options& options, const std::string& text) {
    const std::string out = options.get("output");
    if (out.empty()) {
        std::cout << text;
        return;
    }
    const std::filesystem::path path(out);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    bp::write_file(out, text);
    std::cerr << "wrote " << out << " (" << text.size() << " bytes)\n";
}

bp::ClangInvocation invocation_from(const Options& options) {
    bp::ClangInvocation inv;
    inv.clang = options.get("clang", "clang++");
    inv.standard = options.get("std", "c++20");
    inv.include_dirs = options.include_dirs;
    inv.stdstub_dir = options.get("stdstub");
    inv.target = options.get("target");
    return inv;
}

bp::ReaderOptions reader_options_from(const Options& options) {
    bp::ReaderOptions reader;
    const std::string root = options.get("root");
    if (!root.empty()) reader.roots.push_back(root);
    reader.skip_private = options.has("public-only");
    reader.model_name = options.get("name", "model");
    return reader;
}

// A file may hold a serialised model or a diagram; the caller should not have
// to say which.
bp::Model load_model(const std::string& path) {
    const std::string text = bp::read_file(path);
    if (text.find("blueprint_model") != std::string::npos) {
        return bp::Model::from_json(bp::Json::parse(text));
    }
    return bp::read_class_diagram(text);
}

int command_extract(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "extract needs a source file\n";
        return 2;
    }
    const bp::ClangInvocation inv = invocation_from(options);
    const bp::Json ast = bp::dump_ast(inv, options.positional.front());
    const bp::Model model = bp::read_ast_json(ast, reader_options_from(options));
    emit_result(options, model.to_json().dump(2) + "\n");
    std::cerr << "extracted " << model.classifiers.size() << " classifiers and "
              << model.relations.size() << " relations\n";
    return 0;
}

int command_diagram(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "diagram needs a model file\n";
        return 2;
    }
    const bp::Model model = load_model(options.positional.front());
    const bp::Notation notation = bp::notation_from_string(options.get("format", "plantuml"));
    emit_result(options, bp::emit_class_diagram(model, notation));
    return 0;
}

int command_import(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "import needs a diagram file\n";
        return 2;
    }
    const bp::Model model = bp::read_class_diagram(bp::read_file(options.positional.front()));
    emit_result(options, model.to_json().dump(2) + "\n");
    return 0;
}

int command_ddl(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "ddl needs a model file\n";
        return 2;
    }
    const bp::Model model = load_model(options.positional.front());
    const bp::InheritanceStrategy strategy =
        bp::inheritance_strategy_from_string(options.get("inheritance", "single-table"));
    const bp::SchemaReport report = bp::model_to_schema(model, strategy);
    emit_result(options, bp::schema_to_ddl(report.schema));
    for (const auto& note : report.notes) std::cerr << "note: " << note << "\n";
    return 0;
}

int command_skeleton(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "skeleton needs a schema file\n";
        return 2;
    }
    const bp::Schema schema = bp::ddl_to_schema(bp::read_file(options.positional.front()));
    emit_result(options, bp::schema_to_cpp(schema));
    return 0;
}

int command_check(const Options& options) {
    if (options.positional.size() < 2) {
        std::cerr << "check needs a model of the code and a model of the design\n";
        return 2;
    }
    const bp::Model code = load_model(options.positional[0]);
    const bp::Model design = load_model(options.positional[1]);
    const bp::DiffReport report = bp::compare(code, design);
    std::cout << report.text();
    // A non-zero exit is what lets this run inside a build.
    return report.consistent() ? 0 : 1;
}

int command_use_case(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "usecase needs a specification file\n";
        return 2;
    }
    const bp::UseCaseModel model =
        bp::parse_use_case_spec(bp::read_file(options.positional.front()));
    const bp::Notation notation = bp::notation_from_string(options.get("format", "plantuml"));
    emit_result(options, bp::emit_use_case_diagram(model, notation));
    return 0;
}

int command_activity(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "activity needs a specification file\n";
        return 2;
    }
    const bp::ActivityModel model =
        bp::parse_activity_spec(bp::read_file(options.positional.front()));
    const bp::Notation notation = bp::notation_from_string(options.get("format", "plantuml"));
    emit_result(options, bp::emit_activity_diagram(model, notation));
    return 0;
}

double seconds_since(const std::chrono::steady_clock::time_point& start) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double>(elapsed).count();
}

std::string fixed(double value, int digits) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(digits) << value;
    return out.str();
}

// Extraction cost against translation unit size. Every number this prints was
// timed on the machine it ran on; nothing here is a constant in disguise.
int command_bench(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "bench needs at least one source file\n";
        return 2;
    }
    const int repeat = std::max(1, std::stoi(options.get("repeat", "3")));
    const bp::ClangInvocation inv = invocation_from(options);
    const bp::ReaderOptions reader = reader_options_from(options);

    std::cout << "source,source_bytes,ast_bytes,ast_nodes,classifiers,relations,"
                 "elements,model_bytes,dump_s,parse_s,read_s\n";
    for (const auto& source : options.positional) {
        double dump_best = 0;
        double parse_best = 0;
        double read_best = 0;
        std::size_t ast_bytes = 0;
        std::size_t nodes = 0;
        bp::Model model;
        for (int r = 0; r < repeat; ++r) {
            // The dump is timed separately from the parse because the two
            // costs scale differently and one figure would hide that. The
            // best of the repeats is reported, not the mean: the slower runs
            // differ by scheduling noise, which is not a property of the tool.
            const auto t0 = std::chrono::steady_clock::now();
            const bp::Json ast = bp::dump_ast(inv, source, &ast_bytes);
            const double dump_s = seconds_since(t0);

            const std::string text = ast.dump();
            const auto t1 = std::chrono::steady_clock::now();
            const bp::Json reparsed = bp::Json::parse(text);
            const double parse_s = seconds_since(t1);

            const auto t2 = std::chrono::steady_clock::now();
            model = bp::read_ast_json(reparsed, reader);
            const double read_s = seconds_since(t2);

            if (r == 0 || dump_s < dump_best) dump_best = dump_s;
            if (r == 0 || parse_s < parse_best) parse_best = parse_s;
            if (r == 0 || read_s < read_best) read_best = read_s;
            nodes = static_cast<std::size_t>(std::count(text.begin(), text.end(), '{'));
        }
        std::size_t source_bytes = 0;
        try { source_bytes = bp::read_file(source).size(); } catch (const std::exception&) {}
        std::cout << source << "," << source_bytes << "," << ast_bytes << "," << nodes << ","
                  << model.classifiers.size() << "," << model.relations.size() << ","
                  << model.element_count() << "," << model.to_json().dump().size() << ","
                  << fixed(dump_best, 4) << "," << fixed(parse_best, 4) << ","
                  << fixed(read_best, 4) << "\n";
    }
    return 0;
}

// The round trip, measured. Two loops are reported separately because they
// lose different things: a diagram cannot hold a tagged value, and a table
// cannot hold an operation at all.
int command_roundtrip(const Options& options) {
    if (options.positional.empty()) {
        std::cerr << "roundtrip needs a source file\n";
        return 2;
    }
    const bp::ClangInvocation inv = invocation_from(options);
    const bp::ReaderOptions reader = reader_options_from(options);
    const bp::Model original = bp::read_ast_json(dump_ast(inv, options.positional.front()), reader);

    std::cout << "loop,elements_before,elements_after,preserved,differences,fidelity\n";
    const auto report_loop = [&](const std::string& label, const bp::Model& returned) {
        const bp::DiffReport diff = bp::compare(original, returned);
        const std::size_t before = original.element_count();
        const std::size_t kept = bp::preserved_elements(original, returned);
        const double fidelity =
            before == 0 ? 1.0 : static_cast<double>(kept) / static_cast<double>(before);
        std::cout << label << "," << before << "," << returned.element_count() << "," << kept
                  << "," << diff.differences.size() << "," << fixed(fidelity, 4) << "\n";
    };

    for (const bp::Notation notation : {bp::Notation::PlantUml, bp::Notation::Mermaid}) {
        const std::string text = bp::emit_class_diagram(original, notation);
        report_loop("code-" + bp::to_string(notation) + "-code",
                    bp::read_class_diagram(text, notation));
    }

    const bp::InheritanceStrategy strategy =
        bp::inheritance_strategy_from_string(options.get("inheritance", "single-table"));
    const bp::SchemaReport schema = bp::model_to_schema(original, strategy);
    const std::string ddl = bp::schema_to_ddl(schema.schema);
    report_loop("code-schema-code", bp::schema_to_model(bp::ddl_to_schema(ddl)));
    return 0;
}

int usage() {
    std::cout <<
        "Blueprint, round-trip engineering between C++, UML and a relational schema.\n"
        "\n"
        "  blueprint extract   <source.hpp> [-I dir]... [--root dir] [--stdstub dir]\n"
        "                      [--clang path] [--std c++20] [--name text] [-o model.json]\n"
        "  blueprint diagram   <model.json> [--format plantuml|mermaid] [-o out]\n"
        "  blueprint import    <diagram>    [-o model.json]\n"
        "  blueprint ddl       <model.json> [--inheritance single-table|table-per-class|\n"
        "                      table-per-concrete] [-o schema.sql]\n"
        "  blueprint skeleton  <schema.sql> [-o out.hpp]\n"
        "  blueprint check     <code> <design>       exit 1 when they disagree\n"
        "  blueprint usecase   <spec>  [--format ...] [-o out]\n"
        "  blueprint activity  <spec>  [--format ...] [-o out]\n"
        "  blueprint bench     <source>... [--repeat 3]\n"
        "  blueprint roundtrip <source>    [--inheritance ...]\n"
        "\n"
        "A model file and a diagram file are both accepted wherever a model is asked\n"
        "for; the format is detected from the content.\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    if (options.command.empty() || options.command == "help" || options.command == "--help") {
        return usage();
    }
    try {
        if (options.command == "extract") return command_extract(options);
        if (options.command == "diagram") return command_diagram(options);
        if (options.command == "import") return command_import(options);
        if (options.command == "ddl") return command_ddl(options);
        if (options.command == "skeleton") return command_skeleton(options);
        if (options.command == "check") return command_check(options);
        if (options.command == "usecase") return command_use_case(options);
        if (options.command == "activity") return command_activity(options);
        if (options.command == "bench") return command_bench(options);
        if (options.command == "roundtrip") return command_roundtrip(options);
        std::cerr << "unknown command: " << options.command << "\n\n";
        usage();
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 3;
    }
}
