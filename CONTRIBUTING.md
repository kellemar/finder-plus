# Contributing to Finder Plus

Thank you for your interest in contributing to Finder Plus! This document provides guidelines and information for contributors.

## Getting Started

1. Fork the repository
2. Clone your fork with submodules:
   ```bash
   git clone --recursive https://github.com/yourusername/finder-plus.git
   ```
3. Set up the development environment (see [docs/BUILD.md](docs/BUILD.md))
4. Create a feature branch:
   ```bash
   git checkout -b feature/your-feature-name
   ```

## Development Environment

### Prerequisites

- macOS 12.0+
- Xcode Command Line Tools
- CMake 3.20+
- libssh2 (`brew install libssh2`)

### Building

```bash
# Build raylib (first time only)
cd raylib/src && make PLATFORM=PLATFORM_DESKTOP && cd ../..

# Build project
mkdir -p build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

### Running Tests

Always run tests before submitting:
```bash
cd build
make test_runner
./test_runner
```

All tests must pass.

### Performance Tests

```bash
cd build
make perf_test
./perf_test
```

## Code Style

### C99 Standard

Finder Plus uses C99. Key conventions:

```c
// Use snake_case for functions and variables
void directory_read(DirectoryState *state, const char *path);
int selected_index;

// Use PascalCase for types
typedef struct {
    char name[256];
    bool is_directory;
} FileEntry;

// Use SCREAMING_SNAKE_CASE for constants and macros
#define MAX_PATH_LEN 4096
#define SIDEBAR_WIDTH 200
```

### Formatting

- 4 spaces for indentation (no tabs)
- Opening braces on same line
- Space after keywords (`if`, `for`, `while`)
- No space between function name and parenthesis

```c
// Good
if (condition) {
    do_something();
}

// Bad
if(condition){
    do_something();
}
```

### Comments

- Use `//` for single-line comments
- Use `/* */` for multi-line comments
- Document public functions in headers

```c
// Single-line comment

/*
 * Multi-line comment
 * explaining something complex
 */

/**
 * Read directory contents into state.
 * @param state Directory state to populate
 * @param path Path to directory
 * @return true on success, false on error
 */
bool directory_read(DirectoryState *state, const char *path);
```

### Error Handling

- Check return values
- Use meaningful error messages
- Prefer early returns for error cases

```c
bool file_copy(const char *src, const char *dst) {
    if (!src || !dst) {
        return false;
    }

    FILE *f = fopen(src, "rb");
    if (!f) {
        snprintf(error_msg, sizeof(error_msg),
                 "Cannot open source: %s", strerror(errno));
        return false;
    }

    // ... rest of implementation
}
```

## Project Structure

```
src/
├── main.c                  # Entry point
├── app.c/h                 # Application state (App struct)
├── api/                    # External API integration
│   ├── http_client.*       # libcurl wrapper for HTTP requests
│   ├── claude_client.*     # Claude Messages API with tool use
│   ├── gemini_client.*     # Gemini API for image operations
│   └── auth.*              # API key loading (env vars, config)
├── tools/                  # AI tool system
│   ├── tool_registry.*     # Tool definitions (file_list, file_move, etc.)
│   └── tool_executor.*     # Tool execution and result handling
├── ai/                     # Local AI features
│   ├── embeddings.*        # Text embeddings (all-MiniLM via bert.cpp)
│   ├── vectordb.*          # SQLite-based vector storage
│   ├── indexer.*           # Background file indexing with FSEvents
│   ├── semantic_search.*   # Vector similarity search
│   ├── clip.*              # Image embeddings (CLIP ViT-B/32)
│   ├── visual_search.*     # Image similarity search
│   ├── duplicates.*        # MD5/SHA256/perceptual hashing
│   ├── smart_rename.*      # AI-powered rename suggestions
│   ├── organization.*      # File categorization
│   ├── summarize.*         # Document summarization with caching
│   ├── summarize_async.*   # Thread-safe async summarization
│   └── nl_operations.*     # Natural language command parsing
├── core/                   # Core file management
│   ├── filesystem.*        # Directory reading, FileEntry struct
│   ├── operations.*        # Copy/move/delete/rename
│   ├── operation_queue.*   # Batch operation queueing
│   ├── search.*            # Fuzzy filename search
│   ├── git.*               # Git status integration
│   └── network.*           # SFTP connection support
├── ui/                     # Raylib UI components
│   ├── browser.*           # List/grid/column file views
│   ├── sidebar.*           # Favorites and volumes panel
│   ├── tabs.*              # Tab management
│   ├── dual_pane.*         # Side-by-side browsing
│   ├── preview.*           # File preview panel
│   ├── video.*             # Video preview and playback
│   ├── command_bar.*       # AI command input (Cmd+K)
│   ├── palette.*           # Command palette (Cmd+Shift+P)
│   ├── context_menu.*      # Right-click context menus
│   ├── dialog.*            # Generic dialog system
│   ├── breadcrumb.*        # Path navigation
│   ├── statusbar.*         # Status display
│   ├── queue_panel.*       # Batch operation queue display
│   ├── file_view_modal.*   # Full-screen file viewing
│   └── progress_indicator.* # Spinner/progress animations
├── platform/               # macOS-specific code
│   ├── fsevents.*          # File system change monitoring
│   └── clipboard.m         # macOS pasteboard (Objective-C)
└── utils/
    ├── config.*            # JSON config loading/saving
    ├── theme.*             # Color definitions
    ├── keybindings.*       # Keyboard shortcut mapping
    ├── perf.*              # Performance profiling
    ├── text.*              # Text wrapping utilities
    └── font.*              # Font loading
```

