#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

git config core.hooksPath .githooks
git remote set-url --push upstream DISABLED

exclude_file=".git/info/exclude"
patterns=(
  ".direnv/"
  "result"
  "result-*"
  "build-nix/"
  "cmake-build-nix/"
)

for pattern in "${patterns[@]}"; do
  if ! grep -Fxq "$pattern" "$exclude_file"; then
    printf '%s\n' "$pattern" >> "$exclude_file"
  fi
done

if command -v gh >/dev/null 2>&1; then
  gh repo set-default Jesssullivan/keepassxc >/dev/null
fi

cat <<'EOF'
Installed local fork guards:
- core.hooksPath = .githooks
- upstream push URL disabled
- local exclude patterns for direnv, Nix results, and throwaway build dirs
- gh default repo set to Jesssullivan/keepassxc when gh is available
EOF
