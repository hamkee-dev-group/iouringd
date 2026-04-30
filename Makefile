CC ?= cc
CSTD = -std=c11
WARN = -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes -Wvla -Wformat=2
DEFS = -D_GNU_SOURCE
CPPFLAGS = -Iinclude
COMMON_HEADERS = include/iouringd/api.h include/iouringd/client.h \
	include/iouringd/protocol.h include/iouringd/submit.h \
	include/iouringd/version.h src/daemon/handshake.h src/daemon/submit.h

BUILD_DIR = build
TARGET = $(BUILD_DIR)/iouringd
ASAN_TARGET = $(BUILD_DIR)/iouringd.asan
UBSAN_TARGET = $(BUILD_DIR)/iouringd.ubsan
TSAN_TARGET = $(BUILD_DIR)/iouringd.tsan
TEST_TARGET = $(BUILD_DIR)/test_version
ABI_CONTRACT_TARGET = $(BUILD_DIR)/handshake_contract
COMPLETION_RES_CONTRACT_TARGET = $(BUILD_DIR)/completion_res_contract
SUBMIT_RESULT_CONTRACT_TARGET = $(BUILD_DIR)/submit_result_contract
TIMEOUT_REQUEST_CONTRACT_TARGET = $(BUILD_DIR)/timeout_request_contract
CANCEL_REQUEST_CONTRACT_TARGET = $(BUILD_DIR)/cancel_request_contract
PROTOCOL_TASK_KINDS_TARGET = $(BUILD_DIR)/test_protocol_task_kinds
CLIENT_HANDSHAKE_VALIDATION_TARGET = $(BUILD_DIR)/client_handshake_validation
CLIENT_COMPLETION_VALIDATION_TARGET = $(BUILD_DIR)/client_completion_validation
CLIENT_SUBMIT_VALIDATION_TARGET = $(BUILD_DIR)/client_submit_validation
ROUNDTRIP_DAEMON_TARGET = $(BUILD_DIR)/iouringd_daemon
HANDSHAKE_ROUNDTRIP_TARGET = $(BUILD_DIR)/handshake_roundtrip
NOP_ROUNDTRIP_TARGET = $(BUILD_DIR)/nop_roundtrip
NOP_TWO_REQUESTS_ROUNDTRIP_TARGET = $(BUILD_DIR)/nop_two_requests_roundtrip
RECONNECT_TASK_ID_ROUNDTRIP_TARGET = $(BUILD_DIR)/reconnect_task_id_roundtrip
GRACEFUL_SHUTDOWN_TARGET = $(BUILD_DIR)/graceful_shutdown
REGISTER_SOCK_WRITE_ROUNDTRIP_TARGET = $(BUILD_DIR)/register_sock_write_roundtrip
REGISTER_SOCK_READ_ROUNDTRIP_TARGET = $(BUILD_DIR)/register_sock_read_roundtrip
REGISTER_FILE_READ_ROUNDTRIP_TARGET = $(BUILD_DIR)/register_file_read_roundtrip
REGISTER_FILE_WRITE_ROUNDTRIP_TARGET = $(BUILD_DIR)/register_file_write_roundtrip
OPENAT_FILE_READ_ROUNDTRIP_TARGET = $(BUILD_DIR)/openat_file_read_roundtrip
OPENAT_CLOSE_ROUNDTRIP_TARGET = $(BUILD_DIR)/openat_close_roundtrip
REGISTER_CONNECT_ROUNDTRIP_TARGET = $(BUILD_DIR)/register_connect_roundtrip
REGISTER_ACCEPT_ROUNDTRIP_TARGET = $(BUILD_DIR)/register_accept_roundtrip
REGISTER_POLL_ROUNDTRIP_TARGET = $(BUILD_DIR)/register_poll_roundtrip
REGISTER_BUFFER_FIXED_IO_ROUNDTRIP_TARGET = $(BUILD_DIR)/register_buffer_fixed_io_roundtrip
CONFIG_STATS_ROUNDTRIP_TARGET = $(BUILD_DIR)/config_stats_roundtrip
TRACE_ROUNDTRIP_TARGET = $(BUILD_DIR)/trace_roundtrip
RELEASE_BUSY_RESOURCE_REJECTED_TARGET = $(BUILD_DIR)/release_busy_resource_rejected
TWO_CLIENTS_REGISTER_SOCK_WRITE_ROUNDTRIP_TARGET = $(BUILD_DIR)/two_clients_register_sock_write_roundtrip
TWO_CLIENTS_TIMEOUT_ROUNDTRIP_TARGET = $(BUILD_DIR)/two_clients_timeout_roundtrip
PER_CLIENT_CREDITS_ROUNDTRIP_TARGET = $(BUILD_DIR)/per_client_credits_roundtrip
PRIORITY_DISPATCH_ROUNDTRIP_TARGET = $(BUILD_DIR)/priority_dispatch_roundtrip
BOUNDED_ADMISSION_ROUNDTRIP_TARGET = $(BUILD_DIR)/bounded_admission_roundtrip
TIMEOUT_ROUNDTRIP_TARGET = $(BUILD_DIR)/timeout_roundtrip
TIMEOUT_TWO_REQUESTS_ROUNDTRIP_TARGET = $(BUILD_DIR)/timeout_two_requests_roundtrip
NOP_THEN_TIMEOUT_ROUNDTRIP_TARGET = $(BUILD_DIR)/nop_then_timeout_roundtrip
TIMEOUT_THEN_NOP_ROUNDTRIP_TARGET = $(BUILD_DIR)/timeout_then_nop_roundtrip
MALFORMED_TIMEOUT_REQUEST_REJECTED_TARGET = $(BUILD_DIR)/malformed_timeout_request_rejected
MALFORMED_TIMEOUT_THEN_NOP_ROUNDTRIP_TARGET = $(BUILD_DIR)/malformed_timeout_then_nop_roundtrip
CANCEL_REQUEST_REJECTED_TARGET = $(BUILD_DIR)/cancel_request_rejected
CANCEL_SUBMISSION_REJECTED_TARGET = $(BUILD_DIR)/cancel_submission_rejected
CANCEL_TIMEOUT_ROUNDTRIP_TARGET = $(BUILD_DIR)/cancel_timeout_roundtrip
SUBMIT_BAD_MAGIC_REJECTED_TARGET = $(BUILD_DIR)/submit_bad_magic_rejected
SUBMIT_RESERVED_BITS_REJECTED_TARGET = $(BUILD_DIR)/submit_reserved_bits_rejected
CANCEL_PARTIAL_TARGET_REJECTED_TARGET = $(BUILD_DIR)/cancel_partial_target_rejected
UNSUPPORTED_SUBMIT_KIND_REJECTED_TARGET = $(BUILD_DIR)/unsupported_submit_kind_rejected
WAIT_COMPLETION_REJECTS_INVALID_TARGET = $(BUILD_DIR)/wait_completion_rejects_invalid
COMPLETION_INVALID_TASK_ID_TARGET = $(BUILD_DIR)/completion_invalid_task_id
INVALID_COMPLETION_RESPONSE_TARGET = $(BUILD_DIR)/invalid_completion_response
HANDSHAKE_CLIENT_REJECTS_INVALID_RESPONSE_TARGET = $(BUILD_DIR)/handshake_client_rejects_invalid_response
HANDSHAKE_INVALID_RESPONSE_TARGET = $(BUILD_DIR)/handshake_invalid_response
REJECTED_HANDSHAKE_BLOCKS_SUBMIT_TARGET = $(BUILD_DIR)/rejected_handshake_blocks_submit
SUBMIT_VERSION_MAJOR_REJECTED_TARGET = $(BUILD_DIR)/submit_version_major_rejected
SUBMIT_RESERVED1_REJECTED_TARGET = $(BUILD_DIR)/submit_reserved1_rejected
SRC = src/iouringd.c
CPPCHECK = ./scripts/cppcheck.sh
STATIC_ANALYSIS = ./scripts/static_analysis.sh
ALL_BUILD_TARGETS = $(TARGET) $(ASAN_TARGET) $(UBSAN_TARGET) $(TSAN_TARGET) \
	$(TEST_TARGET) $(ABI_CONTRACT_TARGET) $(COMPLETION_RES_CONTRACT_TARGET) \
	$(SUBMIT_RESULT_CONTRACT_TARGET) $(TIMEOUT_REQUEST_CONTRACT_TARGET) \
	$(CANCEL_REQUEST_CONTRACT_TARGET) $(PROTOCOL_TASK_KINDS_TARGET) \
	$(CLIENT_HANDSHAKE_VALIDATION_TARGET) \
	$(CLIENT_COMPLETION_VALIDATION_TARGET) \
	$(CLIENT_SUBMIT_VALIDATION_TARGET) $(ROUNDTRIP_DAEMON_TARGET) \
	$(HANDSHAKE_ROUNDTRIP_TARGET) $(NOP_ROUNDTRIP_TARGET) \
	$(NOP_TWO_REQUESTS_ROUNDTRIP_TARGET) \
	$(RECONNECT_TASK_ID_ROUNDTRIP_TARGET) $(GRACEFUL_SHUTDOWN_TARGET) \
	$(REGISTER_SOCK_WRITE_ROUNDTRIP_TARGET) \
	$(REGISTER_SOCK_READ_ROUNDTRIP_TARGET) \
	$(REGISTER_FILE_READ_ROUNDTRIP_TARGET) \
	$(REGISTER_FILE_WRITE_ROUNDTRIP_TARGET) \
	$(OPENAT_FILE_READ_ROUNDTRIP_TARGET) \
	$(OPENAT_CLOSE_ROUNDTRIP_TARGET) \
	$(REGISTER_CONNECT_ROUNDTRIP_TARGET) \
	$(REGISTER_ACCEPT_ROUNDTRIP_TARGET) \
	$(REGISTER_POLL_ROUNDTRIP_TARGET) \
	$(REGISTER_BUFFER_FIXED_IO_ROUNDTRIP_TARGET) \
	$(CONFIG_STATS_ROUNDTRIP_TARGET) \
	$(TRACE_ROUNDTRIP_TARGET) \
	$(RELEASE_BUSY_RESOURCE_REJECTED_TARGET) \
	$(TWO_CLIENTS_REGISTER_SOCK_WRITE_ROUNDTRIP_TARGET) \
	$(TWO_CLIENTS_TIMEOUT_ROUNDTRIP_TARGET) \
	$(PER_CLIENT_CREDITS_ROUNDTRIP_TARGET) \
	$(PRIORITY_DISPATCH_ROUNDTRIP_TARGET) \
	$(BOUNDED_ADMISSION_ROUNDTRIP_TARGET) $(TIMEOUT_ROUNDTRIP_TARGET) \
	$(TIMEOUT_TWO_REQUESTS_ROUNDTRIP_TARGET) \
	$(NOP_THEN_TIMEOUT_ROUNDTRIP_TARGET) \
	$(TIMEOUT_THEN_NOP_ROUNDTRIP_TARGET) \
	$(MALFORMED_TIMEOUT_REQUEST_REJECTED_TARGET) \
	$(MALFORMED_TIMEOUT_THEN_NOP_ROUNDTRIP_TARGET) \
	$(CANCEL_REQUEST_REJECTED_TARGET) \
	$(CANCEL_SUBMISSION_REJECTED_TARGET) \
	$(CANCEL_TIMEOUT_ROUNDTRIP_TARGET) \
	$(SUBMIT_BAD_MAGIC_REJECTED_TARGET) \
	$(SUBMIT_RESERVED_BITS_REJECTED_TARGET) \
	$(CANCEL_PARTIAL_TARGET_REJECTED_TARGET) \
	$(UNSUPPORTED_SUBMIT_KIND_REJECTED_TARGET) \
	$(WAIT_COMPLETION_REJECTS_INVALID_TARGET) \
	$(COMPLETION_INVALID_TASK_ID_TARGET) \
	$(INVALID_COMPLETION_RESPONSE_TARGET) \
	$(HANDSHAKE_CLIENT_REJECTS_INVALID_RESPONSE_TARGET) \
	$(HANDSHAKE_INVALID_RESPONSE_TARGET) \
	$(REJECTED_HANDSHAKE_BLOCKS_SUBMIT_TARGET) \
	$(SUBMIT_VERSION_MAJOR_REJECTED_TARGET) \
	$(SUBMIT_RESERVED1_REJECTED_TARGET)

