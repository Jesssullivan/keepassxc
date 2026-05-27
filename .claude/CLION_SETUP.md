# CLion Development Setup for KeePassXC

**Platform:** Rocky Linux 10.1 (RHEL-based)
**CLion Version:** 2025.3.1.1 (via JetBrains Toolbox)
**CMake:** 4.1.2 (CLion bundled) or 3.30.5 (system)

---

## 1. Install Required Dependencies

The system needs development packages installed. Run with sudo:

```bash
# Core build tools
sudo dnf install -y cmake gcc-c++ make

# Qt5 development packages
sudo dnf install -y \
    qt5-devel \
    qt5-qtbase-devel \
    qt5-qtsvg-devel \
    qt5-qttools-devel \
    qt5-qtx11extras-devel \
    qt5-qtwayland-devel \
    qt5-qtdeclarative-devel

# Crypto and compression libraries
sudo dnf install -y \
    botan2-devel \
    libargon2-devel \
    minizip-ng-compat-devel \
    zlib-ng-compat-devel

# QR code generation
sudo dnf install -y qrencode-devel

# CLI readline support
sudo dnf install -y readline-devel

# X11 dependencies (for Auto-Type)
sudo dnf install -y \
    libXi-devel \
    libXtst-devel \
    libX11-devel

# YubiKey support (optional)
sudo dnf install -y libusb1-devel

# Code formatting
sudo dnf install -y clang-tools-extra
```

**Single command (copy-paste ready):**
```bash
sudo dnf install -y cmake gcc-c++ make \
    qt5-devel qt5-qtbase-devel qt5-qtsvg-devel qt5-qttools-devel \
    qt5-qtx11extras-devel qt5-qtwayland-devel qt5-qtdeclarative-devel \
    botan2-devel libargon2-devel minizip-ng-compat-devel zlib-ng-compat-devel \
    qrencode-devel readline-devel \
    libXi-devel libXtst-devel libX11-devel \
    libusb1-devel clang-tools-extra
```

---

## 2. Clean Stale Build Directory

The existing `build/` directory contains macOS configuration. Remove it:

```bash
rm -rf /home/jsullivan2/git/keepassxc/build
```

---

## 3. CLion CMake Configuration

### CMake Options for Development

In CLion: **Settings > Build, Execution, Deployment > CMake**

**Profile: Debug**
```
-DWITH_XC_ALL=ON
-DWITH_TESTS=ON
-DWITH_GUI_TESTS=ON
-DWITH_DEV_BUILD=ON
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

**Profile: Debug with ASAN** (for memory debugging)
```
-DWITH_XC_ALL=ON
-DWITH_TESTS=ON
-DWITH_GUI_TESTS=ON
-DWITH_DEV_BUILD=ON
-DWITH_ASAN=ON
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

**Profile: RelWithDebInfo** (for performance testing)
```
-DWITH_XC_ALL=ON
-DWITH_TESTS=ON
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### Feature Flags Explained

| Flag | Purpose |
|------|---------|
| `WITH_XC_ALL=ON` | Enable all plugins (browser, SSH agent, YubiKey, etc.) |
| `WITH_TESTS=ON` | Build unit tests |
| `WITH_GUI_TESTS=ON` | Build GUI tests |
| `WITH_DEV_BUILD=ON` | Show deprecation warnings |
| `WITH_ASAN=ON` | AddressSanitizer for memory bugs |
| `CMAKE_EXPORT_COMPILE_COMMANDS=ON` | Generate compile_commands.json for IDE |

---

## 4. CLion Run/Debug Configurations

After CMake reload, CLion auto-creates configurations. Key targets:

| Target | Purpose |
|--------|---------|
| `KeePassXC` | Main GUI application |
| `keepassxc-cli` | Command-line tool |
| `keepassxc-proxy` | Browser native messaging proxy |

### Debug Configuration for KeePassXC

1. Click **Run > Edit Configurations**
2. Select **KeePassXC** target
3. Optionally add **Program arguments**: `--debug`
4. Set **Working directory**: `$ProjectFileDir$`

### Test Configuration

1. Click **Run > Edit Configurations**
2. Add **CMake Application**
3. Target: Select specific test (e.g., `testdatabase`)
4. Or use CTest integration for running all tests

---

## 5. Code Style Configuration

CLion should auto-detect `.clang-format`. Verify in:
**Settings > Editor > Code Style > C/C++ > Enable ClangFormat**

The project uses:
- 4 spaces indentation
- 120 character line limit
- Allman brace style for classes/functions

---

## 6. Build and Test Commands

### From CLion
- **Build**: Ctrl+F9 or Build > Build Project
- **Run**: Shift+F10
- **Debug**: Shift+F9
- **Run Tests**: Right-click test in Project view > Run

### From Terminal (build directory)
```bash
# Build everything
cmake --build . --parallel

# Run all tests
ctest --output-on-failure

# Run specific test
ctest -R testdatabase -V

# Format code
cmake --build . --target format
```

---

## 7. Debugging Tips

### Breakpoints for Common Tasks
- **Startup**: `src/main.cpp:main()`
- **Database open**: `src/gui/DatabaseOpenWidget.cpp`
- **Entry operations**: `src/core/Entry.cpp`
- **Browser integration**: `src/browser/BrowserService.cpp`

### AddressSanitizer
When using ASAN (`-DWITH_ASAN=ON`), set environment variable:
```bash
export ASAN_OPTIONS=detect_stack_use_after_return=1
```

---

## 8. Troubleshooting

### CMake can't find Qt5
```bash
# Check Qt5 installation
rpm -qa | grep qt5-qtbase-devel
# Ensure pkg-config can find it
pkg-config --cflags Qt5Core
```

### Botan version mismatch
The vcpkg.json specifies Botan 3.1.1+, but Rocky Linux has Botan 2.19.5.
This is **OK** - the CMake supports both versions. Botan 2.x works fine.

### Missing compile_commands.json
Re-run CMake with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`

### Clang-format not formatting
Ensure `clang-tools-extra` is installed:
```bash
which clang-format
```

---

## 9. Quick Reference

### CMake Profiles Summary

| Profile | Build Type | ASAN | Tests | Use Case |
|---------|------------|------|-------|----------|
| Debug | Debug | No | Yes | Regular development |
| Debug-ASAN | Debug | Yes | Yes | Memory debugging |
| RelWithDebInfo | RelWithDebInfo | No | Yes | Performance testing |
| Release | Release | No | No | Final builds |

### Key Directories

| Directory | Purpose |
|-----------|---------|
| `src/core/` | Database, Entry, Group models |
| `src/gui/` | Qt5 widgets and dialogs |
| `src/cli/` | Command-line tool |
| `src/browser/` | Browser extension backend |
| `tests/` | Unit tests |
| `cmake/` | CMake modules |
