#!/usr/bin/env sh

set -eu

sources="
src/iouringd.c
src/lib/client.c
src/lib/submit.c
src/daemon/main.c
src/daemon/handshake.c
src/daemon/submit.c
"
common_flags="-Iinclude -Isrc/daemon -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes -Wvla -Wformat=2 -D_GNU_SOURCE"

if command -v clang >/dev/null 2>&1; then
    for src in $sources; do
        clang --analyze $common_flags "$src"
    done
    exit 0
fi

if command -v gcc >/dev/null 2>&1; then
    for src in $sources; do
        gcc -fanalyzer -fsyntax-only $common_flags "$src"
    done
    exit 0
fi

echo "static analysis requires clang --analyze or gcc -fanalyzer" >&2
exit 1
