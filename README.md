# snap

Bind aliases to executables. Launch any app by typing a short name.

```
snap evr "C:\Program Files (x86)\Everything\Everything.exe"
```

Now just type `evr` in any terminal to launch Everything.

## Features

- **One command to alias** - `snap <alias> <path>` and you're done
- **Cross-platform** - Windows, macOS, Linux (x64 & ARM64)
- **Auto PATH setup** - snap configures your PATH on first run, no manual steps
- **Zero dependencies** - single static binary, C++17, no runtime needed
- **Lightweight** - ~100 KB binary, no background services
- **Registry** - JSON-based alias registry at `~/.snap/aliases.json`

## Installation

### Download and run (recommended)

1. Download the binary for your platform from [Releases](https://github.com/Thornvald/snap/releases).
2. Run it once. That's it.

| Platform | Binary |
|----------|--------|
| Windows x64 | `snap.exe` |
| macOS (Apple Silicon / Intel) | `snap` |
| Linux x64 | `snap` |
| Linux ARM64 | `snap` |

On first run, snap performs a **one-time self-install**. It will tell you exactly what it's doing in the terminal. See [What happens on first run?](#what-happens-on-first-run) below.

After that, open a new terminal and `snap` is ready to use from anywhere.

### Build from source

Requirements: CMake 3.16+, a C++17 compiler (MSVC, GCC, Clang).

```bash
git clone https://github.com/Thornvald/snap.git
cd snap
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The binary will be at `build/Release/snap.exe` (Windows) or `build/snap` (macOS/Linux).

## Usage

### Add an alias

```bash
snap evr "C:\Program Files (x86)\Everything\Everything.exe"
snap code "/usr/local/bin/code"
snap firefox "/usr/bin/firefox"
```

### List all aliases

```bash
snap list
```

Output:

```
  Registered aliases (2):

    evr      ->  C:\Program Files (x86)\Everything\Everything.exe
    firefox  ->  /usr/bin/firefox
```

### Remove an alias

```bash
snap remove evr
```

### Help

```bash
snap --help
snap --version
```

## What happens on first run?

When you run `snap` for the first time from wherever you downloaded it, it performs a **one-time self-install**. The terminal will show every step so you know exactly what is happening. Nothing runs silently.

It does three things:

1. **Creates `~/.snap/bin/`** - where snap and your alias shims live.
2. **Copies itself into `~/.snap/bin/`** - so you don't need to keep the downloaded file in a specific location.
3. **Adds `~/.snap/bin/` to your user PATH** - so you can type `snap` (and your aliases) from any terminal.

**On Windows**, PATH is updated via the user-level registry key (`HKCU\Environment\Path`). No administrator privileges are required. The terminal will wait for you to press a key before closing so you can read the output.

**On macOS/Linux**, an export line is appended to your shell rc files (`~/.bashrc`, `~/.zshrc`, `~/.profile`), only if the line is not already present.

After the first run, open a new terminal and snap works globally. The self-install only runs **once**. Subsequent runs go straight to normal operation.

> **This is not malware.** snap is open-source ([MIT licensed](LICENSE)), has zero network activity, and all data is stored locally in `~/.snap/`. You can inspect the [full source code](src/main.cpp). It's a single file with no external dependencies.

## How it works

1. **`snap <alias> <path>`** creates a thin launcher shim in `~/.snap/bin/`:
   - Windows: `<alias>.cmd` that calls the target exe
   - macOS/Linux: `<alias>` shell script that execs the target binary
2. A JSON registry at `~/.snap/aliases.json` tracks all bindings.
3. Since `~/.snap/bin/` is on your PATH, typing the alias name runs the shim, which launches the target app.

Open a new terminal after the first-run setup for PATH changes to take effect.

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
