# Kernel Exploit Visualizer

Real-time visualization platform for exploitation techniques. Built for security researchers, educators, and CTF players.

## Phase 1 MVP

This is the Phase 1 MVP implementing:

- **Event Bus Architecture**: Primitives-based event system for decoupled visualization
- **Exploit API**: C API with automatic event emission for exploit development
- **UAF Demo**: Working Use-After-Free demonstration
- **ncurses TUI**: Interactive terminal UI with heap view and timeline

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

## Usage

### Interactive Mode
```bash
./bin/kexploit-viz
```
- Use arrow keys to select exploit
- Press Enter to run
- Press Space to step through events
- Press R to run to next checkpoint
- Press Q to quit

### CLI Options
```
-h, --help       Show help
-l, --list       List available exploits
-e, --exploit N  Run exploit N (non-interactive)
```

## Architecture

```
┌─────────────────────────────────────────┐
│  Presentation Layer (ncurses TUI)       │
│  - Heap View                            │
│  - Timeline                             │
│  - Event Details                        │
└─────────────────────────────────────────┘
                    ↕
┌─────────────────────────────────────────┐
│  Event Bus                              │
│  - ALLOC, FREE, READ, WRITE, UAF, etc.  │
└─────────────────────────────────────────┘
                    ↕
┌─────────────────────────────────────────┐
│  Exploit API                            │
│  - prim_alloc(), prim_free(), etc.      │
│  - Automatic event emission             │
└─────────────────────────────────────────┘
```

## Project Structure

```
src/
├── core/           # Event bus, primitives
├── exploits/       # Exploit API and demos
├── visualization/  # Heap view, timeline
└── ui/             # ncurses TUI
```

## License

MIT
