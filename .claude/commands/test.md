---
description: Run KeePassXC test suite
allowed-tools: "Bash(make*), Bash(ctest*)"
---

# Run Tests

Execute the KeePassXC unit test suite.

## Implementation

```bash
cd /Users/jsullivan2/git/keepassxc/build

# Run tests with verbose output on failure
make test ARGS+="--output-on-failure"
```

## GUI Tests (Linux headless)
```bash
xvfb-run make test
```

## Run Specific Test
```bash
ctest -R TestEntry --output-on-failure
```

## Common Test Names
- `TestEntry` - Entry/password operations
- `TestGroup` - Group hierarchy
- `TestDatabase` - Database operations
- `TestKeePass2Format` - KDBX file format
- `TestCrypto` - Cryptographic operations
- `TestCli` - CLI commands
