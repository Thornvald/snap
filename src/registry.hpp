#pragma once

#include <string>
#include <vector>

namespace snap {

struct AliasEntry {
    std::string alias;
    std::string target;
};

std::vector<AliasEntry> load_registry();
void save_registry(const std::vector<AliasEntry>& entries);

} // namespace snap
