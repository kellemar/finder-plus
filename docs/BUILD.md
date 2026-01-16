# Build Instructions

This guide covers building Finder Plus from source on macOS.

## Prerequisites

### Required

- **macOS 12.0 or later** (Monterey or newer)
- **Xcode Command Line Tools**
  ```bash
  xcode-select --install
  ```
- **CMake 3.20+**
  ```bash
  brew install cmake
  ```
- **libssh2** (for SFTP support)
  ```bash
  brew install libssh2
  ```

### Included with macOS

These dependencies are already available on macOS:
- libcurl (HTTP client)
- SQLite3 (vector database)
- CommonCrypto (file hashing)
- CoreServices (FSEvents for file watching)

## Quick Build

```bash
# Clone with submodules
git clone --recursive https://github.com/yourusername/finder-plus.git
cd finder-plus

# Build raylib (one-time)
cd raylib/src && make PLATFORM=PLATFORM_DESKTOP && cd ../..

# Build application
mkdir -p build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)

# Run
./finder-plus
```

## Detailed Build Steps

### 1. Clone Repository

```bash
git clone --recursive https://github.com/yourusername/finder-plus.git
cd finder-plus
```

If you forgot `--recursive`, initialize submodules:
```bash
git submodule update --init --recursive
```

### 2. Build Raylib

Raylib must be built once before building Finder Plus:

```bash
cd raylib/src
make PLATFORM=PLATFORM_DESKTOP
cd ../..
```

This creates `raylib/src/libraylib.a`.

### 3. Configure with CMake

```bash
mkdir -p build
cd build
cmake ..
```

#### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Debug | Build type (Debug/Release) |

Example with options:
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### 4. Build

```bash
# Build all targets
make -j$(sysctl -n hw.ncpu)

# Or build specific target
make finder-plus      # Main application
make test_runner      # Test suite
make perf_test        # Performance tests
```

### 5. Run

```bash
# From build directory
./finder-plus

# Or specify starting directory
./finder-plus ~/Documents
```

## Build Targets

| Target | Description |
|--------|-------------|
| `finder-plus` | Main application |
| `test_runner` | Unit and integration tests |
| `perf_test` | Performance benchmarks |

## Running Tests

```bash
cd build
make test_runner
./test_runner
```

Expected output:
```
=== Finder Plus Test Suite ===
  Running tests...
  ...
=== Results ===
Passed: 967/968
Failed: 0
All tests PASSED!
```

## Performance Tests

```bash
cd build
make perf_test
./perf_test
```

This benchmarks:
- Directory reading speed
- Sorting performance
- Memory usage

## Debug Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

Debug builds include:
- Debug symbols (`-g`)
- No optimization (`-O0`)
- Debug assertions (`-DDEBUG`)

## Release Build

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Release builds include:
- Optimization (`-O2`)
- No debug assertions (`-DNDEBUG`)

## Clean Build

To completely rebuild:

```bash
cd build
rm -rf *
cmake ..
make
```

## Project Structure

```
finder-plus/
├── src/
│   ├── main.c              # Entry point
│   ├── app.c/h             # Application state
│   ├── api/                # Claude API integration
│   ├── ai/                 # Local AI (embeddings, search)
│   ├── core/               # File operations, search
│   ├── tools/              # AI tool registry
│   ├── ui/                 # UI components
│   ├── platform/           # macOS-specific code
│   └── utils/              # Config, theme, keybindings
├── tests/                  # Test files
├── external/
│   └── cJSON/              # JSON parser
├── raylib/                 # Raylib submodule
├── build/                  # Build output (created)
├── CMakeLists.txt          # Build configuration
└── docs/                   # Documentation
```

## Dependencies

### External Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| Raylib | 5.5.0 | Graphics rendering |
| cJSON | 1.7.x | JSON parsing |
| libcurl | System | HTTP requests |
| SQLite3 | System | Vector database |
| libssh2 | 1.x | SFTP support |

### macOS Frameworks

- Cocoa - Window management
- IOKit - Device handling
- OpenGL - Graphics
- CoreServices - FSEvents

## Troubleshooting

### "raylib.h not found"

Ensure raylib is built:
```bash
cd raylib/src && make PLATFORM=PLATFORM_DESKTOP
```

### "libssh2 not found"

Install via Homebrew:
```bash
brew install libssh2
```

### "Undefined symbols" linker errors

Usually means a dependency is missing. Check:
1. Raylib is built (`raylib/src/libraylib.a` exists)
2. All brew packages are installed
3. Run `cmake ..` again after installing dependencies

### Tests fail to build

Some tests require raylib. Ensure raylib is built first.

### Performance issues

Try a release build:
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## IDE Setup

### VS Code

Recommended extensions:
- C/C++ (Microsoft)
- CMake Tools
- clangd

`.vscode/c_cpp_properties.json`:
```json
{
  "configurations": [
    {
      "name": "Mac",
      "includePath": [
        "${workspaceFolder}/src",
        "${workspaceFolder}/external",
        "${workspaceFolder}/raylib/src"
      ],
      "compilerPath": "/usr/bin/clang",
      "cStandard": "c99",
      "intelliSenseMode": "macos-clang-arm64"
    }
  ]
}
```

### Xcode

Generate Xcode project:
```bash
mkdir -p build-xcode && cd build-xcode
cmake -G Xcode ..
open finder-plus.xcodeproj
```

## Creating a Release

1. Build in release mode
2. Run tests to verify
3. Create application bundle (see Distribution docs)

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./test_runner  # Verify all tests pass
```
