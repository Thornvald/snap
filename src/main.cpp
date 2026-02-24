/*
 * snap - Bind aliases to executables so you can launch apps by name.
 *
 * Usage:
 *   snap <alias> <path-to-executable>   Register an alias
 *   snap list                           List all registered aliases
 *   snap remove <alias>                 Remove an alias
 *   snap --help                         Show help
 *   snap --version                      Show version
 *
 * Cross-platform: Windows, macOS, Linux (x64, ARM64).
 * C++17, no external dependencies.
 *
 * MIT License - https://github.com/Thornvald/snap
 */

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  #include <shlobj.h>
#else
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace fs = std::filesystem;

// ── version ────────────────────────────────────────────────────────────────

static constexpr const char* VERSION = "1.0.0";

// ── helpers ────────────────────────────────────────────────────────────────

static fs::path get_home_dir() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    if (home) return fs::path(home);
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path  = std::getenv("HOMEPATH");
    if (drive && path) return fs::path(std::string(drive) + path);
    return fs::path("C:\\Users\\Default");
#else
    const char* home = std::getenv("HOME");
    if (home) return fs::path(home);
    return fs::path("/tmp");
#endif
}

static fs::path get_snap_dir() {
    return get_home_dir() / ".snap";
}

static fs::path get_bin_dir() {
    return get_snap_dir() / "bin";
}

static fs::path get_registry_path() {
    return get_snap_dir() / "aliases.json";
}

// ── minimal JSON registry (no deps) ───────────────────────────────────────
// Format: {"alias": "target_path", ...}
// We hand-parse to avoid any dependency.

struct AliasEntry {
    std::string alias;
    std::string target;
};

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

static std::vector<AliasEntry> load_registry() {
    std::vector<AliasEntry> entries;
    fs::path reg = get_registry_path();
    if (!fs::exists(reg)) return entries;

    std::ifstream f(reg);
    if (!f.is_open()) return entries;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    f.close();

    // Simple parse: find "key": "value" pairs
    size_t pos = 0;
    while (pos < content.size()) {
        size_t q1 = content.find('"', pos);
        if (q1 == std::string::npos) break;
        size_t q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) break;

        std::string key = content.substr(q1 + 1, q2 - q1 - 1);

        size_t colon = content.find(':', q2 + 1);
        if (colon == std::string::npos) break;

        size_t q3 = content.find('"', colon + 1);
        if (q3 == std::string::npos) break;
        size_t q4 = content.find('"', q3 + 1);
        if (q4 == std::string::npos) break;

        std::string value = content.substr(q3 + 1, q4 - q3 - 1);

        // Unescape backslashes
        std::string unescaped;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '\\') {
                unescaped += '\\';
                ++i;
            } else {
                unescaped += value[i];
            }
        }

        entries.push_back({key, unescaped});
        pos = q4 + 1;
    }
    return entries;
}

static void save_registry(const std::vector<AliasEntry>& entries) {
    fs::create_directories(get_snap_dir());
    std::ofstream f(get_registry_path());
    if (!f.is_open()) {
        std::cerr << "error: could not write to " << get_registry_path() << "\n";
        return;
    }

    f << "{\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        // Escape backslashes for JSON
        std::string escaped;
        for (char c : entries[i].target) {
            if (c == '\\') escaped += "\\\\";
            else escaped += c;
        }
        f << "  \"" << entries[i].alias << "\": \"" << escaped << "\"";
        if (i + 1 < entries.size()) f << ",";
        f << "\n";
    }
    f << "}\n";
    f.close();
}

// ── PATH management ───────────────────────────────────────────────────────

#ifdef _WIN32

static std::string get_user_path() {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return "";

    char buffer[32767]; // max env var size on Windows
    DWORD bufSize = sizeof(buffer);
    DWORD type;
    std::string result;

    if (RegQueryValueExA(key, "Path", nullptr, &type, (LPBYTE)buffer, &bufSize) == ERROR_SUCCESS) {
        result = std::string(buffer, bufSize > 0 ? bufSize - 1 : 0);
    }
    RegCloseKey(key);
    return result;
}

