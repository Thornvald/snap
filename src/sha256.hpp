#pragma once

#include <filesystem>
#include <string>

namespace snap {

std::string sha256_file_hex(const std::filesystem::path& file_path);

} // namespace snap