## Adding Features

### New UI Component

1. Create `src/ui/yourcomponent.c` and `src/ui/yourcomponent.h`
2. Add to `CMakeLists.txt` SOURCES list
3. Include header in `app.h` if needed
4. Add state to `App` struct if needed
5. Call from `app_draw()` and `app_handle_input()`
6. Write tests in `tests/test_yourcomponent.c`

### New AI Tool

1. Add tool definition in `src/tools/tool_registry.c`
2. Add executor in `src/tools/tool_executor.c`
3. Register in `tool_registry_register_file_tools()`
4. Write tests in `tests/test_tool_executor.c`

### New Keyboard Action

1. Add action enum in `src/utils/keybindings.h`
2. Add action name in `keybindings.c` action_names array
3. Add default binding in `keybindings.c` default_bindings array
4. Handle action in `app.c` input handling

### New Local AI Feature

1. Create files in `src/ai/` directory
2. Use `embeddings.h` or `clip.h` for model access
3. Use `vectordb.h` for vector storage if needed
4. Add to CMakeLists.txt SOURCES
5. Write tests in `tests/test_ai.c` or dedicated test file

## Testing

### Test Framework

We use a custom test framework with these macros:

```c
#include "test_framework.h"

void test_my_feature(void) {
    TEST_ASSERT(condition);
    TEST_ASSERT_EQ(actual, expected);
    TEST_ASSERT_STR_EQ(str1, str2);
}
```

### Test Files (29 total)

Tests are organized by feature in `/tests/`:

- `test_filesystem.c` - Directory operations
- `test_operations.c` - File copy/move/delete
- `test_browser.c` - Browser rendering
- `test_tabs.c` - Tab management
- `test_search.c` - Search functionality
- `test_claude_client.c` - Claude API
- `test_tool_executor.c` - Tool execution
- `test_ai.c` - Local AI features
- `test_git.c` - Git integration
- `test_network.c` - SFTP connectivity
- ... and more

### Adding Tests

1. Create `tests/test_yourfeature.c`
2. Add test file to `CMakeLists.txt` TEST_SOURCES
3. Declare test function in `tests/test_main.c`
4. Call test function from `run_all_tests()`

### Test Guidelines

- Test both success and failure cases
- Test edge cases (empty input, NULL, max values)
- Keep tests focused and independent
- Clean up any created files/resources
- Tests compile with `TESTING` macro defined (excludes UI code)

## Key Architecture Patterns

### App State Management

The central `App` struct holds all application state:

```c
// In app.h
typedef struct App {
    DirectoryState directory;
    SelectionState selection;
    TabState tabs;
    SidebarState sidebar;
    // ... etc
} App;

// Pass to functions that need global state
void browser_draw(App *app, Rectangle bounds);
```

### Claude Tool Use Flow

```c
// 1. Create and register tools
ToolRegistry *registry = tool_registry_create();
tool_registry_register_file_tools(registry);

// 2. Attach tools to request
claude_request_set_tools(request, registry);

// 3. Send message, may return CLAUDE_STOP_TOOL_USE
ClaudeStopReason reason = claude_send_message(client, request, response);

// 4. Execute tool if needed
if (reason == CLAUDE_STOP_TOOL_USE) {
    ToolResult result = tool_executor_execute(executor, tool_use);
    claude_request_add_tool_result(request, &result);
    // Continue conversation...
}
```

### AI Model Integration

The project uses two separate ggml-based libraries:

- **bert.cpp** (GGUF v1) - Text embeddings, built as static library
- **clip.cpp** (GGUF v2) - Image embeddings, built as SHARED library

They're isolated to prevent ggml version conflicts:

```cmake
# In CMakeLists.txt
add_library(clip SHARED ...)  # Separate shared library
target_link_libraries(finder-plus ... clip)
```

## Pull Request Process

1. **Create a feature branch** from `main`
2. **Make your changes** following the code style
3. **Write/update tests** for your changes
4. **Run all tests** and ensure they pass
5. **Update documentation** if needed
6. **Submit a pull request** with:
   - Clear title describing the change
   - Description of what and why
   - Link to any related issues

### PR Title Format

```
feat: Add semantic search toggle
fix: Correct file size display for large files
docs: Update keyboard shortcuts reference
refactor: Simplify directory reading logic
test: Add tests for clipboard operations
perf: Optimize directory loading for large folders
```

### PR Description Template

```markdown
## Summary
Brief description of changes

## Changes
- Change 1
- Change 2

## Testing
How the changes were tested

## Related Issues
Fixes #123
```

## Reporting Issues

### Bug Reports

Include:
- macOS version
- Steps to reproduce
- Expected vs actual behavior
- Error messages or logs
- Screenshots if applicable

### Feature Requests

Include:
- Use case description
- Proposed solution
- Alternatives considered

## Code of Conduct

- Be respectful and inclusive
- Provide constructive feedback
- Focus on the code, not the person
- Help others learn and grow

## Questions?

- Open an issue for questions
- Check existing issues for answers
- Read the documentation in `docs/`

Thank you for contributing!