static void set_user_path(const std::string& newPath) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_WRITE, &key) != ERROR_SUCCESS) {
        std::cerr << "error: could not open user environment registry key.\n";
        return;
    }

    RegSetValueExA(key, "Path", 0, REG_EXPAND_SZ,
                   (const BYTE*)newPath.c_str(), (DWORD)(newPath.size() + 1));
    RegCloseKey(key);

    // Broadcast WM_SETTINGCHANGE so open shells pick up the change
    DWORD_PTR result;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, &result);
}

static bool path_contains(const std::string& pathVar, const std::string& dir) {
    // Case-insensitive search on Windows
    std::string lowerPath = pathVar;
    std::string lowerDir = dir;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    std::transform(lowerDir.begin(), lowerDir.end(), lowerDir.begin(), ::tolower);

    // Remove trailing separator for comparison
    if (!lowerDir.empty() && (lowerDir.back() == '\\' || lowerDir.back() == '/'))
        lowerDir.pop_back();

    std::istringstream ss(lowerPath);
    std::string token;
    while (std::getline(ss, token, ';')) {
        token = trim(token);
        if (!token.empty() && (token.back() == '\\' || token.back() == '/'))
            token.pop_back();
        if (token == lowerDir) return true;
    }
    return false;
}

static void ensure_bin_in_path() {
    std::string binDir = get_bin_dir().string();
    std::string userPath = get_user_path();

    if (path_contains(userPath, binDir)) return;

    std::string newPath = userPath;
    if (!newPath.empty() && newPath.back() != ';') newPath += ';';
    newPath += binDir;

    set_user_path(newPath);
}

#else // macOS / Linux

static void ensure_bin_in_path() {
    fs::path binDir = get_bin_dir();
    fs::create_directories(binDir);

    // Check if already in current PATH
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv) {
        std::string pathStr(pathEnv);
        std::istringstream ss(pathStr);
        std::string token;
        while (std::getline(ss, token, ':')) {
            if (token == binDir.string()) return; // already present
        }
    }

    // Append to shell rc files
    std::string exportLine = "\n# snap - alias manager\nexport PATH=\"" + binDir.string() + ":$PATH\"\n";
    std::vector<std::string> rcFiles = {".bashrc", ".zshrc", ".profile"};
    fs::path home = get_home_dir();
    bool added = false;

    for (const auto& rc : rcFiles) {
        fs::path rcPath = home / rc;
        if (!fs::exists(rcPath)) continue;

        // Check if already added
        std::ifstream check(rcPath);
        std::string content((std::istreambuf_iterator<char>(check)),
                             std::istreambuf_iterator<char>());
        check.close();

        if (content.find(binDir.string()) != std::string::npos) continue;

        std::ofstream out(rcPath, std::ios::app);
        if (out.is_open()) {
            out << exportLine;
            out.close();
            std::cout << "  Added " << binDir.string() << " to ~/" << rc << "\n";
            added = true;
        }
    }

    if (added) {
        std::cout << "  Restart your terminal or run: source ~/.bashrc\n";
    }
}

#endif

// ── shim creation / removal ───────────────────────────────────────────────

#ifdef _WIN32

static void create_shim(const std::string& alias, const std::string& target) {
    fs::path shimPath = get_bin_dir() / (alias + ".cmd");
    fs::create_directories(get_bin_dir());

    std::ofstream f(shimPath);
    if (!f.is_open()) {
        std::cerr << "error: could not create shim at " << shimPath << "\n";
        return;
    }

    f << "@echo off\n";
    f << "\"" << target << "\" %*\n";
    f.close();
}

static void remove_shim(const std::string& alias) {
    fs::path shimPath = get_bin_dir() / (alias + ".cmd");
    if (fs::exists(shimPath)) {
        fs::remove(shimPath);
    }
}

#else

static void create_shim(const std::string& alias, const std::string& target) {
    fs::path shimPath = get_bin_dir() / alias;
    fs::create_directories(get_bin_dir());

    std::ofstream f(shimPath);
    if (!f.is_open()) {
        std::cerr << "error: could not create shim at " << shimPath << "\n";
        return;
    }

    f << "#!/bin/sh\n";
    f << "exec \"" << target << "\" \"$@\"\n";
    f.close();

    // Make executable
    chmod(shimPath.c_str(), 0755);
}

static void remove_shim(const std::string& alias) {
    fs::path shimPath = get_bin_dir() / alias;
    if (fs::exists(shimPath)) {
        fs::remove(shimPath);
    }
}

