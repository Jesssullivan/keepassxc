---
description: Format code using clang-format
allowed-tools: "Bash(make*), Bash(clang-format*)"
---

# Format Code

Apply project code style using clang-format.

## Format All Changed Files
```bash
cd /Users/jsullivan2/git/keepassxc/build
make format
```

## Format Specific File
```bash
clang-format -i /Users/jsullivan2/git/keepassxc/src/path/to/file.cpp
```

## Check Formatting (No Changes)
```bash
clang-format --dry-run -Werror src/path/to/file.cpp
```

## Style Summary (from .clang-format)
- Column limit: 120
- Indent: 4 spaces
- Pointer alignment: Left (`int* ptr`)
- Brace wrapping: After class, function, namespace
- Standard: C++17
