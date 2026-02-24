#include "app.hpp"

#include "paths.hpp"
#include "registry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#else
  #include <sys/stat.h>
#endif

namespace snap {

namespace fs = std::filesystem;

static constexpr const char* VERSION = "1.0.4";

namespace {

std::string trim(const std::string& value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";

    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool files_identical(const fs::path& lhs, const fs::path& rhs) {
    std::error_code ec;
    if (!fs::exists(lhs, ec) || !fs::exists(rhs, ec)) return false;

    const auto lhs_size = fs::file_size(lhs, ec);
    if (ec) return false;
    const auto rhs_size = fs::file_size(rhs, ec);
    if (ec || lhs_size != rhs_size) return false;

    std::ifstream a(lhs, std::ios::binary);
    std::ifstream b(rhs, std::ios::binary);
    if (!a.is_open() || !b.is_open()) return false;

    std::array<char, 8192> abuf{};
    std::array<char, 8192> bbuf{};
    while (a && b) {
        a.read(abuf.data(), static_cast<std::streamsize>(abuf.size()));
        b.read(bbuf.data(), static_cast<std::streamsize>(bbuf.size()));

        const std::streamsize acount = a.gcount();
        const std::streamsize bcount = b.gcount();
        if (acount != bcount) return false;
        if (acount <= 0) break;

        if (!std::equal(abuf.begin(), abuf.begin() + acount, bbuf.begin())) {
            return false;
        }
    }

    return true;
}

#ifdef _WIN32

bool is_windows_executable_extension(const fs::path& target_path) {
    std::string ext = target_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".exe" || ext == ".com" || ext == ".bat" || ext == ".cmd";
}

std::string get_user_path() {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return "";
    }

    char buffer[32767];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    std::string result;

    if (RegQueryValueExA(key, "Path", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS) {
        result = std::string(buffer, size > 0 ? size - 1 : 0);
    }
    RegCloseKey(key);
    return result;
}

void set_user_path(const std::string& new_path) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_WRITE, &key) != ERROR_SUCCESS) {
        std::cerr << "error: could not open user environment registry key.\n";
        return;
    }

    RegSetValueExA(key, "Path", 0, REG_EXPAND_SZ,
                   reinterpret_cast<const BYTE*>(new_path.c_str()),
                   static_cast<DWORD>(new_path.size() + 1));
    RegCloseKey(key);

    DWORD_PTR result = 0;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>("Environment"),
                        SMTO_ABORTIFHUNG, 5000, &result);
}

std::string normalize_windows_path_token(std::string token) {
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

bool path_contains(const std::string& path_var, const std::string& dir) {
    const std::string normalized_dir = normalize_windows_path_token(dir);

    std::istringstream ss(path_var);
    std::string token;
    while (std::getline(ss, token, ';')) {
        if (normalize_windows_path_token(token) == normalized_dir) {
            return true;
        }
    }
    return false;
}

#else

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

void erase_all(std::string& text, const std::string& pattern) {
    if (pattern.empty()) return;

    size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        text.erase(pos, pattern.size());
    }
}

#endif

