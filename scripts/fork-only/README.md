# Fork-only development tooling

This branch is for `Jesssullivan/keepassxc` only. Do not include these files in
upstream KeePassXC PR branches.

Run once per checkout:

```sh
scripts/fork-only/install-local-guards.sh
direnv allow
```

Before opening an upstream PR, verify the branch is clean:

```sh
scripts/fork-only/check-upstream-clean.sh
```

Expected upstream PR workflow:

```sh
git fetch upstream
git switch -c my-upstream-branch upstream/develop
```

Fork-only branches that intentionally carry this tooling should use a `fork/`
branch name.
