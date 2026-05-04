#!/usr/bin/env bash
# tools/git-hooks/install.sh
#
# Install the SFS pre-commit hook into .git/hooks/. Idempotent.
# Run from anywhere inside the repo.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GIT_HOOKS_DIR="${ROOT}/.git/hooks"

if [ ! -d "$GIT_HOOKS_DIR" ]; then
    echo "install.sh: not a git checkout (no .git/hooks/)." >&2
    exit 1
fi

src="${ROOT}/tools/git-hooks/pre-commit"
dst="${GIT_HOOKS_DIR}/pre-commit"

if [ -f "$dst" ] && ! cmp -s "$src" "$dst"; then
    backup="${dst}.bak.$(date +%s)"
    echo "install.sh: existing pre-commit differs; backing up to ${backup}"
    mv "$dst" "$backup"
fi

cp "$src" "$dst"
chmod +x "$dst"

echo "Installed pre-commit hook -> $dst"
echo "Run 'git commit' to exercise it; remove with 'rm $dst'."