.PHONY: all debug asan ubsan tsan clean lint test validate

all: $(TARGET)

debug: CFLAGS = -O0 -g
debug: $(TARGET)

asan: CFLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
asan: $(ASAN_TARGET)

ubsan: CFLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=undefined
ubsan: $(UBSAN_TARGET)

tsan: CFLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=thread
tsan: $(TSAN_TARGET)

lint:
	$(CPPCHECK)
	$(STATIC_ANALYSIS)

test: asan ubsan tsan $(TEST_TARGET) $(ABI_CONTRACT_TARGET) $(COMPLETION_RES_CONTRACT_TARGET) $(SUBMIT_RESULT_CONTRACT_TARGET) $(TIMEOUT_REQUEST_CONTRACT_TARGET) $(CANCEL_REQUEST_CONTRACT_TARGET) $(PROTOCOL_TASK_KINDS_TARGET) $(CLIENT_HANDSHAKE_VALIDATION_TARGET) $(CLIENT_COMPLETION_VALIDATION_TARGET) $(CLIENT_SUBMIT_VALIDATION_TARGET) $(ROUNDTRIP_DAEMON_TARGET) $(HANDSHAKE_ROUNDTRIP_TARGET) $(NOP_ROUNDTRIP_TARGET) $(NOP_TWO_REQUESTS_ROUNDTRIP_TARGET) $(RECONNECT_TASK_ID_ROUNDTRIP_TARGET) $(GRACEFUL_SHUTDOWN_TARGET) $(REGISTER_SOCK_WRITE_ROUNDTRIP_TARGET) $(REGISTER_SOCK_READ_ROUNDTRIP_TARGET) $(REGISTER_FILE_READ_ROUNDTRIP_TARGET) $(REGISTER_FILE_WRITE_ROUNDTRIP_TARGET) $(OPENAT_FILE_READ_ROUNDTRIP_TARGET) $(OPENAT_CLOSE_ROUNDTRIP_TARGET) $(REGISTER_CONNECT_ROUNDTRIP_TARGET) $(REGISTER_ACCEPT_ROUNDTRIP_TARGET) $(REGISTER_POLL_ROUNDTRIP_TARGET) $(REGISTER_BUFFER_FIXED_IO_ROUNDTRIP_TARGET) $(CONFIG_STATS_ROUNDTRIP_TARGET) $(TRACE_ROUNDTRIP_TARGET) $(RELEASE_BUSY_RESOURCE_REJECTED_TARGET) $(TWO_CLIENTS_REGISTER_SOCK_WRITE_ROUNDTRIP_TARGET) $(TWO_CLIENTS_TIMEOUT_ROUNDTRIP_TARGET) $(PER_CLIENT_CREDITS_ROUNDTRIP_TARGET) $(PRIORITY_DISPATCH_ROUNDTRIP_TARGET) $(BOUNDED_ADMISSION_ROUNDTRIP_TARGET) $(TIMEOUT_ROUNDTRIP_TARGET) $(TIMEOUT_TWO_REQUESTS_ROUNDTRIP_TARGET) $(NOP_THEN_TIMEOUT_ROUNDTRIP_TARGET) $(TIMEOUT_THEN_NOP_ROUNDTRIP_TARGET) $(MALFORMED_TIMEOUT_REQUEST_REJECTED_TARGET) $(MALFORMED_TIMEOUT_THEN_NOP_ROUNDTRIP_TARGET) $(CANCEL_REQUEST_REJECTED_TARGET) $(CANCEL_SUBMISSION_REJECTED_TARGET) $(CANCEL_TIMEOUT_ROUNDTRIP_TARGET) $(CANCEL_PARTIAL_TARGET_REJECTED_TARGET) $(SUBMIT_BAD_MAGIC_REJECTED_TARGET) $(SUBMIT_RESERVED_BITS_REJECTED_TARGET) $(UNSUPPORTED_SUBMIT_KIND_REJECTED_TARGET) $(WAIT_COMPLETION_REJECTS_INVALID_TARGET) $(COMPLETION_INVALID_TASK_ID_TARGET) $(INVALID_COMPLETION_RESPONSE_TARGET) $(HANDSHAKE_CLIENT_REJECTS_INVALID_RESPONSE_TARGET) $(HANDSHAKE_INVALID_RESPONSE_TARGET) $(REJECTED_HANDSHAKE_BLOCKS_SUBMIT_TARGET) $(SUBMIT_VERSION_MAJOR_REJECTED_TARGET) $(SUBMIT_RESERVED1_REJECTED_TARGET)
	tests/run_tests.sh

