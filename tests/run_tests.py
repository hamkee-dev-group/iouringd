#!/usr/bin/env python3

import subprocess
import sys
import time


def test_bin(name: str) -> str:
    return f"./build/tests/{name}"


def daemon_bin() -> str:
    return "./build/bin/iouringd"


COMMANDS = [
    [test_bin("test_version")],
    [test_bin("handshake_contract")],
    [test_bin("completion_res_contract")],
    [test_bin("submit_result_contract")],
    [test_bin("timeout_request_contract")],
    [test_bin("cancel_request_contract")],
    [test_bin("test_protocol_task_kinds")],
    [test_bin("client_handshake_validation")],
    [test_bin("client_completion_validation")],
    [test_bin("client_submit_validation")],
    [test_bin("handshake_roundtrip"), daemon_bin()],
    [test_bin("nop_roundtrip"), daemon_bin()],
    [test_bin("nop_two_requests_roundtrip"), daemon_bin()],
    [test_bin("reconnect_task_id_roundtrip"), daemon_bin()],
    [test_bin("graceful_shutdown"), daemon_bin()],
    [test_bin("register_sock_write_roundtrip"), daemon_bin()],
    [test_bin("register_sock_read_roundtrip"), daemon_bin()],
    [test_bin("register_file_read_roundtrip"), daemon_bin()],
    [test_bin("register_file_write_roundtrip"), daemon_bin()],
    [test_bin("openat_file_read_roundtrip"), daemon_bin()],
    [test_bin("openat_close_roundtrip"), daemon_bin()],
    [test_bin("register_connect_roundtrip"), daemon_bin()],
    [test_bin("register_accept_roundtrip"), daemon_bin()],
    [test_bin("submit_accept_einval_rejected"), daemon_bin()],
    [test_bin("register_poll_roundtrip"), daemon_bin()],
    [test_bin("register_buffer_fixed_io_roundtrip"), daemon_bin()],
    [test_bin("config_stats_roundtrip"), daemon_bin()],
    [test_bin("trace_roundtrip"), daemon_bin()],
    [test_bin("release_busy_resource_rejected"), daemon_bin()],
    [test_bin("two_clients_register_sock_write_roundtrip"), daemon_bin()],
    [test_bin("two_clients_timeout_roundtrip"), daemon_bin()],
    [test_bin("per_client_credits_roundtrip"), daemon_bin()],
    [test_bin("priority_dispatch_roundtrip"), daemon_bin()],
    [test_bin("bounded_admission_roundtrip"), daemon_bin()],
    [test_bin("timeout_roundtrip"), daemon_bin()],
    [test_bin("timeout_two_requests_roundtrip"), daemon_bin()],
    [test_bin("nop_then_timeout_roundtrip"), daemon_bin()],
    [test_bin("timeout_then_nop_roundtrip"), daemon_bin()],
    [test_bin("malformed_timeout_request_rejected"), daemon_bin()],
    [test_bin("malformed_timeout_then_nop_roundtrip"), daemon_bin()],
    [test_bin("cancel_request_rejected"), daemon_bin()],
    [test_bin("cancel_submission_rejected"), daemon_bin()],
    [test_bin("cancel_timeout_roundtrip"), daemon_bin()],
    [test_bin("cancel_partial_target_rejected"), daemon_bin()],
    [test_bin("wait_completion_rejects_invalid")],
    [test_bin("completion_invalid_task_id")],
    [test_bin("invalid_completion_response")],
    [test_bin("handshake_client_rejects_invalid_response")],
    [test_bin("rejected_handshake_blocks_submit"), daemon_bin()],
    [test_bin("handshake_invalid_response")],
    [test_bin("submit_bad_magic_rejected"), daemon_bin()],
    [test_bin("submit_version_major_rejected"), daemon_bin()],
    [test_bin("submit_reserved_bits_rejected"), daemon_bin()],
    [test_bin("submit_reserved1_rejected"), daemon_bin()],
    [test_bin("unsupported_submit_kind_rejected"), daemon_bin()],
    [test_bin("unsupported_kernel_behavior"), daemon_bin()],
    [test_bin("restricted_policy_behavior"), daemon_bin()],
    [test_bin("trace_stderr_roundtrip"), daemon_bin()],
]


def is_retryable(command: list[str]) -> bool:
    return len(command) >= 2 and command[1] == daemon_bin()


def main() -> int:
    for index, command in enumerate(COMMANDS, 1):
        attempts = 2 if is_retryable(command) else 1
        for attempt in range(attempts):
            result = subprocess.run(command, check=False)
            if result.returncode == 0:
                break
            if attempt + 1 == attempts:
                print(
                    f"test command failed [{index}/{len(COMMANDS)}]: {' '.join(command)}",
                    file=sys.stderr,
                )
                return result.returncode
            time.sleep(0.05)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
