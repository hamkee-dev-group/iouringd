#!/usr/bin/env sh

set -eu

exec cppcheck \
    --enable=warning,style,performance,portability \
    --check-level=exhaustive \
    --std=c11 \
    --error-exitcode=1 \
    --suppressions-list=scripts/cppcheck.suppress \
    -I include \
    src tests