validate: all lint test asan ubsan
	./$(TARGET)
	./$(ASAN_TARGET)
	./$(UBSAN_TARGET)

$(ALL_BUILD_TARGETS): $(COMMON_HEADERS)

$(TARGET): $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $< -o $@

$(ASAN_TARGET): $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) $(CFLAGS) $< -o $@

$(UBSAN_TARGET): $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) $(CFLAGS) $< -o $@

$(TSAN_TARGET): $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) $(CFLAGS) $< -o $@

$(TEST_TARGET): tests/smoke/test_version.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(ABI_CONTRACT_TARGET): tests/abi/handshake_contract.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $< -o $@

$(COMPLETION_RES_CONTRACT_TARGET): tests/abi/completion_res_contract.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $< -o $@

$(SUBMIT_RESULT_CONTRACT_TARGET): tests/abi/submit_result_contract.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $< -o $@

$(TIMEOUT_REQUEST_CONTRACT_TARGET): tests/abi/timeout_request_contract.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $< -o $@

$(CANCEL_REQUEST_CONTRACT_TARGET): tests/abi/cancel_request_contract.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $< -o $@

$(PROTOCOL_TASK_KINDS_TARGET): tests/test_protocol_task_kinds.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $< -o $@

$(CLIENT_HANDSHAKE_VALIDATION_TARGET): tests/unit/client_handshake_validation.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(CLIENT_COMPLETION_VALIDATION_TARGET): tests/unit/client_completion_validation.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(CLIENT_SUBMIT_VALIDATION_TARGET): tests/unit/client_submit_validation.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(ROUNDTRIP_DAEMON_TARGET): src/daemon/main.c src/daemon/handshake.c src/daemon/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) -Isrc/daemon -O2 -g $(filter %.c,$^) -o $@

