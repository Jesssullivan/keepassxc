---
description: Verify debugger and development environment setup
allowed-tools: "Bash(cmake*), Bash(lldb*), Bash(brew*), Bash(which*)"
---

# Debug Environment Setup

Verify CLion/debugger setup for KeePassXC development.

## Check Required Tools
```bash
# Check CMake
cmake --version

# Check compiler
clang++ --version

# Check LLDB debugger
lldb --version

# Check Qt5
brew --prefix qt@5
```

## Verify Dependencies
```bash
brew list | grep -E "botan|qt@5|argon2|minizip|qrencode|zlib|readline"
```

## Check CLion Configuration

1. **CMake Profile**: Ensure Debug profile uses:
   - `-DWITH_XC_ALL=ON`
   - `-DWITH_TESTS=ON`
   - `-DCMAKE_BUILD_TYPE=Debug`

2. **Debugger**: LLDB should auto-configure on macOS

3. **Code Style**: Enable "ClangFormat" in Editor > Code Style

4. **Compile Commands**: Verify `build/compile_commands.json` exists

## Enable AddressSanitizer (Optional)
```bash
cmake -DWITH_ASAN=ON ..

# Before running with ASAN on macOS
export ASAN_OPTIONS=detect_leaks=0
```

## Test Debugger
```bash
cd /Users/jsullivan2/git/keepassxc/build
lldb src/KeePassXC.app/Contents/MacOS/KeePassXC
# (lldb) breakpoint set -n main
# (lldb) run
```
