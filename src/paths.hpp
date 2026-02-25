#pragma once

#include <filesystem>
#include <string>

namespace snap {

std::filesystem::path get_home_dir();
std::filesystem::path get_snap_dir();
std::filesystem::path get_bin_dir();
std::filesystem::path get_registry_path();
std::filesystem::path get_installed_binary_path();

std::string get_latest_asset_name();
std::string get_latest_asset_url();
std::string get_latest_checksums_url();

} // namespace snap
