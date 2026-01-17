# Finder Plus

A modern, AI-enhanced, high-performance file manager for macOS built with Raylib.

Finder Plus is a keyboard-first file manager that combines the speed of native C99 code with the intelligence of Claude AI. It features semantic search, smart file organization, visual search via CLIP, and natural language commands while maintaining 60+ FPS performance.

## Features

### Core File Management
- **Three View Modes**: List, Grid, and Column (Miller columns) views
- **Keyboard-First**: Complete vim-style navigation (`j`/`k`/`h`/`l`) plus standard macOS shortcuts
- **Dual Pane Mode**: Side-by-side directory browsing (`F3`)
- **Tabs**: Multiple directories in one window
- **Quick Look Preview**: Image, text, code, and video file previews with in-pane playback

### AI Features (Claude API)
- **AI Command Bar**: Natural language file operations via Claude API (`Cmd+K`)
- **Smart Rename**: AI-powered batch rename suggestions based on file content
- **Document Summarization**: On-demand AI summaries with caching
- **File Organization**: AI-suggested folder structures and categorization

### Local AI (Privacy-Preserving)
- **Semantic Search**: Find files by meaning using local embeddings (all-MiniLM-L6-v2)
- **Visual Search**: Find similar images via CLIP embeddings (ViT-B/32)
- **Duplicate Detection**: Find exact and near-duplicate files (MD5/SHA256/perceptual hashing)

### Power User Features
- **Git Integration**: See file status, branch info, and quick commits
- **Network Locations**: SFTP connection support
- **Command Palette**: Quick access to all commands (`Cmd+Shift+P`)
- **Batch Operations**: Queue-based copy/move/delete with progress tracking
- **Custom Keybindings**: Fully configurable keyboard shortcuts

## Installation

### Prerequisites

- macOS 12.0 or later
- Xcode Command Line Tools
- CMake 3.20+
- libcurl (included with macOS)
- SQLite3 (included with macOS)
- libssh2 (install via Homebrew: `brew install libssh2`)

### Building from Source

```bash
# Clone the repository with submodules
git clone --recursive https://github.com/yourusername/finder-plus.git
cd finder-plus

# Build Raylib (one-time setup)
cd raylib/src
make PLATFORM=PLATFORM_DESKTOP
cd ../..

# Build Finder Plus
mkdir -p build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)

# Run
./finder-plus
```

### Running Tests

```bash
cd build
make test_runner
./test_runner
```

### Performance Tests

```bash
cd build
make perf_test
./perf_test
```

## Quick Start

1. **Launch**: Run `./finder-plus` from the build directory
2. **Navigate**: Use arrow keys or `j`/`k` to move, `Enter` or `l` to open
3. **Go Back**: Press `Backspace` or `h` to go to parent directory
4. **Switch Views**: `Cmd+1` (List), `Cmd+2` (Grid), `Cmd+3` (Columns)
5. **AI Commands**: Press `Cmd+K` and type natural language commands
6. **Search**: Press `/` to filter files in current directory
7. **Dual Pane**: Press `F3` to toggle side-by-side browsing

## Keyboard Shortcuts

See [docs/KEYBOARD.md](docs/KEYBOARD.md) for the complete keyboard reference.

### Essential Shortcuts

| Action | Shortcut |
|--------|----------|
| Navigate up/down | `j` / `k` or Arrow keys |
| Enter directory | `l` or `Enter` |
| Go to parent | `h` or `Backspace` |
| AI Command Bar | `Cmd+K` |
| Command Palette | `Cmd+Shift+P` |
| New Tab | `Cmd+T` |
| Close Tab | `Cmd+W` |
| Copy | `Cmd+C` |
| Paste | `Cmd+V` |
| Delete | `Cmd+Backspace` |
| Toggle Hidden Files | `Cmd+Shift+.` |
| Dual Pane Mode | `F3` or `Cmd+Shift+D` |
| Select All | `Cmd+A` |
| Multi-select | `Cmd+Click` or `Shift+Click` |

## Configuration

Configuration is stored at `~/.config/finder-plus/config.json`. See [docs/CONFIG.md](docs/CONFIG.md) for all options.

### AI Setup

To enable Claude AI features and Google's Nano Banana image generation, set your API key:

```bash
export CLAUDE_API_KEY="your-api-key-here"
export GEMINI_API_KEY="your-gemini-key-here"
```

Or add it to your config file. Local AI features (semantic search, visual search, duplicate detection) work without an API key.

## Documentation

- [User Guide](docs/USER_GUIDE.md) - Complete usage guide
- [Keyboard Shortcuts](docs/KEYBOARD.md) - Complete keyboard reference
- [Configuration](docs/CONFIG.md) - All configuration options
- [Building](docs/BUILD.md) - Detailed build instructions
- [Contributing](CONTRIBUTING.md) - How to contribute

## Project Status

Finder Plus is in active development. See [ROADMAP.md](ROADMAP.md) for the development plan.

**Current Version**: 0.8 (Phase 8 - Polish)

### Completed Phases

| Phase | Name | Description |
|-------|------|-------------|
| 1 | Foundation | Window, directory reading, vim-style navigation |
| 2 | Core Features | 3 view modes, sidebar, file operations |
| 3 | Enhanced UX | Tabs, search, preview, theming |
| 4 | AI Foundation | Claude API, tool registry, command bar |
| 5 | Local AI | Embeddings, CLIP, semantic search |
| 6 | AI Features | Smart rename, duplicates, organization |
| 7 | Power User | Git integration, batch ops, command palette |
| 8 | Polish | Dual pane, network locations, performance (in progress) |

## Architecture

Finder Plus is built with ~32,500 lines of C99 code across 99 source files:

- **Core**: File system operations, search, git integration
- **UI**: Raylib-based rendering (list/grid/column views, tabs, dialogs)
- **API**: Claude and Gemini API integration with tool use
- **AI**: Local embeddings (bert.cpp), vision (clip.cpp), vector database (SQLite)
- **Platform**: macOS-specific (FSEvents, clipboard, SFTP)

## Performance

| Metric | Target | Status |
|--------|--------|--------|
| Directory load (10K files) | < 500ms | Achieved |
| Scrolling FPS | 60+ FPS | Achieved |
| Memory (typical) | < 100MB | Achieved |
| Memory (with AI) | < 600MB | Achieved |
| Startup time | < 200ms | Achieved |
| Semantic search (10K files) | < 100ms | Achieved |

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- [Raylib](https://www.raylib.com/) - Simple and easy-to-use graphics library
- [cJSON](https://github.com/DaveGamble/cJSON) - Ultralightweight JSON parser
- [bert.cpp](https://github.com/skeskinen/bert.cpp) - Text embeddings via ggml
- [clip.cpp](https://github.com/monatis/clip.cpp) - CLIP image embeddings via ggml
- [Anthropic Claude](https://www.anthropic.com/) - AI assistant API
- [libssh2](https://www.libssh2.org/) - SSH2 protocol library
