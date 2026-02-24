#include "registry.hpp"

#include "paths.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace snap {

namespace {

std::string unescape_json_path(const std::string& value) {
    std::string out;
    out.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '\\') {
            out += '\\';
            ++i;
        } else {
            out += value[i];
        }
    }
    return out;
}

std::string escape_json_path(const std::string& value) {
    std::string out;
    out.reserve(value.size());

    for (char c : value) {
        if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

} // namespace

std::vector<AliasEntry> load_registry() {
    std::vector<AliasEntry> entries;
    const std::filesystem::path registry_path = get_registry_path();
    if (!std::filesystem::exists(registry_path)) return entries;

    std::ifstream in(registry_path);
    if (!in.is_open()) return entries;

    const std::string content((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());

    size_t pos = 0;
    while (pos < content.size()) {
        const size_t q1 = content.find('"', pos);
        if (q1 == std::string::npos) break;

        const size_t q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) break;

        const std::string key = content.substr(q1 + 1, q2 - q1 - 1);

        const size_t colon = content.find(':', q2 + 1);
        if (colon == std::string::npos) break;

        const size_t q3 = content.find('"', colon + 1);
        if (q3 == std::string::npos) break;

        const size_t q4 = content.find('"', q3 + 1);
        if (q4 == std::string::npos) break;

        const std::string raw_value = content.substr(q3 + 1, q4 - q3 - 1);
        entries.push_back({key, unescape_json_path(raw_value)});
        pos = q4 + 1;
    }

    return entries;
}

void save_registry(const std::vector<AliasEntry>& entries) {
    std::filesystem::create_directories(get_snap_dir());

    std::ofstream out(get_registry_path());
    if (!out.is_open()) {
        std::cerr << "error: could not write to " << get_registry_path() << "\n";
        return;
    }

    out << "{\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        out << "  \"" << entries[i].alias << "\": \""
            << escape_json_path(entries[i].target) << "\"";
        if (i + 1 < entries.size()) out << ",";
        out << "\n";
    }
    out << "}\n";
}

} // namespace snap
