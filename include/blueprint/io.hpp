// File reading and writing, in one place so that every module fails the same
// way on a missing file.
#ifndef BLUEPRINT_IO_HPP
#define BLUEPRINT_IO_HPP

#include <string>

namespace bp {

// Both throw std::runtime_error naming the path, which is the only thing the
// caller can act on.
std::string read_file(const std::string& path);
void write_file(const std::string& path, const std::string& content);

} // namespace bp

#endif
