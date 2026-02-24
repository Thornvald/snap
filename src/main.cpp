/*
 * snap - Bind aliases to files so you can launch them by name.
 *
 * Usage:
 *   snap <alias> <path-to-target>       Register an alias
 *   snap list                           List all registered aliases
 *   snap remove <alias>                 Remove an alias
 *   snap uninstall                      Uninstall snap and aliases
 *   snap update                         Update snap to latest release
 *   snap --help                         Show help
 *   snap --version                      Show version
 *
 * Cross-platform: Windows, macOS, Linux (x64, ARM64).
 * C++17, no external dependencies.
 *
 * MIT License - https://github.com/Thornvald/snap
 */

#include <algorithm>
#include <cctype>
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

static constexpr const char* VERSION = "1.0.2";

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

static fs::path get_installed_binary_path() {
#ifdef _WIN32
    return get_bin_dir() / "snap.exe";
#else
    return get_bin_dir() / "snap";
#endif
}

static std::string get_latest_asset_url() {
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

#ifdef _WIN32
static bool is_windows_executable_extension(const fs::path& targetPath) {
    std::string ext = targetPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".exe" || ext == ".com" || ext == ".bat" || ext == ".cmd";
}
#else
static std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}
#endif

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

static std::string normalize_windows_path_token(std::string token) {
    token = trim(token);
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
        token = token.substr(1, token.size() - 2);
    }

    std::replace(token.begin(), token.end(), '/', '\\');
    while (!token.empty() && (token.back() == '\\' || token.back() == '/')) {
        token.pop_back();
    }

    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return token;
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

static void remove_bin_from_path() {
    std::string userPath = get_user_path();
    if (userPath.empty()) return;

    const std::string binNorm = normalize_windows_path_token(get_bin_dir().string());
    std::istringstream ss(userPath);
    std::string token;
    std::vector<std::string> kept;
    bool removed = false;

    while (std::getline(ss, token, ';')) {
        std::string trimmed = trim(token);
        if (trimmed.empty()) continue;

        if (normalize_windows_path_token(trimmed) == binNorm) {
            removed = true;
            continue;
        }
        kept.push_back(trimmed);
    }

    if (!removed) return;

    std::string newPath;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i > 0) newPath += ';';
        newPath += kept[i];
    }
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

static void erase_all(std::string& text, const std::string& pattern) {
    if (pattern.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        text.erase(pos, pattern.size());
    }
}

