# tgk editor

A tiny **multi-language IDE for Linux**, written in C++ with Qt6.
Dark/Light themes, one-key compile & run, and real IntelliSense for C/C++
powered by **clangd** (the same engine behind VS Code's C/C++ support).

## Features

- **Multi-language**: C, C++, Java, Python, JavaScript, TypeScript, Go,
  Rust, Shell, Ruby, PHP — auto-detected by file extension
- **F9 = Compile & Run** — one key, no terminal commands
- **First run per language asks once**, then remembers your toolchain commands
  in `~/.config/tgk/tgkeditor.json` (editable JSON; add your own languages!)
- **Code completion** for C/C++ (Ctrl+Space or automatic): real semantic
  suggestions from clangd
- **5 themes** (View → Theme), persisted between sessions:
  Dev-C++ Classic, Dark, Monokai, Solarized Light/Dark
- Syntax highlighting for C/C++/Java/Python with language-aware comment toggle
  (**Ctrl+\\**)
- Output pane with an **input line** for interactive programs (stdin)
- Problems pane: compiler/interpreter diagnostics; double-click to jump to line
- Line numbers, current-line highlight, toolbar buttons showing their shortcuts
- **Home page with recent files** — the app opens to a start screen showing the
  last 10 files you worked on; click one to reopen it instantly. Also available
  under File → Recent Files. Ctrl+W returns to the home page.

## Build & install

```bash
./install.sh          # builds + installs to ~/.local (adds app-menu entry)
```

or manually:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/tgkeditor myfile.cpp
```

Requires: Qt6 (Widgets, Gui), g++, clangd.

## Shortcuts

| Key | Action |
|---|---|
| F9 | Compile & run |
| F8 | Compile only |
| F10 | Stop program |
| Ctrl+\\ | Toggle comment (uses `//` or `#` per language) |
| Ctrl+Space | Code completion (C/C++) |
| Ctrl+S / Ctrl+O / Ctrl+N | Save / Open / New |
| Ctrl+W | Close file and return to the home page |

## Language configuration

`~/.config/tgk/tgkeditor.json` stores every language's compile/run commands:

```json
{
  "languages": {
    "python": {
      "displayName": "Python",
      "extensions": [".py", ".pyw"],
      "compile": "",
      "run": "python3 {file}",
      "confirmed": true
    }
  }
}
```

Placeholders: `{file}` `{dir}` `{name}` `{class}` `{output}`.
Set `"confirmed": false` (or delete an entry) to be asked again on next run.
Add your own language by appending an entry with a new extension list.

## Debugging tips

- `CPPIDE_TRACE=1` prints the raw clangd LSP traffic to stderr.
- `build/clangd_test` is a headless end-to-end test of the IntelliSense
  pipeline (should print `PASS`).
- `build/icon_gen out.png` renders the app icon.
