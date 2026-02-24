# snap

snap lets you bind a short alias to any executable on your system. Once bound, you can launch that app by typing the alias in any terminal.

```
snap <alias> <path-to-executable>
```

That's it.

## Installation

Download the binary for your platform from [Releases](https://github.com/Thornvald/snap/releases), run it once, and snap installs itself. Open a new terminal and you're ready.

### Build from source

Requirements: CMake 3.16+, a C++17 compiler (MSVC, GCC, Clang).

```bash
git clone https://github.com/Thornvald/snap.git
cd snap
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Usage

```
snap <alias> <path>       Bind an alias to an executable
snap list                 List all registered aliases
snap remove <alias>       Remove an alias
snap --help               Show help
snap --version            Show version
```

## What happens on first run?

When you run snap for the first time, it performs a one-time self-install. The terminal will show every step so you know exactly what is happening. Nothing runs silently.

It does three things:

1. **Creates `~/.snap/bin/`** - where snap and your alias shims live.
2. **Copies itself into `~/.snap/bin/`** - so you don't need to keep the downloaded file in a specific location.
3. **Adds `~/.snap/bin/` to your user PATH** - so you can type `snap` and your aliases from any terminal.

**On Windows**, PATH is updated via the user-level registry key (`HKCU\Environment\Path`). No administrator privileges are required. The terminal will wait for you to press a key before closing so you can read the output.

**On macOS/Linux**, an export line is appended to your shell rc files (`~/.bashrc`, `~/.zshrc`, `~/.profile`), only if the line is not already present.

After the first run, open a new terminal and snap works globally. The self-install only runs **once**. Subsequent runs go straight to normal operation.

> **This is not malware.** snap is open-source ([MIT licensed](LICENSE)), has zero network activity, and all data is stored locally in `~/.snap/`. You can inspect the [full source code](src/main.cpp). It's a single file with no external dependencies.

## How it works

1. `snap <alias> <path>` creates a thin launcher shim in `~/.snap/bin/`:
   - Windows: `<alias>.cmd` that calls the target exe
   - macOS/Linux: `<alias>` shell script that execs the target binary
2. A JSON registry at `~/.snap/aliases.json` tracks all bindings.
3. Since `~/.snap/bin/` is on your PATH, typing the alias launches the app.

## Alias rules

- Aliases can contain letters, numbers, hyphens (`-`), and underscores (`_`).
- If an alias already exists, it will be updated to the new path.
- Target must be a file, not a directory. Pass the full path to the executable.

## Uninstall

1. Run `snap list` and `snap remove` for each alias.
2. Remove the `~/.snap/` directory.
3. Remove the PATH entry:
   - Windows: remove `%USERPROFILE%\.snap\bin` from your user PATH environment variable.
   - macOS/Linux: remove the `export PATH=.../.snap/bin...` line from your shell rc files.

## License

[MIT](LICENSE)