void ensure_bin_in_path() {
#ifdef _WIN32
    const std::string bin_dir = get_bin_dir().string();
    const std::string user_path = get_user_path();
    if (path_contains(user_path, bin_dir)) return;

    std::string new_path = user_path;
    if (!new_path.empty() && new_path.back() != ';') new_path += ';';
    new_path += bin_dir;
    set_user_path(new_path);
#else
    const fs::path bin_dir = get_bin_dir();
    fs::create_directories(bin_dir);

    const char* path_env = std::getenv("PATH");
    if (path_env) {
        std::istringstream ss(path_env);
        std::string token;
        while (std::getline(ss, token, ':')) {
            if (token == bin_dir.string()) return;
        }
    }

    const std::string export_line = "\n# snap - alias manager\nexport PATH=\"" + bin_dir.string() + ":$PATH\"\n";
    const std::vector<std::string> rc_files = {".bashrc", ".zshrc", ".profile"};
    const fs::path home = get_home_dir();

    for (const auto& rc : rc_files) {
        const fs::path rc_path = home / rc;
        if (!fs::exists(rc_path)) continue;

        std::ifstream in(rc_path);
        if (!in.is_open()) continue;
        const std::string content((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());

        if (content.find(bin_dir.string()) != std::string::npos) continue;

        std::ofstream out(rc_path, std::ios::app);
        if (out.is_open()) {
            out << export_line;
        }
    }
#endif
}

void remove_bin_from_path() {
#ifdef _WIN32
    const std::string user_path = get_user_path();
    if (user_path.empty()) return;

    const std::string bin_norm = normalize_windows_path_token(get_bin_dir().string());
    std::istringstream ss(user_path);
    std::string token;
    std::vector<std::string> kept;
    bool removed = false;

    while (std::getline(ss, token, ';')) {
        const std::string cleaned = trim(token);
        if (cleaned.empty()) continue;

        if (normalize_windows_path_token(cleaned) == bin_norm) {
            removed = true;
            continue;
        }
        kept.push_back(cleaned);
    }

    if (!removed) return;

    std::string new_path;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i > 0) new_path += ';';
        new_path += kept[i];
    }
    set_user_path(new_path);
#else
    const fs::path bin_dir = get_bin_dir();
    const fs::path home = get_home_dir();
    const std::vector<std::string> rc_files = {".bashrc", ".zshrc", ".profile"};
    const std::string block = "# snap - alias manager\nexport PATH=\"" + bin_dir.string() + ":$PATH\"\n";

    for (const auto& rc : rc_files) {
        const fs::path rc_path = home / rc;
        if (!fs::exists(rc_path)) continue;

        std::ifstream in(rc_path);
        if (!in.is_open()) continue;

        const std::string content((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());

        std::string updated = content;
        erase_all(updated, "\n" + block);
        erase_all(updated, block);

        if (updated == content) continue;

        std::ofstream out(rc_path, std::ios::trunc);
        if (out.is_open()) {
            out << updated;
        }
    }
#endif
}

void create_shim(const std::string& alias, const std::string& target) {
#ifdef _WIN32
    const fs::path shim_path = get_bin_dir() / (alias + ".cmd");
    fs::create_directories(get_bin_dir());

    std::ofstream out(shim_path);
    if (!out.is_open()) {
        std::cerr << "error: could not create shim at " << shim_path << "\n";
        return;
    }

    out << "@echo off\n";
    out << "setlocal\n";
    if (is_windows_executable_extension(fs::path(target))) {
        out << "\"" << target << "\" %*\n";
    } else {
        out << "start \"\" \"" << target << "\" %*\n";
    }
#else
    const fs::path shim_path = get_bin_dir() / alias;
    fs::create_directories(get_bin_dir());

    std::ofstream out(shim_path);
    if (!out.is_open()) {
        std::cerr << "error: could not create shim at " << shim_path << "\n";
        return;
    }

    out << "#!/bin/sh\n";
    out << "target=" << shell_quote(target) << "\n";
    out << "if [ -x \"$target\" ] && [ ! -d \"$target\" ]; then\n";
    out << "  exec \"$target\" \"$@\"\n";
    out << "fi\n";
    out << "if command -v xdg-open >/dev/null 2>&1; then\n";
    out << "  xdg-open \"$target\" >/dev/null 2>&1 &\n";
    out << "  exit 0\n";
    out << "fi\n";
    out << "if command -v open >/dev/null 2>&1; then\n";
    out << "  open \"$target\" \"$@\"\n";
    out << "  exit $?\n";
    out << "fi\n";
    out << "echo \"error: no file opener available (xdg-open/open).\" >&2\n";
    out << "exit 1\n";
    out.close();

    chmod(shim_path.c_str(), 0755);
#endif
}

void remove_shim(const std::string& alias) {
#ifdef _WIN32
    const fs::path shim_path = get_bin_dir() / (alias + ".cmd");
#else
    const fs::path shim_path = get_bin_dir() / alias;
#endif
    std::error_code ec;
    fs::remove(shim_path, ec);
}