#endif

// ── self-install ──────────────────────────────────────────────────────────
// On first run, copies snap into ~/.snap/bin/ and adds it to PATH.
// This means the user can download snap.exe anywhere, double-click or run it
// once, and from then on just type "snap" in any terminal.

static fs::path get_self_path() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) return fs::path(buf);
    return fs::path();
#else
    // /proc/self/exe on Linux, argv[0] fallback
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p;
    return fs::path();
#endif
}

static bool self_install() {
    fs::path selfPath = get_self_path();
    if (selfPath.empty() || !fs::exists(selfPath)) return false;

#ifdef _WIN32
    fs::path installedPath = get_bin_dir() / "snap.exe";
#else
    fs::path installedPath = get_bin_dir() / "snap";
#endif

    // Check if we are already the installed copy
    std::error_code ec;
    if (fs::exists(installedPath, ec)) {
        auto selfCanon = fs::canonical(selfPath, ec);
        auto instCanon = fs::canonical(installedPath, ec);
        if (!ec && selfCanon == instCanon) return false; // already installed, running from bin

        // Installed copy exists but we're running from elsewhere - update it
    }

    // ── Transparent first-run setup with step-by-step output ──
    std::cout << "\n";
    std::cout << "  snap - first time setup\n";
    std::cout << "  ---------------------------------------------------------\n";
    std::cout << "  This is a one-time setup. Here is exactly what snap will do:\n\n";
    std::cout << "  [1/3] Create directory:  " << get_bin_dir().string() << "\n";

    fs::create_directories(get_bin_dir(), ec);
    if (ec) {
        std::cerr << "        FAILED: " << ec.message() << "\n";
        return false;
    }
    std::cout << "        Done.\n\n";

    std::cout << "  [2/3] Copy snap binary:  " << selfPath.string() << "\n";
    std::cout << "        To:                " << installedPath.string() << "\n";

    fs::copy_file(selfPath, installedPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "        FAILED: " << ec.message() << "\n";
        return false;
    }

#ifndef _WIN32
    chmod(installedPath.c_str(), 0755);
#endif
    std::cout << "        Done.\n\n";

    std::cout << "  [3/3] Add to user PATH:  " << get_bin_dir().string() << "\n";
#ifdef _WIN32
    std::cout << "        (Modifies HKCU\\Environment\\Path - no admin required)\n";
#else
    std::cout << "        (Appends export line to shell rc files)\n";
#endif

    ensure_bin_in_path();
    std::cout << "        Done.\n";

    std::cout << "\n  ---------------------------------------------------------\n";
    std::cout << "  Setup complete! You can now type 'snap' in any new terminal.\n\n";
    std::cout << "  What is snap?\n";
    std::cout << "    A lightweight tool to bind short aliases to executables.\n";
    std::cout << "    Example: snap evr \"C:\\Program Files\\Everything\\Everything.exe\"\n";
    std::cout << "    Then just type 'evr' to launch it.\n\n";
    std::cout << "  Quick reference:\n";
    std::cout << "    snap <alias> <path>     Bind an alias to an executable\n";
    std::cout << "    snap list               List all your aliases\n";
    std::cout << "    snap remove <alias>     Remove an alias\n";
    std::cout << "    snap --help             Full help\n";
    std::cout << "  ---------------------------------------------------------\n";
    std::cout << "  Source: https://github.com/Thornvald/snap\n";
    std::cout << "  All data stored in: " << get_snap_dir().string() << "\n";
    std::cout << "  To uninstall, just delete that folder and remove it from PATH.\n";
    std::cout << "\n";

#ifdef _WIN32
    std::cout << "  Press any key to exit...\n";
    std::system("pause >nul 2>&1");
#endif

    return true;
}

// ── commands ──────────────────────────────────────────────────────────────

static void print_help() {
    std::cout << R"(
  snap - bind aliases to executables

  Usage:
    snap <alias> <path>     Register an alias for an executable
    snap list               List all registered aliases
    snap remove <alias>     Remove a registered alias
    snap --help, -h         Show this help message
    snap --version, -v      Show version

  Examples:
    snap evr "C:\Program Files (x86)\Everything\Everything.exe"
    snap code "/usr/local/bin/code"
    snap list
    snap remove evr

  After adding an alias, open a new terminal and type the alias name
  to launch the application.
)";
}

