#pragma once

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

namespace cfs {

inline std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

// Strict integer parsing: the whole string must be a valid int.
inline bool parse_int(const std::string& text, int& out) {
    const std::string t = trim(text);
    if (t.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(t.c_str(), &end, 10);
    if (errno != 0 || *end != '\0' || value < INT_MIN || value > INT_MAX) return false;
    out = static_cast<int>(value);
    return true;
}

}  // namespace cfs