fs::path get_self_path() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return fs::path(buffer);
    }
    return fs::path();
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p;
    return fs::path();
#endif
}

bool self_install(bool interactive_no_args) {
    const fs::path self_path = get_self_path();
    if (self_path.empty() || !fs::exists(self_path)) return false;

    const fs::path installed_path = get_installed_binary_path();
    std::error_code ec;
    const bool installed_exists = fs::exists(installed_path, ec);

    if (installed_exists) {
        ec.clear();
        const fs::path self_canon = fs::weakly_canonical(self_path, ec);
        ec.clear();
        const fs::path installed_canon = fs::weakly_canonical(installed_path, ec);
        if (!ec && self_canon == installed_canon) return false;
    }

    if (!installed_exists) {
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

        std::cout << "  [2/3] Copy snap binary:  " << self_path.string() << "\n";
        std::cout << "        To:                " << installed_path.string() << "\n";
        fs::copy_file(self_path, installed_path, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "        FAILED: " << ec.message() << "\n";
            return false;
        }
#ifndef _WIN32
        chmod(installed_path.c_str(), 0755);
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
        std::cout << "  To uninstall, run: snap uninstall\n\n";

#ifdef _WIN32
        if (interactive_no_args) {
            std::cout << "  Press any key to exit...\n";
            std::system("pause >nul 2>&1");
            return true;
        }
#endif
        return false;
    }

    fs::create_directories(get_bin_dir(), ec);
    ec.clear();
    fs::copy_file(self_path, installed_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "error: could not sync installed snap binary: " << ec.message() << "\n";
        return false;
    }
#ifndef _WIN32
    chmod(installed_path.c_str(), 0755);
#endif
    ensure_bin_in_path();
    return false;
}

void print_help() {
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

void print_version() {
    std::cout << "snap " << VERSION << "\n";
}

int cmd_add(const std::string& alias, const std::string& target) {
    if (alias.empty()) {
        std::cerr << "error: alias cannot be empty.\n";
        return 1;
    }

    for (char c : alias) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
            std::cerr << "error: alias can only contain alphanumeric characters, hyphens, and underscores.\n";
            return 1;
        }
    }

    const fs::path target_path(target);
    if (!fs::exists(target_path)) {
        std::cerr << "error: target path does not exist: " << target << "\n";
        return 1;
    }

    ensure_bin_in_path();

    std::vector<AliasEntry> entries = load_registry();
    bool updated = false;
    for (auto& entry : entries) {
        if (entry.alias == alias) {
            entry.target = target;
            updated = true;
            break;
        }
    }

    if (!updated) {
        entries.push_back({alias, target});
    }

    save_registry(entries);
    create_shim(alias, target);

    if (updated) {
        std::cout << "  Updated alias '" << alias << "' -> " << target << "\n";
    } else {
        std::cout << "  Created alias '" << alias << "' -> " << target << "\n";
    }
    return 0;
}

int cmd_list() {
    const std::vector<AliasEntry> entries = load_registry();
    if (entries.empty()) {
        std::cout << "  No aliases registered. Use 'snap <alias> <path>' to add one.\n";
        return 0;
    }

    size_t max_len = 0;
    for (const auto& entry : entries) {
        if (entry.alias.size() > max_len) max_len = entry.alias.size();
    }

    std::cout << "\n  Registered aliases (" << entries.size() << "):\n\n";
    for (const auto& entry : entries) {
        const bool exists = fs::exists(entry.target);
        std::cout << "    " << entry.alias;
        for (size_t i = entry.alias.size(); i < max_len; ++i) std::cout << ' ';
        std::cout << "  ->  " << entry.target;
        if (!exists) std::cout << " [missing]";
        std::cout << "\n";
    }
    std::cout << "\n";
    return 0;
}

