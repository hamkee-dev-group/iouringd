# iouringd

`iouringd` is a small Unix-socket daemon that exposes bounded async I/O over
Linux `io_uring`. It is usable as a standalone runtime kit and as a constrained
service inside a larger host-local job platform.

The daemon provides:

- feature probing for `io_uring_setup`, probed op support, queue sizing,
  registered files, and registered buffers
- a stable handshake plus submission/completion wire protocol
- bounded client admission and per-client credits
- registered file and registered buffer support where the kernel allows them
- trace and metrics output over the control socket, plus optional structured
  stderr logs with `--trace-stderr`
- clear startup failures when `io_uring` is unavailable or blocked by policy

## Build

```sh
cmake -S . -B build
cmake --build build -j4
python3 ./tests/run_tests.py
```

The test runner expects binaries under `build/tests/` and the daemon at
`build/bin/iouringd`.

## Run

```sh
./build/bin/iouringd /tmp/iouringd.sock
```

Supported daemon arguments:

```sh
iouringd [--ring-entries N] [--max-clients N] \
  [--registered-fds N] [--registered-buffers N] \
  [--per-client-credits N] [--io-bytes-max N] \
  [--job-id N] [--trace-stderr] SOCKET_PATH
```

`--job-id` annotates stderr trace and metrics records for correlation with
`cgroupd` or another orchestrator. `--trace-stderr` emits structured trace and
metrics lines to stderr in addition to the socket protocol trace stream.

## Documents

- [Wire Protocol](docs/protocol.md)
- [Platform Integration](docs/integration.md)

## Examples

- [Minimal client](examples/standalone_nop.c)
- [Per-job launch recipe](examples/per_job_service.sh)
- [Sealed memfd to overlay workspace copy](examples/memfd_overlay_copy.c)

## Failure Modes

If `io_uring` startup fails, the daemon exits with code `1` and emits a clear
stderr message:

- unsupported kernel: `io_uring setup is unavailable on this kernel (ENOSYS)`
- blocked by policy: `io_uring setup blocked by kernel policy or sandbox`
- probe yielded no supported operations: `io_uring probe reported no supported operations`

That behavior is covered by the integration tests.
