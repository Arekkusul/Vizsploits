# Vizsploits

A real-time visualization platform for security exploitation techniques. Vizsploits is a C11 terminal application using ncurses that demonstrates exploit primitives (use-after-free, double-free, buffer overflow, etc.) through an interactive TUI with heap/stack views, event timelines, and educational annotations.

Designed for security researchers, educators, and CTF players.

## Features

- **Interactive TUI** with heap view, stack view, hex view, event timeline, and info panel
- **Step-by-step execution** of exploit demos with forward/backward navigation
- **5 built-in exploit demos**: Use-After-Free, Double-Free, Buffer Overflow, Integer Overflow, Format String
- **Education/lesson mode** with per-step annotations explaining vulnerabilities and mitigations
- **Timeline search and filtering** by event type or description text
- **Breakpoints** on event types (e.g., pause on any UAF or overflow event)
- **Memory diff view** for comparing heap snapshots side-by-side
- **Trace serialization** — save and replay exploit traces as JSON files
- **Lua scripting** — write custom exploit demos in Lua (optional, requires `liblua5.4-dev`)
- **Ptrace instrumentation** — trace real binaries with malloc/free interception, syscall tracing, and memory watch regions
- **Test suite** with 2294 tests covering core subsystems

## Quick Start

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install build-essential libncurses-dev

# Build
make

# Run interactive mode
./bin/kexploit-viz

# List available exploits
./bin/kexploit-viz --list
```

## Building

### Dependencies

```bash
# Required
sudo apt install build-essential libncurses-dev

# Optional (for Lua scripting support)
sudo apt install liblua5.4-dev pkg-config
```

### Build Targets

```bash
make              # Build → bin/kexploit-viz
make run          # Build and run interactive TUI
make test         # Build and run test suite
make debug        # Build with ASAN/UBSAN sanitizers
make clean        # Remove build artifacts
```

## Usage

### Interactive Mode (default)

```bash
./bin/kexploit-viz
```

Use arrow keys to select an exploit, Enter to run it, then step through events with Space.

### CLI Options

```
-h, --help          Show help
-l, --list          List available exploits
-e, --exploit N     Run exploit N headless (no TUI)
-r, --replay FILE   Replay a saved trace file in TUI
-s, --script FILE   Load a Lua exploit script
-d, --script-dir D  Load all .lua files from directory
-p, --ptrace BIN    Trace a real binary with ptrace
-w, --watch A:S     Watch memory at addr:size (hex, with --ptrace)
```

### Replay a Saved Trace

```bash
./bin/kexploit-viz -r trace.json
```

### Lua Scripting

```bash
./bin/kexploit-viz -s my_exploit.lua         # Load a single script
./bin/kexploit-viz -d scripts/               # Load all .lua files from directory
```

Lua scripts define an exploit table and lifecycle functions:

```lua
exploit = {
    name = "My Exploit",
    description = "Custom exploit demo"
}

function setup()
    viz.checkpoint(1, "Setup")
end

function run()
    local ptr = viz.alloc(64, "Allocate buffer")
    viz.write(ptr, "AAAA", "Write data")
    viz.free(ptr, "Free buffer")
    viz.use_after_free(ptr, "UAF!")
end

function cleanup() end
```

Available Lua API: `viz.alloc()`, `viz.free()`, `viz.realloc()`, `viz.write()`, `viz.read()`, `viz.use_after_free()`, `viz.double_free()`, `viz.overflow()`, `viz.checkpoint()`, `viz.note()`, `viz.call()`, `viz.ret()`

### Ptrace Mode

```bash
./bin/kexploit-viz -p /path/to/binary
./bin/kexploit-viz -p /path/to/binary -w 0x7fff0000:256   # Watch memory region
```

Ptrace mode automatically intercepts malloc/free/realloc calls, traces syscalls, and monitors watched memory regions for changes.

## TUI Keybindings

| Key | Action |
|-----|--------|
| Space | Step forward |
| B | Step backward |
| R | Run to next checkpoint/breakpoint |
| 0 | Reset to start |
| < / > | Switch panel focus |
| j / k | Scroll focused panel |
| / | Search timeline |
| f | Cycle type filter |
| F | Clear all filters |
| p | Toggle breakpoint picker |
| d | Memory diff (press once for snapshot A, again for B) |
| e | Toggle lesson/education mode |
| S | Save trace to JSON file |
| n | Single-step instruction (ptrace mode) |
| c | Continue execution (ptrace mode) |
| ? / H | Help screen |
| Q | Quit / back to menu |
| Esc | Cancel current action |

## Architecture

The codebase follows an **Event Bus Architecture**:

```
Exploits (prim_* wrappers) --> Event Bus --> Visualization Views --> ncurses TUI
```

### Directory Structure

```
src/
  core/              Event bus, primitives, serialization, main entry point
  exploits/
    api/             Exploit API, primitive wrappers, allocation tracker
    examples/        Built-in demo exploits
  visualization/     Heap view, stack view, timeline, hex view, memory diff, filters
  ui/                ncurses TUI with panel management
  education/         Lesson registry and built-in lesson annotations
  scripting/         Lua scripting bridge (conditional compilation)
  instrumentation/
    ptrace/          Ptrace backend, ELF symbol resolution
  utils/             Vendored libraries (cJSON)
tests/               Test suite (2294 tests)
```

### Key Design Patterns

- **Exploit auto-registration** via `__attribute__((constructor))`
- **Primitive wrappers** (`prim_alloc`, `prim_free`, etc.) emit events automatically through the event bus
- **Allocation tracking** for UAF/double-free detection
- **Conditional compilation** for optional Lua support (`#ifdef HAS_LUA`)
- **Event-driven visualization** — all views subscribe to the event bus and update on new events

## License

MIT