int cmd_remove(const std::string& alias) {
    std::vector<AliasEntry> entries = load_registry();
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const AliasEntry& entry) { return entry.alias == alias; });
    if (it == entries.end()) {
        std::cerr << "error: alias '" << alias << "' not found.\n";
        return 1;
    }

    const std::string old_target = it->target;
    entries.erase(it);

    save_registry(entries);
    remove_shim(alias);

    std::cout << "  Removed alias '" << alias << "' (was -> " << old_target << ")\n";
    return 0;
}

int cmd_uninstall() {
    std::cout << "  Uninstalling snap...\n";
    remove_bin_from_path();

#ifdef _WIN32
    const fs::path snap_dir = get_snap_dir();
    std::error_code ec;
    if (!fs::exists(snap_dir, ec)) {
        std::cout << "  snap is already removed.\n";
        return 0;
    }

    for (const auto& entry : load_registry()) {
        remove_shim(entry.alias);
    }
    fs::remove(get_registry_path(), ec);

    const std::string command = "cmd.exe /c ping 127.0.0.1 -n 2 >nul && rmdir /s /q \"" + snap_dir.string() + "\"";

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::vector<char> cmd_line(command.begin(), command.end());
    cmd_line.push_back('\0');

    const BOOL started = CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                                        DETACHED_PROCESS | CREATE_NO_WINDOW,
                                        nullptr, nullptr, &si, &pi);
    if (!started) {
        std::cerr << "error: could not schedule removal of " << snap_dir.string() << "\n";
        std::cerr << "  Please delete that folder manually. PATH entry was removed.\n";
        return 1;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "  Removed PATH entry and scheduled deletion of " << snap_dir.string() << "\n";
    std::cout << "  Open a new terminal. snap is now uninstalled.\n";
    return 0;
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

int cmd_update() {
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

    const fs::path installed_path = get_installed_binary_path();
#ifdef _WIN32
    const fs::path temp_path = get_snap_dir() / "snap.update.exe";
#else
    const fs::path temp_path = get_snap_dir() / "snap.update";
#endif

    std::cout << "  Checking latest release...\n";
    std::cout << "  Downloading: " << url << "\n";

#ifdef _WIN32
    const std::string download_cmd =
        "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
        "try { $ProgressPreference='SilentlyContinue'; "
        "Invoke-WebRequest -UseBasicParsing -Uri '" + url + "' -OutFile '" + temp_path.string() + "'; "
        "exit 0 } catch { exit 1 }\"";

    if (std::system(download_cmd.c_str()) != 0 || !fs::exists(temp_path)) {
        std::cerr << "error: failed to download update package.\n";
        return 1;
    }

    if (files_identical(temp_path, installed_path)) {
        fs::remove(temp_path, ec);
        ensure_bin_in_path();
        std::cout << "  You already have the latest version (" << VERSION << ").\n";
        return 0;
    }

    const fs::path self_path = get_self_path();
    fs::path self_canon = fs::weakly_canonical(self_path, ec);
    ec.clear();
    fs::path installed_canon = fs::weakly_canonical(installed_path, ec);
    const bool running_installed = (!ec && self_canon == installed_canon);

    if (running_installed) {
        const std::string replace_cmd =
            "cmd.exe /c ping 127.0.0.1 -n 2 >nul && move /Y \"" + temp_path.string() + "\" \"" +
            installed_path.string() + "\" >nul";

        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);

        std::vector<char> cmd_line(replace_cmd.begin(), replace_cmd.end());
        cmd_line.push_back('\0');

        const BOOL started = CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                                            DETACHED_PROCESS | CREATE_NO_WINDOW,
                                            nullptr, nullptr, &si, &pi);
        if (!started) {
            std::cerr << "error: update downloaded but could not schedule replacement.\n";
            std::cerr << "  Temporary file: " << temp_path << "\n";
            return 1;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        ensure_bin_in_path();
        std::cout << "  Update downloaded and queued.\n";
        std::cout << "  It will apply right after this process exits.\n";
        std::cout << "  Run 'snap --version' to verify.\n";
        return 0;
    }

    fs::copy_file(temp_path, installed_path, fs::copy_options::overwrite_existing, ec);
    fs::remove(temp_path, ec);
    if (ec) {
        std::cerr << "error: could not install update: " << ec.message() << "\n";
        return 1;
    }

    ensure_bin_in_path();
    std::cout << "  Updated snap at " << installed_path.string() << "\n";
    std::cout << "  Run 'snap --version' to verify.\n";
    return 0;
#else
    std::string download_cmd = "curl -fsSL \"" + url + "\" -o \"" + temp_path.string() + "\"";
    int code = std::system(download_cmd.c_str());
    if (code != 0 || !fs::exists(temp_path)) {
        download_cmd = "wget -qO \"" + temp_path.string() + "\" \"" + url + "\"";
        code = std::system(download_cmd.c_str());
    }

    if (code != 0 || !fs::exists(temp_path)) {
        std::cerr << "error: failed to download update package (tried curl/wget).\n";
        return 1;
    }

    if (files_identical(temp_path, installed_path)) {
        fs::remove(temp_path, ec);
        ensure_bin_in_path();
        std::cout << "  You already have the latest version (" << VERSION << ").\n";
        return 0;
    }

    chmod(temp_path.c_str(), 0755);
    fs::rename(temp_path, installed_path, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(temp_path, installed_path, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "error: could not install update: " << ec.message() << "\n";
            return 1;
        }
        fs::remove(temp_path, ec);
    }

    chmod(installed_path.c_str(), 0755);
    ensure_bin_in_path();
    std::cout << "  Updated snap at " << installed_path.string() << "\n";
    std::cout << "  Run 'snap --version' to verify.\n";
    return 0;
#endif
}

} // namespace