static void remove_bin_from_path() {
    fs::path binDir = get_bin_dir();
    std::vector<std::string> rcFiles = {".bashrc", ".zshrc", ".profile"};
    fs::path home = get_home_dir();
    const std::string block = "# snap - alias manager\nexport PATH=\"" + binDir.string() + ":$PATH\"\n";

    for (const auto& rc : rcFiles) {
        fs::path rcPath = home / rc;
        if (!fs::exists(rcPath)) continue;

        std::ifstream in(rcPath);
        if (!in.is_open()) continue;

        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        in.close();

        std::string updated = content;
        erase_all(updated, "\n" + block);
        erase_all(updated, block);

        if (updated == content) continue;

        std::ofstream out(rcPath, std::ios::trunc);
        if (out.is_open()) {
            out << updated;
            out.close();
        }
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
    f << "setlocal\n";
    if (is_windows_executable_extension(fs::path(target))) {
        f << "\"" << target << "\" %*\n";
    } else {
        // Use file association for non-executable files (txt, uproject, etc.)
        f << "start \"\" \"" << target << "\" %*\n";
    }
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
    f << "target=" << shell_quote(target) << "\n";
    f << "if [ -x \"$target\" ] && [ ! -d \"$target\" ]; then\n";
    f << "  exec \"$target\" \"$@\"\n";
    f << "fi\n";
    f << "if command -v xdg-open >/dev/null 2>&1; then\n";
    f << "  xdg-open \"$target\" >/dev/null 2>&1 &\n";
    f << "  exit 0\n";
    f << "fi\n";
    f << "if command -v open >/dev/null 2>&1; then\n";
    f << "  open \"$target\" \"$@\"\n";
    f << "  exit $?\n";
    f << "fi\n";
    f << "echo \"error: no file opener available (xdg-open/open).\" >&2\n";
    f << "exit 1\n";
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
    std::cout << "    A lightweight tool to bind short aliases to files, folders, and apps.\n";
    std::cout << "    Example: snap game \"D:\\Games\\Project.uproject\"\n";
    std::cout << "    Then just type 'game' to launch it.\n\n";
    std::cout << "  Quick reference:\n";
    std::cout << "    snap <alias> <path>     Bind an alias to a target path\n";
    std::cout << "    snap list               List all your aliases\n";
    std::cout << "    snap remove <alias>     Remove an alias\n";
    std::cout << "    snap uninstall          Uninstall snap\n";
    std::cout << "    snap update             Update snap\n";
    std::cout << "    snap --help             Full help\n";
    std::cout << "  ---------------------------------------------------------\n";
    std::cout << "  Source: https://github.com/Thornvald/snap\n";
    std::cout << "  All data stored in: " << get_snap_dir().string() << "\n";
    std::cout << "  To uninstall, run: snap uninstall\n";
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
  snap - bind aliases to target paths

  Usage:
    snap <alias> <path>     Register an alias for a file, folder, or executable
    snap list               List all registered aliases
    snap remove <alias>     Remove a registered alias
    snap uninstall          Uninstall snap and remove all aliases
    snap update             Update snap to latest release
    snap --help, -h         Show this help message
    snap --version, -v      Show version

  Examples:
    snap game "D:\Games\Project.uproject"
    snap notes "C:\Users\me\Documents\todo.txt"
    snap list
    snap remove game
    snap uninstall
    snap update

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

static int cmd_uninstall() {
    std::cout << "  Uninstalling snap...\n";
    remove_bin_from_path();

#ifdef _WIN32
    fs::path snapDir = get_snap_dir();
    std::error_code ec;
    if (!fs::exists(snapDir, ec)) {
        std::cout << "  snap is already removed.\n";
        return 0;
    }

    // Remove aliases and registry first.
    for (const auto& entry : load_registry()) {
        remove_shim(entry.alias);
    }
    fs::remove(get_registry_path(), ec);

    // Schedule full directory removal after this process exits.
    std::string cmd = "cmd.exe /c ping 127.0.0.1 -n 2 >nul && rmdir /s /q \"" + snapDir.string() + "\"";
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::vector<char> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back('\0');

    BOOL started = CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                                  DETACHED_PROCESS | CREATE_NO_WINDOW,
                                  nullptr, nullptr, &si, &pi);

    if (started) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::cout << "  Removed PATH entry and scheduled deletion of " << snapDir.string() << "\n";
        std::cout << "  Open a new terminal. snap is now uninstalled.\n";
        return 0;
    }

    std::cerr << "error: could not schedule removal of " << snapDir.string() << "\n";
    std::cerr << "  Please delete that folder manually. PATH entry was removed.\n";
    return 1;
#else
    std::error_code ec;
    fs::remove_all(get_snap_dir(), ec);
    if (ec) {
        std::cerr << "error: could not remove " << get_snap_dir() << ": " << ec.message() << "\n";
        return 1;
    }

    std::cout << "  Removed PATH entry and deleted " << get_snap_dir().string() << "\n";
    std::cout << "  Open a new terminal. snap is now uninstalled.\n";
    return 0;
#endif
}

