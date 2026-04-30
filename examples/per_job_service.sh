#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 SOCKET_PATH" >&2
  exit 2
fi

SOCKET_PATH=$1
JOB_ID=${JOB_ID:-1000}
WORKSPACE=${WORKSPACE:-/workspace}

exec cgroupd run --id "$JOB_ID" -- \
  landlockd run --ro /usr --rw "$WORKSPACE" -- \
  ./build/bin/iouringd \
    --job-id "$JOB_ID" \
    --trace-stderr \
    --ring-entries 64 \
    --max-clients 4 \
    --registered-fds 8 \
    --registered-buffers 1 \
    --per-client-credits 16 \
    --io-bytes-max 4096 \
    "$SOCKET_PATH"