enum class CommandKind {
    kNone,
    kHelp,
    kVersion,
    kList,
    kRemove,
    kUninstall,
    kUpdate,
    kAdd,
};

CommandKind parse_command_kind(const std::string& command) {
    if (command.empty()) return CommandKind::kNone;

    static const std::unordered_map<std::string, CommandKind> table = {
        {"--help", CommandKind::kHelp},
        {"-h", CommandKind::kHelp},
        {"--version", CommandKind::kVersion},
        {"-v", CommandKind::kVersion},
        {"list", CommandKind::kList},
        {"remove", CommandKind::kRemove},
        {"uninstall", CommandKind::kUninstall},
        {"update", CommandKind::kUpdate},
    };

    const auto it = table.find(command);
    if (it != table.end()) return it->second;
    return CommandKind::kAdd;
}

int handle_remove_command(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "error: usage: snap remove <alias>\n";
        return 1;
    }
    return cmd_remove(argv[2]);
}

int handle_add_command(int argc, char* argv[], const std::string& alias) {
    if (argc < 3) {
        std::cerr << "error: usage: snap <alias> <path-to-target>\n";
        std::cerr << "  Run 'snap --help' for more information.\n";
        return 1;
    }

    std::string target;
    for (int i = 2; i < argc; ++i) {
        if (i > 2) target += ' ';
        target += argv[i];
    }

    return cmd_add(alias, target);
}

int run(int argc, char* argv[]) {
    const std::string command = argc >= 2 ? argv[1] : "";
    const CommandKind kind = parse_command_kind(command);

    if (kind != CommandKind::kUninstall && kind != CommandKind::kUpdate &&
        self_install(kind == CommandKind::kNone)) {
        return 0;
    }

    switch (kind) {
        case CommandKind::kNone:
        case CommandKind::kHelp:
            print_help();
            return 0;
        case CommandKind::kVersion:
            print_version();
            return 0;
        case CommandKind::kList:
            return cmd_list();
        case CommandKind::kRemove:
            return handle_remove_command(argc, argv);
        case CommandKind::kUninstall:
            return cmd_uninstall();
        case CommandKind::kUpdate:
            return cmd_update();
        case CommandKind::kAdd:
            return handle_add_command(argc, argv, command);
        default:
            print_help();
            return 0;
    }
}

} // namespace snap
