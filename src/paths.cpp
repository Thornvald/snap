#include "paths.hpp"

#include <cstdlib>

namespace snap {

std::filesystem::path get_home_dir() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    if (home) return std::filesystem::path(home);

    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive && path) return std::filesystem::path(std::string(drive) + path);

    return std::filesystem::path("C:\\Users\\Default");
#else
    const char* home = std::getenv("HOME");
    if (home) return std::filesystem::path(home);
    return std::filesystem::path("/tmp");
#endif
}

std::filesystem::path get_snap_dir() {
    return get_home_dir() / ".snap";
}

std::filesystem::path get_bin_dir() {
    return get_snap_dir() / "bin";
}

std::filesystem::path get_registry_path() {
    return get_snap_dir() / "aliases.json";
}

std::filesystem::path get_installed_binary_path() {
#ifdef _WIN32
    return get_bin_dir() / "snap.exe";
#else
    return get_bin_dir() / "snap";
#endif
}

std::string get_latest_asset_url() {
    const std::string base = "https://github.com/Thornvald/snap/releases/latest/download/";
#ifdef _WIN32
    return base + "snap-windows-x64.exe";
#elif defined(__APPLE__)
    return base + "snap-macos-universal";
#elif defined(__linux__)
  #if defined(__aarch64__) || defined(__arm64__)
    return base + "snap-linux-arm64";
  #else
    return base + "snap-linux-x64";
  #endif
#else
    return "";
#endif
}

} // namespace snap
