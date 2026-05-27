# KeePassXC - Claude Code Configuration

**Last Updated**: 2025-01-19
**Project**: KeePassXC - Cross-Platform Password Manager
**Version**: 2.8.0 (develop branch)

---

## Project Overview

KeePassXC is a community fork of KeePassX, a native cross-platform password manager. Built with C++17 and Qt5, it provides secure password storage with features like Auto-Type, browser integration, YubiKey support, SSH agent, and TOTP.

**Architecture**: Desktop GUI application (Qt5) + CLI (`keepassxc-cli`)
**Crypto Library**: Botan (3.1.1+)
**Database Format**: KDBX 4.x (KeePass compatible)

---

## Build System

### Quick Start (macOS with Homebrew)
```bash
# Install dependencies
brew install cmake qt@5 botan argon2 minizip libqrencode zlib readline

# Configure and build
mkdir build && cd build
cmake -DWITH_XC_ALL=ON \
      -DCMAKE_PREFIX_PATH=$(brew --prefix qt@5)/lib/cmake \
      -DCMAKE_BUILD_TYPE=Debug \
      ..
make -j$(sysctl -n hw.ncpu)
```

### CLion IDE Configuration

**Recommended CMake Options** (Settings > Build > CMake):
```
-DWITH_XC_ALL=ON
-DWITH_TESTS=ON
-DWITH_GUI_TESTS=ON
-DWITH_DEV_BUILD=ON
-DCMAKE_VERBOSE_MAKEFILE=ON
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
-DCMAKE_PREFIX_PATH=$(brew --prefix qt@5)/lib/cmake
```

**Build Types**:
| Type | Use Case |
|------|----------|
| `Debug` | Development with full symbols |
| `RelWithDebInfo` | Performance testing with debug info |
| `Release` | Final builds, no debug |

**Debugger Setup**:
- LLDB is default on macOS (CLion auto-configures)
- For AddressSanitizer: Add `-DWITH_ASAN=ON` to CMake options
- Set `ASAN_OPTIONS=detect_leaks=0` on macOS (LSAN not supported)

### vcpkg Alternative (Windows/Cross-platform)
```bash
cmake -DWITH_XC_ALL=ON \
      -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
      ..
```

---

## Project Structure

```
keepassxc/
├── src/
│   ├── core/           # Database, Entry, Group, Metadata classes
│   ├── crypto/         # Botan wrappers, KDF, cipher, hash
│   ├── gui/            # Qt5 widgets and dialogs
│   ├── cli/            # keepassxc-cli commands
│   ├── browser/        # Browser extension protocol
│   ├── autotype/       # Auto-Type platform implementations
│   ├── keys/           # Key files, challenge-response
│   ├── format/         # KDBX reader/writer, CSV import
│   ├── sshagent/       # SSH key agent
│   ├── keeshare/       # Database sync/share
│   ├── fdosecrets/     # D-Bus secret service
│   └── thirdparty/     # Embedded libraries
├── tests/              # Qt Test framework tests
├── cmake/              # CMake modules (FindBotan, etc.)
├── share/              # Resources, icons, translations
└── docs/               # AsciiDoc user documentation
```

---

## Coding Standards

### Style Guide
- **Naming**: `lowerCamelCase`
- **Indentation**: 4 spaces (C++), 2 spaces (.ui files)
- **Line length**: 120 characters max
- **Member variables**: Prefix with `m_` (e.g., `m_database`)
- **Includes**: Class → App → Global order

### Format Code
```bash
# From build directory
make format
```

### Include Order
```cpp
// Class includes
#include "MyWidget.h"
#include "ui_MyWidget.h"

// Application includes
#include "core/Config.h"

// Global includes
#include <QWidget>
```

---

## Testing

### Run Tests
```bash
cd build
make test ARGS+="--output-on-failure"

# GUI tests (Linux headless)
xvfb-run make test
```

### Test Files Location
- Unit tests: `tests/Test*.cpp`
- Test data: `tests/data/`
- Mock objects: `tests/mock/`

---

## Git Workflow

### Branch Strategy (git-flow-lite)
- `develop` - Main development branch
- `feature/[name]` - New features
- `fix/[name]` - Bug fixes
- `latest` tag - Most recent stable release

### Commit Messages
- Present tense, imperative mood
- First line ≤72 characters
- Reference issues: "Fixes #1234"

---

## Key Files for Development

| File | Purpose |
|------|---------|
| `src/core/Database.cpp` | Database operations core |
| `src/core/Entry.cpp` | Password entry model |
| `src/gui/MainWindow.cpp` | Main application window |
| `src/cli/Command.cpp` | CLI command base class |
| `src/browser/BrowserService.cpp` | Browser extension backend |
| `CMakeLists.txt` | Root build configuration |
| `.clang-format` | Code formatting rules |

---

## Dependencies (vcpkg.json)

| Package | Min Version | Purpose |
|---------|-------------|---------|
| Botan | 3.1.1 | Cryptography |
| Qt5 | 5.15.17 | GUI framework |
| Argon2 | 20190702 | Key derivation |
| Minizip | 1.3 | KDBX file handling |
| libqrencode | 4.1.1 | QR code generation |
| zlib | 1.3 | Compression |

---

## Build Targets

| Target | Description |
|--------|-------------|
| `KeePassXC` | Main GUI application |
| `keepassxc-cli` | Command-line tool |
| `keepassxc-proxy` | Browser native messaging proxy |
| `test` | Run unit tests |
| `format` | Format code with clang-format |

---

## Security Considerations

### Sensitive Code Areas
- `src/crypto/` - All cryptographic operations
- `src/keys/` - Key file handling, challenge-response
- `src/core/Database.cpp` - Database encryption/decryption
- `src/browser/` - External communication

### Memory Safety
- Enable ASAN during development: `-DWITH_ASAN=ON`
- CodeQL runs on all PRs (`.github/workflows/codeql.yml`)

---

## AI Contribution Policy

Per CONTRIBUTING.md: If using AI for code generation:
- Document AI usage in pull request
- Specify service/model used
- All code undergoes standard review process

---

## Quick Reference

### Development Commands
```bash
# Build
cmake --build build --parallel

# Test
cmake --build build --target test

# Format
cmake --build build --target format

# Clean
rm -rf build && mkdir build
```

### Debug Tips
- Set breakpoints in `src/main.cpp` for startup
- Entry operations: `src/core/Entry.cpp`
- Database unlock: `src/gui/DatabaseOpenWidget.cpp`
- Browser integration: `src/browser/BrowserService.cpp`