$(HANDSHAKE_ROUNDTRIP_TARGET): tests/integration/handshake_roundtrip.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(NOP_ROUNDTRIP_TARGET): tests/integration/nop_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(NOP_TWO_REQUESTS_ROUNDTRIP_TARGET): tests/integration/nop_two_requests_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(RECONNECT_TASK_ID_ROUNDTRIP_TARGET): tests/integration/reconnect_task_id_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(GRACEFUL_SHUTDOWN_TARGET): tests/integration/graceful_shutdown.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REGISTER_SOCK_WRITE_ROUNDTRIP_TARGET): tests/integration/register_sock_write_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REGISTER_SOCK_READ_ROUNDTRIP_TARGET): tests/integration/register_sock_read_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REGISTER_FILE_READ_ROUNDTRIP_TARGET): tests/integration/register_file_read_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REGISTER_FILE_WRITE_ROUNDTRIP_TARGET): tests/integration/register_file_write_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(OPENAT_FILE_READ_ROUNDTRIP_TARGET): tests/integration/openat_file_read_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(OPENAT_CLOSE_ROUNDTRIP_TARGET): tests/integration/openat_close_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REGISTER_CONNECT_ROUNDTRIP_TARGET): tests/integration/register_connect_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REGISTER_ACCEPT_ROUNDTRIP_TARGET): tests/integration/register_accept_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REGISTER_POLL_ROUNDTRIP_TARGET): tests/integration/register_poll_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REGISTER_BUFFER_FIXED_IO_ROUNDTRIP_TARGET): tests/integration/register_buffer_fixed_io_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(CONFIG_STATS_ROUNDTRIP_TARGET): tests/integration/config_stats_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(TRACE_ROUNDTRIP_TARGET): tests/integration/trace_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(RELEASE_BUSY_RESOURCE_REJECTED_TARGET): tests/integration/release_busy_resource_rejected.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(TWO_CLIENTS_REGISTER_SOCK_WRITE_ROUNDTRIP_TARGET): tests/integration/two_clients_register_sock_write_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(TWO_CLIENTS_TIMEOUT_ROUNDTRIP_TARGET): tests/integration/two_clients_timeout_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(PER_CLIENT_CREDITS_ROUNDTRIP_TARGET): tests/integration/per_client_credits_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(PRIORITY_DISPATCH_ROUNDTRIP_TARGET): tests/integration/priority_dispatch_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(BOUNDED_ADMISSION_ROUNDTRIP_TARGET): tests/integration/bounded_admission_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(TIMEOUT_ROUNDTRIP_TARGET): tests/integration/timeout_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(TIMEOUT_TWO_REQUESTS_ROUNDTRIP_TARGET): tests/integration/timeout_two_requests_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(NOP_THEN_TIMEOUT_ROUNDTRIP_TARGET): tests/integration/nop_then_timeout_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(TIMEOUT_THEN_NOP_ROUNDTRIP_TARGET): tests/integration/timeout_then_nop_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(MALFORMED_TIMEOUT_REQUEST_REJECTED_TARGET): tests/integration/malformed_timeout_request_rejected.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(MALFORMED_TIMEOUT_THEN_NOP_ROUNDTRIP_TARGET): tests/integration/malformed_timeout_then_nop_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(CANCEL_REQUEST_REJECTED_TARGET): tests/integration/cancel_request_rejected.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(CANCEL_SUBMISSION_REJECTED_TARGET): tests/integration/cancel_submission_rejected.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(CANCEL_TIMEOUT_ROUNDTRIP_TARGET): tests/integration/cancel_timeout_roundtrip.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(CANCEL_PARTIAL_TARGET_REJECTED_TARGET): tests/integration/cancel_partial_target_rejected.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(SUBMIT_BAD_MAGIC_REJECTED_TARGET): tests/integration/submit_bad_magic_rejected.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(SUBMIT_RESERVED_BITS_REJECTED_TARGET): tests/integration/submit_reserved_bits_rejected.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(UNSUPPORTED_SUBMIT_KIND_REJECTED_TARGET): tests/integration/unsupported_submit_kind_rejected.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(WAIT_COMPLETION_REJECTS_INVALID_TARGET): tests/integration/wait_completion_rejects_invalid.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(COMPLETION_INVALID_TASK_ID_TARGET): tests/integration/completion_invalid_task_id.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(INVALID_COMPLETION_RESPONSE_TARGET): tests/integration/invalid_completion_response.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(HANDSHAKE_CLIENT_REJECTS_INVALID_RESPONSE_TARGET): tests/integration/handshake_client_rejects_invalid_response.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(HANDSHAKE_INVALID_RESPONSE_TARGET): tests/integration/handshake_invalid_response.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(REJECTED_HANDSHAKE_BLOCKS_SUBMIT_TARGET): tests/integration/rejected_handshake_blocks_submit.c src/lib/client.c src/lib/submit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(SUBMIT_VERSION_MAJOR_REJECTED_TARGET): tests/integration/submit_version_major_rejected.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

$(SUBMIT_RESERVED1_REJECTED_TARGET): tests/integration/submit_reserved1_rejected.c src/lib/client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(DEFS) -O2 -g $(filter %.c,$^) -o $@

clean:
	rm -rf $(BUILD_DIR)