static int cmd_update() {
    const std::string url = get_latest_asset_url();
    if (url.empty()) {
        std::cerr << "error: this platform is not supported for snap update.\n";
        return 1;
    }

    std::error_code ec;
    fs::create_directories(get_bin_dir(), ec);
    if (ec) {
        std::cerr << "error: could not create " << get_bin_dir() << ": " << ec.message() << "\n";
        return 1;
    }

    const fs::path installedPath = get_installed_binary_path();
    const fs::path tempPath = get_snap_dir() /
#ifdef _WIN32
        "snap.update.exe";
#else
        "snap.update";
#endif

    std::cout << "  Checking latest release...\n";
    std::cout << "  Downloading: " << url << "\n";

#ifdef _WIN32
    std::string downloadCmd =
        "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
        "try { "
        "$ProgressPreference='SilentlyContinue'; "
        "Invoke-WebRequest -UseBasicParsing -Uri '" + url + "' -OutFile '" + tempPath.string() + "'; "
        "exit 0 "
        "} catch { "
        "exit 1 "
        "}\"";

    if (std::system(downloadCmd.c_str()) != 0 || !fs::exists(tempPath)) {
        std::cerr << "error: failed to download update package.\n";
        return 1;
    }

    fs::path selfPath = get_self_path();
    auto selfCanon = fs::weakly_canonical(selfPath, ec);
    ec.clear();
    auto installedCanon = fs::weakly_canonical(installedPath, ec);
    bool runningInstalled = (!ec && selfCanon == installedCanon);

    if (runningInstalled) {
        std::string scriptCmd =
            "cmd.exe /c ping 127.0.0.1 -n 2 >nul && move /Y \"" + tempPath.string() + "\" \"" + installedPath.string() +
            "\" >nul";

        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        std::vector<char> cmdLine(scriptCmd.begin(), scriptCmd.end());
        cmdLine.push_back('\0');

        BOOL started = CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                                      DETACHED_PROCESS | CREATE_NO_WINDOW,
                                      nullptr, nullptr, &si, &pi);
        if (!started) {
            std::cerr << "error: update downloaded but could not schedule replacement.\n";
            std::cerr << "  Temporary file: " << tempPath << "\n";
            return 1;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        ensure_bin_in_path();
        std::cout << "  Update downloaded.\n";
        std::cout << "  Close this terminal and open a new one.\n";
        std::cout << "  Run 'snap --version' to verify.\n";
        return 0;
    }

    fs::copy_file(tempPath, installedPath, fs::copy_options::overwrite_existing, ec);
    fs::remove(tempPath, ec);
    if (ec) {
        std::cerr << "error: could not install update: " << ec.message() << "\n";
        return 1;
    }

    ensure_bin_in_path();
    std::cout << "  Updated snap at " << installedPath.string() << "\n";
    std::cout << "  Run 'snap --version' to verify.\n";
    return 0;
#else
    std::string downloadCmd =
        "curl -fsSL \"" + url + "\" -o \"" + tempPath.string() + "\"";
    int code = std::system(downloadCmd.c_str());
    if (code != 0 || !fs::exists(tempPath)) {
        downloadCmd = "wget -qO \"" + tempPath.string() + "\" \"" + url + "\"";
        code = std::system(downloadCmd.c_str());
    }

    if (code != 0 || !fs::exists(tempPath)) {
        std::cerr << "error: failed to download update package (tried curl/wget).\n";
        return 1;
    }

    chmod(tempPath.c_str(), 0755);

    fs::rename(tempPath, installedPath, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(tempPath, installedPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "error: could not install update: " << ec.message() << "\n";
            return 1;
        }
        fs::remove(tempPath, ec);
    }

    chmod(installedPath.c_str(), 0755);
    ensure_bin_in_path();
    std::cout << "  Updated snap at " << installedPath.string() << "\n";
    std::cout << "  Run 'snap --version' to verify.\n";
    return 0;
#endif
}

// ── main ──────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string cmd = argc >= 2 ? argv[1] : "";

    // Self-install on first run: copy binary to ~/.snap/bin/ and add to PATH.
    // If installation just happened, exit so user opens a new terminal.
    if (cmd != "uninstall" && cmd != "update" && self_install()) {
        return 0;
    }

    if (argc < 2) {
        print_help();
        return 0;
    }

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

    if (cmd == "uninstall") {
        return cmd_uninstall();
    }

    if (cmd == "update") {
        return cmd_update();
    }

    // Default: snap <alias> <path>
    if (argc < 3) {
        std::cerr << "error: usage: snap <alias> <path-to-target>\n";
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
