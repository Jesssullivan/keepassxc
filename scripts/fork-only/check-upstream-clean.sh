#!/usr/bin/env bash
set -euo pipefail

base_ref="${1:-upstream/develop}"
target_ref="${2:-HEAD}"

fork_only_paths=(
  ".envrc"
  "flake.nix"
  "flake.lock"
  ".githooks/"
  "scripts/fork-only/"
)

git rev-parse --verify "$base_ref" >/dev/null
git rev-parse --verify "$target_ref" >/dev/null

changed="$(git diff --name-only "$base_ref...$target_ref" -- "${fork_only_paths[@]}" || true)"

if [[ -n "$changed" ]]; then
  cat >&2 <<EOF
Branch is not clean for an upstream KeePassXC PR.
Fork-only tooling files are present relative to $base_ref:
$changed

Create upstream PR branches from $base_ref, not from the fork-only develop branch.
EOF
  exit 1
fi

echo "OK: no fork-only tooling files relative to $base_ref."
