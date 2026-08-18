#include "blueprint/io.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace bp {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open for reading: " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open for writing: " + path);
    out << content;
    if (!out) throw std::runtime_error("write failed: " + path);
}

} // namespace bp
