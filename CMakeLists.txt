cmake_minimum_required(VERSION 3.20)

project(iouringd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(
  -Wall
  -Wextra
  -Werror
  -Wpedantic
)

include(CTest)

add_library(iouringd STATIC
  src/lib/client.c
  src/lib/submit.c
)
target_include_directories(iouringd PUBLIC include)
set_target_properties(iouringd PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

add_executable(iouringd_daemon
  src/daemon/main.c
  src/daemon/handshake.c
  src/daemon/submit.c
)
target_include_directories(iouringd_daemon PRIVATE include src/daemon)
set_target_properties(iouringd_daemon PROPERTIES
  OUTPUT_NAME iouringd
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(handshake_roundtrip
  tests/integration/handshake_roundtrip.c
)
target_link_libraries(handshake_roundtrip PRIVATE iouringd)
target_include_directories(handshake_roundtrip PRIVATE include)
set_target_properties(handshake_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME handshake_roundtrip
  COMMAND $<TARGET_FILE:handshake_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(nop_roundtrip
  tests/integration/nop_roundtrip.c
)
target_link_libraries(nop_roundtrip PRIVATE iouringd)
target_include_directories(nop_roundtrip PRIVATE include)
set_target_properties(nop_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME nop_roundtrip
  COMMAND $<TARGET_FILE:nop_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(reconnect_task_id_roundtrip
  tests/integration/reconnect_task_id_roundtrip.c
)
target_link_libraries(reconnect_task_id_roundtrip PRIVATE iouringd)
target_include_directories(reconnect_task_id_roundtrip PRIVATE include)
set_target_properties(reconnect_task_id_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME reconnect_task_id_roundtrip
  COMMAND $<TARGET_FILE:reconnect_task_id_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(graceful_shutdown
  tests/integration/graceful_shutdown.c
)
target_link_libraries(graceful_shutdown PRIVATE iouringd)
target_include_directories(graceful_shutdown PRIVATE include)
set_target_properties(graceful_shutdown PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME graceful_shutdown
  COMMAND $<TARGET_FILE:graceful_shutdown> $<TARGET_FILE:iouringd_daemon>
)

add_executable(register_sock_write_roundtrip
  tests/integration/register_sock_write_roundtrip.c
)
target_link_libraries(register_sock_write_roundtrip PRIVATE iouringd)
target_include_directories(register_sock_write_roundtrip PRIVATE include)
set_target_properties(register_sock_write_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME register_sock_write_roundtrip
  COMMAND $<TARGET_FILE:register_sock_write_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(register_sock_read_roundtrip
  tests/integration/register_sock_read_roundtrip.c
)
target_link_libraries(register_sock_read_roundtrip PRIVATE iouringd)
target_include_directories(register_sock_read_roundtrip PRIVATE include)
set_target_properties(register_sock_read_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME register_sock_read_roundtrip
  COMMAND $<TARGET_FILE:register_sock_read_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(register_file_read_roundtrip
  tests/integration/register_file_read_roundtrip.c
)
target_link_libraries(register_file_read_roundtrip PRIVATE iouringd)
target_include_directories(register_file_read_roundtrip PRIVATE include)
set_target_properties(register_file_read_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME register_file_read_roundtrip
  COMMAND $<TARGET_FILE:register_file_read_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(register_file_write_roundtrip
  tests/integration/register_file_write_roundtrip.c
)
target_link_libraries(register_file_write_roundtrip PRIVATE iouringd)
target_include_directories(register_file_write_roundtrip PRIVATE include)
set_target_properties(register_file_write_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME register_file_write_roundtrip
  COMMAND $<TARGET_FILE:register_file_write_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(openat_file_read_roundtrip
  tests/integration/openat_file_read_roundtrip.c
)
target_link_libraries(openat_file_read_roundtrip PRIVATE iouringd)
target_include_directories(openat_file_read_roundtrip PRIVATE include)
set_target_properties(openat_file_read_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME openat_file_read_roundtrip
  COMMAND $<TARGET_FILE:openat_file_read_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(openat_close_roundtrip
  tests/integration/openat_close_roundtrip.c
)
target_link_libraries(openat_close_roundtrip PRIVATE iouringd)
target_include_directories(openat_close_roundtrip PRIVATE include)
set_target_properties(openat_close_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME openat_close_roundtrip
  COMMAND $<TARGET_FILE:openat_close_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(register_connect_roundtrip
  tests/integration/register_connect_roundtrip.c
)
target_link_libraries(register_connect_roundtrip PRIVATE iouringd)
target_include_directories(register_connect_roundtrip PRIVATE include)
set_target_properties(register_connect_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME register_connect_roundtrip
  COMMAND $<TARGET_FILE:register_connect_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(register_accept_roundtrip
  tests/integration/register_accept_roundtrip.c
)
target_link_libraries(register_accept_roundtrip PRIVATE iouringd)
target_include_directories(register_accept_roundtrip PRIVATE include)
set_target_properties(register_accept_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME register_accept_roundtrip
  COMMAND $<TARGET_FILE:register_accept_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(submit_accept_einval_rejected
  tests/integration/submit_accept_einval_rejected.c
)
target_link_libraries(submit_accept_einval_rejected PRIVATE iouringd)
target_include_directories(submit_accept_einval_rejected PRIVATE include)
set_target_properties(submit_accept_einval_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME submit_accept_einval_rejected
  COMMAND $<TARGET_FILE:submit_accept_einval_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(register_poll_roundtrip
  tests/integration/register_poll_roundtrip.c
)
target_link_libraries(register_poll_roundtrip PRIVATE iouringd)
target_include_directories(register_poll_roundtrip PRIVATE include)
set_target_properties(register_poll_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME register_poll_roundtrip
  COMMAND $<TARGET_FILE:register_poll_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(register_buffer_fixed_io_roundtrip
  tests/integration/register_buffer_fixed_io_roundtrip.c
)
target_link_libraries(register_buffer_fixed_io_roundtrip PRIVATE iouringd)
target_include_directories(register_buffer_fixed_io_roundtrip PRIVATE include)
set_target_properties(register_buffer_fixed_io_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME register_buffer_fixed_io_roundtrip
  COMMAND $<TARGET_FILE:register_buffer_fixed_io_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(config_stats_roundtrip
  tests/integration/config_stats_roundtrip.c
)
target_link_libraries(config_stats_roundtrip PRIVATE iouringd)
target_include_directories(config_stats_roundtrip PRIVATE include)
set_target_properties(config_stats_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME config_stats_roundtrip
  COMMAND $<TARGET_FILE:config_stats_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(trace_roundtrip
  tests/integration/trace_roundtrip.c
)
target_link_libraries(trace_roundtrip PRIVATE iouringd)
target_include_directories(trace_roundtrip PRIVATE include)
set_target_properties(trace_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME trace_roundtrip
  COMMAND $<TARGET_FILE:trace_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(priority_dispatch_roundtrip
  tests/integration/priority_dispatch_roundtrip.c
)
target_link_libraries(priority_dispatch_roundtrip PRIVATE iouringd)
target_include_directories(priority_dispatch_roundtrip PRIVATE include)
set_target_properties(priority_dispatch_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME priority_dispatch_roundtrip
  COMMAND $<TARGET_FILE:priority_dispatch_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(release_busy_resource_rejected
  tests/integration/release_busy_resource_rejected.c
)
target_link_libraries(release_busy_resource_rejected PRIVATE iouringd)
target_include_directories(release_busy_resource_rejected PRIVATE include)
set_target_properties(release_busy_resource_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME release_busy_resource_rejected
  COMMAND $<TARGET_FILE:release_busy_resource_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(two_clients_register_sock_write_roundtrip
  tests/integration/two_clients_register_sock_write_roundtrip.c
)
target_link_libraries(two_clients_register_sock_write_roundtrip PRIVATE iouringd)
target_include_directories(two_clients_register_sock_write_roundtrip PRIVATE include)
set_target_properties(two_clients_register_sock_write_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME two_clients_register_sock_write_roundtrip
  COMMAND $<TARGET_FILE:two_clients_register_sock_write_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(two_clients_timeout_roundtrip
  tests/integration/two_clients_timeout_roundtrip.c
)
target_link_libraries(two_clients_timeout_roundtrip PRIVATE iouringd)
target_include_directories(two_clients_timeout_roundtrip PRIVATE include)
set_target_properties(two_clients_timeout_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME two_clients_timeout_roundtrip
  COMMAND $<TARGET_FILE:two_clients_timeout_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(per_client_credits_roundtrip
  tests/integration/per_client_credits_roundtrip.c
)
target_link_libraries(per_client_credits_roundtrip PRIVATE iouringd)
target_include_directories(per_client_credits_roundtrip PRIVATE include)
set_target_properties(per_client_credits_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME per_client_credits_roundtrip
  COMMAND $<TARGET_FILE:per_client_credits_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(bounded_admission_roundtrip
  tests/integration/bounded_admission_roundtrip.c
)
target_link_libraries(bounded_admission_roundtrip PRIVATE iouringd)
target_include_directories(bounded_admission_roundtrip PRIVATE include)
set_target_properties(bounded_admission_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME bounded_admission_roundtrip
  COMMAND $<TARGET_FILE:bounded_admission_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(timeout_roundtrip
  tests/integration/timeout_roundtrip.c
)
target_link_libraries(timeout_roundtrip PRIVATE iouringd)
target_include_directories(timeout_roundtrip PRIVATE include)
set_target_properties(timeout_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME timeout_roundtrip
  COMMAND $<TARGET_FILE:timeout_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(timeout_two_requests_roundtrip
  tests/integration/timeout_two_requests_roundtrip.c
)
target_link_libraries(timeout_two_requests_roundtrip PRIVATE iouringd)
target_include_directories(timeout_two_requests_roundtrip PRIVATE include)
set_target_properties(timeout_two_requests_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME timeout_two_requests_roundtrip
  COMMAND $<TARGET_FILE:timeout_two_requests_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(malformed_timeout_request_rejected
  tests/integration/malformed_timeout_request_rejected.c
)
target_link_libraries(malformed_timeout_request_rejected PRIVATE iouringd)
target_include_directories(malformed_timeout_request_rejected PRIVATE include)
set_target_properties(malformed_timeout_request_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME malformed_timeout_request_rejected
  COMMAND $<TARGET_FILE:malformed_timeout_request_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(malformed_timeout_then_nop_roundtrip
  tests/integration/malformed_timeout_then_nop_roundtrip.c
)
target_link_libraries(malformed_timeout_then_nop_roundtrip PRIVATE iouringd)
target_include_directories(malformed_timeout_then_nop_roundtrip PRIVATE include)
set_target_properties(malformed_timeout_then_nop_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME malformed_timeout_then_nop_roundtrip
  COMMAND $<TARGET_FILE:malformed_timeout_then_nop_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(cancel_request_rejected
  tests/integration/cancel_request_rejected.c
)
target_link_libraries(cancel_request_rejected PRIVATE iouringd)
target_include_directories(cancel_request_rejected PRIVATE include)
set_target_properties(cancel_request_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME cancel_request_rejected
  COMMAND $<TARGET_FILE:cancel_request_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(cancel_submission_rejected
  tests/integration/cancel_submission_rejected.c
)
target_link_libraries(cancel_submission_rejected PRIVATE iouringd)
target_include_directories(cancel_submission_rejected PRIVATE include)
set_target_properties(cancel_submission_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME cancel_submission_rejected
  COMMAND $<TARGET_FILE:cancel_submission_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(cancel_timeout_roundtrip
  tests/integration/cancel_timeout_roundtrip.c
)
target_link_libraries(cancel_timeout_roundtrip PRIVATE iouringd)
target_include_directories(cancel_timeout_roundtrip PRIVATE include)
set_target_properties(cancel_timeout_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME cancel_timeout_roundtrip
  COMMAND $<TARGET_FILE:cancel_timeout_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(cancel_partial_target_rejected
  tests/integration/cancel_partial_target_rejected.c
)
target_link_libraries(cancel_partial_target_rejected PRIVATE iouringd)
target_include_directories(cancel_partial_target_rejected PRIVATE include)
set_target_properties(cancel_partial_target_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME cancel_partial_target_rejected
  COMMAND $<TARGET_FILE:cancel_partial_target_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(submit_bad_magic_rejected
  tests/integration/submit_bad_magic_rejected.c
)
target_link_libraries(submit_bad_magic_rejected PRIVATE iouringd)
target_include_directories(submit_bad_magic_rejected PRIVATE include)
set_target_properties(submit_bad_magic_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME submit_bad_magic_rejected
  COMMAND $<TARGET_FILE:submit_bad_magic_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(submit_version_major_rejected
  tests/integration/submit_version_major_rejected.c
)
target_link_libraries(submit_version_major_rejected PRIVATE iouringd)
target_include_directories(submit_version_major_rejected PRIVATE include)
set_target_properties(submit_version_major_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME submit_version_major_rejected
  COMMAND $<TARGET_FILE:submit_version_major_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(submit_reserved_bits_rejected
  tests/integration/submit_reserved_bits_rejected.c
)
target_link_libraries(submit_reserved_bits_rejected PRIVATE iouringd)
target_include_directories(submit_reserved_bits_rejected PRIVATE include)
set_target_properties(submit_reserved_bits_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME submit_reserved_bits_rejected
  COMMAND $<TARGET_FILE:submit_reserved_bits_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(submit_reserved1_rejected
  tests/integration/submit_reserved1_rejected.c
)
target_link_libraries(submit_reserved1_rejected PRIVATE iouringd)
target_include_directories(submit_reserved1_rejected PRIVATE include)
set_target_properties(submit_reserved1_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME submit_reserved1_rejected
  COMMAND $<TARGET_FILE:submit_reserved1_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(unsupported_submit_kind_rejected
  tests/integration/unsupported_submit_kind_rejected.c
)
target_link_libraries(unsupported_submit_kind_rejected PRIVATE iouringd)
target_include_directories(unsupported_submit_kind_rejected PRIVATE include)
set_target_properties(unsupported_submit_kind_rejected PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME unsupported_submit_kind_rejected
  COMMAND $<TARGET_FILE:unsupported_submit_kind_rejected> $<TARGET_FILE:iouringd_daemon>
)

add_executable(nop_two_requests_roundtrip
  tests/integration/nop_two_requests_roundtrip.c
)
target_link_libraries(nop_two_requests_roundtrip PRIVATE iouringd)
target_include_directories(nop_two_requests_roundtrip PRIVATE include)
set_target_properties(nop_two_requests_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME nop_two_requests_roundtrip
  COMMAND $<TARGET_FILE:nop_two_requests_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(nop_then_timeout_roundtrip
  tests/integration/nop_then_timeout_roundtrip.c
)
target_link_libraries(nop_then_timeout_roundtrip PRIVATE iouringd)
target_include_directories(nop_then_timeout_roundtrip PRIVATE include)
set_target_properties(nop_then_timeout_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME nop_then_timeout_roundtrip
  COMMAND $<TARGET_FILE:nop_then_timeout_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(timeout_then_nop_roundtrip
  tests/integration/timeout_then_nop_roundtrip.c
)
target_link_libraries(timeout_then_nop_roundtrip PRIVATE iouringd)
target_include_directories(timeout_then_nop_roundtrip PRIVATE include)
set_target_properties(timeout_then_nop_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME timeout_then_nop_roundtrip
  COMMAND $<TARGET_FILE:timeout_then_nop_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

add_executable(rejected_handshake_blocks_submit
  tests/integration/rejected_handshake_blocks_submit.c
)
target_link_libraries(rejected_handshake_blocks_submit PRIVATE iouringd)
target_include_directories(rejected_handshake_blocks_submit PRIVATE include)
set_target_properties(rejected_handshake_blocks_submit PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME rejected_handshake_blocks_submit
  COMMAND $<TARGET_FILE:rejected_handshake_blocks_submit> $<TARGET_FILE:iouringd_daemon>
)

add_executable(unsupported_kernel_behavior
  tests/integration/unsupported_kernel_behavior.c
)
target_include_directories(unsupported_kernel_behavior PRIVATE include)
set_target_properties(unsupported_kernel_behavior PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME unsupported_kernel_behavior
  COMMAND $<TARGET_FILE:unsupported_kernel_behavior> $<TARGET_FILE:iouringd_daemon>
)

add_executable(restricted_policy_behavior
  tests/integration/restricted_policy_behavior.c
)
target_include_directories(restricted_policy_behavior PRIVATE include)
set_target_properties(restricted_policy_behavior PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME restricted_policy_behavior
  COMMAND $<TARGET_FILE:restricted_policy_behavior> $<TARGET_FILE:iouringd_daemon>
)

add_executable(trace_stderr_roundtrip
  tests/integration/trace_stderr_roundtrip.c
)
target_link_libraries(trace_stderr_roundtrip PRIVATE iouringd)
target_include_directories(trace_stderr_roundtrip PRIVATE include)
set_target_properties(trace_stderr_roundtrip PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME trace_stderr_roundtrip
  COMMAND $<TARGET_FILE:trace_stderr_roundtrip> $<TARGET_FILE:iouringd_daemon>
)

foreach(daemon_integration_target
    handshake_roundtrip
    nop_roundtrip
    reconnect_task_id_roundtrip
    graceful_shutdown
    register_sock_write_roundtrip
    register_sock_read_roundtrip
    register_file_read_roundtrip
    register_file_write_roundtrip
    openat_file_read_roundtrip
    openat_close_roundtrip
    register_connect_roundtrip
    register_accept_roundtrip
    submit_accept_einval_rejected
    register_poll_roundtrip
    register_buffer_fixed_io_roundtrip
    config_stats_roundtrip
    trace_roundtrip
    release_busy_resource_rejected
    two_clients_register_sock_write_roundtrip
    two_clients_timeout_roundtrip
    per_client_credits_roundtrip
    priority_dispatch_roundtrip
    bounded_admission_roundtrip
    timeout_roundtrip
    timeout_two_requests_roundtrip
    malformed_timeout_request_rejected
    malformed_timeout_then_nop_roundtrip
    cancel_request_rejected
    cancel_submission_rejected
    cancel_timeout_roundtrip
    cancel_partial_target_rejected
    submit_bad_magic_rejected
    submit_version_major_rejected
    submit_reserved_bits_rejected
    submit_reserved1_rejected
    unsupported_submit_kind_rejected
    nop_two_requests_roundtrip
    nop_then_timeout_roundtrip
    timeout_then_nop_roundtrip
    rejected_handshake_blocks_submit
    unsupported_kernel_behavior
    restricted_policy_behavior
    trace_stderr_roundtrip)
  add_dependencies(${daemon_integration_target} iouringd_daemon)
endforeach()

add_executable(handshake_invalid_response
  tests/integration/handshake_invalid_response.c
)
target_link_libraries(handshake_invalid_response PRIVATE iouringd)
target_include_directories(handshake_invalid_response PRIVATE include)
set_target_properties(handshake_invalid_response PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME handshake_invalid_response
  COMMAND $<TARGET_FILE:handshake_invalid_response>
)

add_executable(wait_completion_rejects_invalid
  tests/integration/wait_completion_rejects_invalid.c
)
target_link_libraries(wait_completion_rejects_invalid PRIVATE iouringd)
target_include_directories(wait_completion_rejects_invalid PRIVATE include)
set_target_properties(wait_completion_rejects_invalid PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME wait_completion_rejects_invalid
  COMMAND $<TARGET_FILE:wait_completion_rejects_invalid>
)

add_executable(completion_invalid_task_id
  tests/integration/completion_invalid_task_id.c
)
target_link_libraries(completion_invalid_task_id PRIVATE iouringd)
target_include_directories(completion_invalid_task_id PRIVATE include)
set_target_properties(completion_invalid_task_id PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME completion_invalid_task_id
  COMMAND $<TARGET_FILE:completion_invalid_task_id>
)

add_executable(invalid_completion_response
  tests/integration/invalid_completion_response.c
)
target_link_libraries(invalid_completion_response PRIVATE iouringd)
target_include_directories(invalid_completion_response PRIVATE include)
set_target_properties(invalid_completion_response PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME invalid_completion_response
  COMMAND $<TARGET_FILE:invalid_completion_response>
)

add_executable(handshake_client_rejects_invalid_response
  tests/integration/handshake_client_rejects_invalid_response.c
)
target_link_libraries(handshake_client_rejects_invalid_response PRIVATE iouringd)
target_include_directories(handshake_client_rejects_invalid_response PRIVATE include)
set_target_properties(handshake_client_rejects_invalid_response PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME handshake_client_rejects_invalid_response
  COMMAND $<TARGET_FILE:handshake_client_rejects_invalid_response>
)

add_executable(test_version
  tests/smoke/test_version.c
)
target_link_libraries(test_version PRIVATE iouringd)
target_include_directories(test_version PRIVATE include)
set_target_properties(test_version PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME test_version
  COMMAND $<TARGET_FILE:test_version>
)

add_executable(handshake_contract
  tests/abi/handshake_contract.c
)
target_include_directories(handshake_contract PRIVATE include)
set_target_properties(handshake_contract PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME handshake_contract
  COMMAND $<TARGET_FILE:handshake_contract>
)

add_executable(completion_res_contract
  tests/abi/completion_res_contract.c
)
target_include_directories(completion_res_contract PRIVATE include)
set_target_properties(completion_res_contract PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME completion_res_contract
  COMMAND $<TARGET_FILE:completion_res_contract>
)

add_executable(submit_result_contract
  tests/abi/submit_result_contract.c
)
target_include_directories(submit_result_contract PRIVATE include)
set_target_properties(submit_result_contract PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME submit_result_contract
  COMMAND $<TARGET_FILE:submit_result_contract>
)

add_executable(timeout_request_contract
  tests/abi/timeout_request_contract.c
)
target_include_directories(timeout_request_contract PRIVATE include)
set_target_properties(timeout_request_contract PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME timeout_request_contract
  COMMAND $<TARGET_FILE:timeout_request_contract>
)

add_executable(cancel_request_contract
  tests/abi/cancel_request_contract.c
)
target_include_directories(cancel_request_contract PRIVATE include)
set_target_properties(cancel_request_contract PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME cancel_request_contract
  COMMAND $<TARGET_FILE:cancel_request_contract>
)

add_executable(test_protocol_task_kinds
  tests/test_protocol_task_kinds.c
)
target_include_directories(test_protocol_task_kinds PRIVATE include)
set_target_properties(test_protocol_task_kinds PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME test_protocol_task_kinds
  COMMAND $<TARGET_FILE:test_protocol_task_kinds>
)

add_executable(client_handshake_validation
  tests/unit/client_handshake_validation.c
)
target_link_libraries(client_handshake_validation PRIVATE iouringd)
target_include_directories(client_handshake_validation PRIVATE include)
set_target_properties(client_handshake_validation PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME client_handshake_validation
  COMMAND $<TARGET_FILE:client_handshake_validation>
)

add_executable(client_completion_validation
  tests/unit/client_completion_validation.c
)
target_link_libraries(client_completion_validation PRIVATE iouringd)
target_include_directories(client_completion_validation PRIVATE include)
set_target_properties(client_completion_validation PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME client_completion_validation
  COMMAND $<TARGET_FILE:client_completion_validation>
)

add_executable(client_submit_validation
  tests/unit/client_submit_validation.c
)
target_link_libraries(client_submit_validation PRIVATE iouringd)
target_include_directories(client_submit_validation PRIVATE include)
set_target_properties(client_submit_validation PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
  NAME client_submit_validation
  COMMAND $<TARGET_FILE:client_submit_validation>
)
