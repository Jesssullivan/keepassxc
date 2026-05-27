---
description: Build KeePassXC with all features enabled
allowed-tools: "Bash(cmake*), Bash(make*), Bash(ninja*)"
---

# Build KeePassXC

Build the project with all features enabled for development.

## Steps

1. Ensure build directory exists
2. Run CMake configuration if needed
3. Build with parallel jobs

## Implementation

```bash
cd /Users/jsullivan2/git/keepassxc

# Create build directory if missing
mkdir -p build && cd build

# Configure if CMakeCache.txt doesn't exist
if [ ! -f CMakeCache.txt ]; then
    cmake -DWITH_XC_ALL=ON \
          -DWITH_TESTS=ON \
          -DWITH_DEV_BUILD=ON \
          -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          -DCMAKE_PREFIX_PATH=$(brew --prefix qt@5)/lib/cmake \
          ..
fi

# Build
make -j$(sysctl -n hw.ncpu)
```

## Success Criteria
- Build completes without errors
- `src/KeePassXC.app` (macOS) or `src/keepassxc` binary exists