static void print_version() {
    std::cout << "snap " << VERSION << "\n";
}

static int cmd_add(const std::string& alias, const std::string& target) {
    // Validate alias name
    for (char c : alias) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            std::cerr << "error: alias can only contain alphanumeric characters, hyphens, and underscores.\n";
            return 1;
        }
    }

    if (alias.empty()) {
        std::cerr << "error: alias cannot be empty.\n";
        return 1;
    }

    // Validate target path exists
    fs::path targetPath(target);
    if (!fs::exists(targetPath)) {
        std::cerr << "error: target path does not exist: " << target << "\n";
        return 1;
    }

    if (fs::is_directory(targetPath)) {
        std::cerr << "error: target must be an executable file, not a directory.\n";
        std::cerr << "  Provide the full path to the .exe / binary.\n";
        return 1;
    }

    // Make sure PATH is set up
    ensure_bin_in_path();

    // Load existing aliases
    auto entries = load_registry();

    // Check for duplicate
    bool updated = false;
    for (auto& e : entries) {
        if (e.alias == alias) {
            e.target = target;
            updated = true;
            break;
        }
    }

    if (!updated) {
        entries.push_back({alias, target});
    }

    // Save and create shim
    save_registry(entries);
    create_shim(alias, target);

    if (updated) {
        std::cout << "  Updated alias '" << alias << "' -> " << target << "\n";
    } else {
        std::cout << "  Created alias '" << alias << "' -> " << target << "\n";
    }

    return 0;
}

static int cmd_list() {
    auto entries = load_registry();

    if (entries.empty()) {
        std::cout << "  No aliases registered. Use 'snap <alias> <path>' to add one.\n";
        return 0;
    }

    // Find the longest alias for formatting
    size_t maxLen = 0;
    for (const auto& e : entries) {
        if (e.alias.size() > maxLen) maxLen = e.alias.size();
    }

    std::cout << "\n  Registered aliases (" << entries.size() << "):\n\n";
    for (const auto& e : entries) {
        bool exists = fs::exists(e.target);
        std::string status = exists ? "" : " [missing]";
        std::cout << "    " << e.alias;
        // Pad
        for (size_t i = e.alias.size(); i < maxLen; ++i) std::cout << ' ';
        std::cout << "  ->  " << e.target << status << "\n";
    }
    std::cout << "\n";

    return 0;
}

static int cmd_remove(const std::string& alias) {
    auto entries = load_registry();

    auto it = std::find_if(entries.begin(), entries.end(),
        [&](const AliasEntry& e) { return e.alias == alias; });

    if (it == entries.end()) {
        std::cerr << "error: alias '" << alias << "' not found.\n";
        return 1;
    }

    std::string target = it->target;
    entries.erase(it);

    save_registry(entries);
    remove_shim(alias);

    std::cout << "  Removed alias '" << alias << "' (was -> " << target << ")\n";
    return 0;
}

// ── main ──────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Self-install on first run: copy binary to ~/.snap/bin/ and add to PATH.
    // If installation just happened, exit so user opens a new terminal.
    if (self_install()) {
        return 0;
    }

    if (argc < 2) {
        print_help();
        return 0;
    }

    std::string cmd(argv[1]);

    if (cmd == "--help" || cmd == "-h") {
        print_help();
        return 0;
    }

    if (cmd == "--version" || cmd == "-v") {
        print_version();
        return 0;
    }

    if (cmd == "list") {
        return cmd_list();
    }

    if (cmd == "remove") {
        if (argc < 3) {
            std::cerr << "error: usage: snap remove <alias>\n";
            return 1;
        }
        return cmd_remove(argv[2]);
    }

    // Default: snap <alias> <path>
    if (argc < 3) {
        std::cerr << "error: usage: snap <alias> <path-to-executable>\n";
        std::cerr << "  Run 'snap --help' for more information.\n";
        return 1;
    }

    // Reconstruct path from remaining args (handles unquoted spaces)
    std::string target;
    for (int i = 2; i < argc; ++i) {
        if (i > 2) target += ' ';
        target += argv[i];
    }

    return cmd_add(cmd, target);
}
