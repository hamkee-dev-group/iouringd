#ifndef IOURINGD_API_H
#define IOURINGD_API_H

#include <stddef.h>
#include <stdint.h>

#include "iouringd/protocol.h"

#define IOURINGD_ABI_V1_MAJOR 1
#define IOURINGD_ABI_V1_MINOR 12

#define IOURINGD_PROTOCOL_V1_MAJOR 1
#define IOURINGD_PROTOCOL_V1_MINOR 12

#define IOURINGD_PROTOCOL_ENDIAN_LITTLE 1
#define IOURINGD_PROTOCOL_PACKING_NONE 1
#define IOURINGD_PROTOCOL_WIRE_MAGIC UINT32_C(0x31444f49)

#define IOURINGD_HANDSHAKE_STATUS_ACCEPT 0u
#define IOURINGD_HANDSHAKE_STATUS_REJECT 1u
#define IOURINGD_HANDSHAKE_STATUS_DEGRADE 2u

#define IOURINGD_CAPABILITY_OP_NOP UINT32_C(1)
#define IOURINGD_CAPABILITY_OP_TIMEOUT UINT32_C(1) << 1
#define IOURINGD_CAPABILITY_OP_READV UINT32_C(1) << 2
#define IOURINGD_CAPABILITY_OP_WRITEV UINT32_C(1) << 3
#define IOURINGD_CAPABILITY_OP_ASYNC_CANCEL UINT32_C(1) << 4
#define IOURINGD_CAPABILITY_OP_READ_FIXED UINT32_C(1) << 5
#define IOURINGD_CAPABILITY_OP_WRITE_FIXED UINT32_C(1) << 6
#define IOURINGD_CAPABILITY_OP_POLL UINT32_C(1) << 7
#define IOURINGD_CAPABILITY_OP_CONNECT UINT32_C(1) << 8
#define IOURINGD_CAPABILITY_OP_ACCEPT UINT32_C(1) << 9
#define IOURINGD_CAPABILITY_OP_OPENAT UINT32_C(1) << 10
#define IOURINGD_CAPABILITY_OP_CLOSE UINT32_C(1) << 11
#define IOURINGD_CAPABILITY_OP_MASK_V1                                            \
    (IOURINGD_CAPABILITY_OP_NOP | IOURINGD_CAPABILITY_OP_TIMEOUT |            \
     IOURINGD_CAPABILITY_OP_READV | IOURINGD_CAPABILITY_OP_WRITEV |           \
     IOURINGD_CAPABILITY_OP_ASYNC_CANCEL | IOURINGD_CAPABILITY_OP_READ_FIXED | \
     IOURINGD_CAPABILITY_OP_WRITE_FIXED | IOURINGD_CAPABILITY_OP_POLL |       \
     IOURINGD_CAPABILITY_OP_CONNECT | IOURINGD_CAPABILITY_OP_ACCEPT |         \
     IOURINGD_CAPABILITY_OP_OPENAT | IOURINGD_CAPABILITY_OP_CLOSE)
#define IOURINGD_CAPABILITY_DESCRIPTOR_V1_STRUCT_SIZE UINT32_C(36)

#define IOURINGD_REQUEST_KIND_REGISTER_FD UINT16_C(256)
#define IOURINGD_REQUEST_KIND_RELEASE_RESOURCE UINT16_C(257)
#define IOURINGD_REQUEST_KIND_REGISTER_BUFFER UINT16_C(258)
#define IOURINGD_REQUEST_KIND_GET_STATS UINT16_C(259)
#define IOURINGD_REQUEST_KIND_GET_TRACE UINT16_C(260)

#define IOURINGD_SUBMIT_PRIORITY_NORMAL UINT16_C(0)
#define IOURINGD_SUBMIT_PRIORITY_LOW UINT16_C(1)
#define IOURINGD_SUBMIT_PRIORITY_HIGH UINT16_C(2)
#define IOURINGD_SUBMIT_PRIORITY_MAX_V1 UINT16_C(3)

#define IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT UINT16_C(1)
#define IOURINGD_TRACE_EVENT_SUBMIT_REJECT UINT16_C(2)
#define IOURINGD_TRACE_EVENT_COMPLETION UINT16_C(3)
#define IOURINGD_TRACE_EVENT_RESOURCE_REGISTER UINT16_C(4)
#define IOURINGD_TRACE_EVENT_RESOURCE_RELEASE UINT16_C(5)

#define IOURINGD_TASK_ID_RESERVED_MIN (UINT64_MAX - UINT64_C(15))
#define IOURINGD_TASK_ID_RESERVED_MAX UINT64_MAX






struct iouringd_task_id_record_v1 {
    iouringd_task_id_t task_id;
};

struct iouringd_resource_id_record_v1 {
    iouringd_resource_id_t resource_id;
};

struct iouringd_protocol_header_v1 {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
};

struct iouringd_handshake_request_v1 {
    struct iouringd_protocol_header_v1 header;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t reserved;
};

struct iouringd_handshake_response_v1 {
    struct iouringd_protocol_header_v1 header;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t status;
    uint16_t reserved;
};

struct iouringd_capability_descriptor_v1 {
    uint32_t struct_size;
    uint32_t features;
    uint32_t ring_entries;
    uint32_t cq_entries;
    uint32_t op_mask;
    uint32_t submit_credits;
    uint32_t registered_fd_slots;
    uint32_t registered_buffer_slots;
    uint32_t io_bytes_max;
};

struct iouringd_handshake_result_v1 {
    struct iouringd_handshake_response_v1 response;
    struct iouringd_capability_descriptor_v1 capabilities;
};

struct iouringd_submit_request_v1 {
    struct iouringd_protocol_header_v1 header;
    iouringd_task_kind_wire_v1 task_kind;
    uint16_t priority;
    uint32_t reserved1;
};

struct iouringd_control_request_v1 {
    struct iouringd_protocol_header_v1 header;
    uint16_t request_kind;
    uint16_t reserved0;
    uint32_t reserved1;
};

struct iouringd_timeout_request_v1 {
    struct iouringd_submit_request_v1 submit;
    uint64_t timeout_nsec;
};





struct iouringd_cancel_request_v1 {
    struct iouringd_protocol_header_v1 header;
    iouringd_task_kind_wire_v1 task_kind;
    uint16_t reserved0;
    uint32_t reserved1;
    struct iouringd_task_id_record_v1 target;
};

struct iouringd_sock_read_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 resource;
    uint32_t length;
};

struct iouringd_sock_write_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 resource;
    uint32_t length;
};

struct iouringd_file_read_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 resource;
    uint32_t length;
};

struct iouringd_file_write_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 resource;
    uint32_t length;
};

struct iouringd_poll_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 resource;
    uint16_t poll_mask;
    uint16_t reserved2;
};

struct iouringd_connect_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 resource;
    uint32_t sockaddr_length;
};

struct iouringd_accept_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 resource;
    uint32_t sockaddr_length;
};

struct iouringd_openat_request_v1 {
    struct iouringd_submit_request_v1 submit;
    int32_t open_flags;
    uint32_t open_mode;
    uint32_t path_length;
};

struct iouringd_close_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 resource;
    uint32_t reserved2;
};

struct iouringd_sock_read_fixed_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 file_resource;
    struct iouringd_resource_id_record_v1 buffer_resource;
    uint32_t length;
};

struct iouringd_sock_write_fixed_request_v1 {
    struct iouringd_submit_request_v1 submit;
    struct iouringd_resource_id_record_v1 file_resource;
    struct iouringd_resource_id_record_v1 buffer_resource;
    uint32_t length;
};

struct iouringd_register_fd_request_v1 {
    struct iouringd_control_request_v1 control;
};

struct iouringd_register_buffer_request_v1 {
    struct iouringd_control_request_v1 control;
    uint32_t length;
    uint32_t reserved2;
};

struct iouringd_release_resource_request_v1 {
    struct iouringd_control_request_v1 control;
    struct iouringd_resource_id_record_v1 resource;
    uint32_t reserved2;
};

struct iouringd_get_stats_request_v1 {
    struct iouringd_control_request_v1 control;
};

struct iouringd_get_trace_request_v1 {
    struct iouringd_control_request_v1 control;
    uint64_t after_sequence;
    uint32_t max_events;
    uint32_t reserved2;
};






struct iouringd_submit_result_v1 {
    struct iouringd_task_id_record_v1 task;
    int32_t res;
    uint32_t credits;
};






struct iouringd_resource_result_v1 {
    struct iouringd_resource_id_record_v1 resource;
    int32_t res;
    uint32_t resources_available;
    uint32_t reserved;
};

struct iouringd_stats_result_v1 {
    uint32_t active_clients;
    uint32_t outstanding_tasks;
    uint32_t outstanding_credit_tasks;
    uint32_t available_credits;
    uint32_t registered_files;
    uint32_t registered_buffers;
    uint64_t accepted_submits;
    uint64_t rejected_submits;
    uint64_t completions;
    uint32_t per_client_credit_limit;
    uint32_t clients_at_credit_limit;
};

struct iouringd_trace_result_v1 {
    uint64_t oldest_sequence;
    uint64_t latest_sequence;
    uint32_t count;
    uint32_t reserved;
};

struct iouringd_trace_event_v1 {
    uint64_t sequence;
    uint64_t timestamp_nsec;
    struct iouringd_task_id_record_v1 task;
    struct iouringd_resource_id_record_v1 resource;
    int32_t res;
    uint32_t credits;
    uint16_t event_kind;
    iouringd_task_kind_wire_v1 task_kind;
    uint16_t client_slot;
    uint16_t priority;
    uint32_t reserved1;
};

 
#define IOURINGD_COMPLETION_RES_OK ((int32_t)0)
#define IOURINGD_COMPLETION_RES_IS_ERROR(x) ((int32_t)(x) < 0)

struct iouringd_completion_record_v1 {
    struct iouringd_protocol_header_v1 header;
    struct iouringd_task_id_record_v1 task;
    int32_t res;
    iouringd_task_kind_wire_v1 task_kind;
    uint16_t payload_length;
};

_Static_assert(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");
_Static_assert(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");
_Static_assert(sizeof(uint64_t) == 8, "uint64_t must be 8 bytes");
_Static_assert(sizeof(iouringd_task_id_t) == 8, "task id size changed");
_Static_assert(sizeof(iouringd_resource_id_t) == 4, "resource id size changed");
_Static_assert((iouringd_task_id_t)-1 > 0, "task id must be unsigned");
_Static_assert(IOURINGD_TASK_ID_INVALID == UINT64_C(0),
               "task id invalid value changed");
_Static_assert(IOURINGD_RESOURCE_ID_INVALID == UINT32_C(0),
               "resource id invalid value changed");
_Static_assert(IOURINGD_TASK_ID_RESERVED_MIN == (UINT64_MAX - UINT64_C(15)),
               "task id reserved minimum changed");
_Static_assert(IOURINGD_TASK_ID_RESERVED_MAX == UINT64_MAX,
               "task id reserved maximum changed");
_Static_assert(IOURINGD_REQUEST_KIND_REGISTER_FD == UINT16_C(256),
               "register fd request kind changed");
_Static_assert(IOURINGD_REQUEST_KIND_RELEASE_RESOURCE == UINT16_C(257),
               "release resource request kind changed");
_Static_assert(IOURINGD_REQUEST_KIND_REGISTER_BUFFER == UINT16_C(258),
               "register buffer request kind changed");
_Static_assert(IOURINGD_REQUEST_KIND_GET_STATS == UINT16_C(259),
               "get stats request kind changed");
_Static_assert(IOURINGD_REQUEST_KIND_GET_TRACE == UINT16_C(260),
               "get trace request kind changed");
_Static_assert(IOURINGD_SUBMIT_PRIORITY_NORMAL == UINT16_C(0),
               "submit priority normal value changed");
_Static_assert(IOURINGD_SUBMIT_PRIORITY_LOW == UINT16_C(1),
               "submit priority low value changed");
_Static_assert(IOURINGD_SUBMIT_PRIORITY_HIGH == UINT16_C(2),
               "submit priority high value changed");
_Static_assert(IOURINGD_SUBMIT_PRIORITY_MAX_V1 == UINT16_C(3),
               "submit priority max value changed");
_Static_assert(IOURINGD_CAPABILITY_OP_NOP == UINT32_C(1),
               "nop capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_TIMEOUT == (UINT32_C(1) << 1),
               "timeout capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_READV == (UINT32_C(1) << 2),
               "readv capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_WRITEV == (UINT32_C(1) << 3),
               "writev capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_ASYNC_CANCEL == (UINT32_C(1) << 4),
               "async cancel capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_READ_FIXED == (UINT32_C(1) << 5),
               "read fixed capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_WRITE_FIXED == (UINT32_C(1) << 6),
               "write fixed capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_POLL == (UINT32_C(1) << 7),
               "poll capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_CONNECT == (UINT32_C(1) << 8),
               "connect capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_ACCEPT == (UINT32_C(1) << 9),
               "accept capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_OPENAT == (UINT32_C(1) << 10),
               "openat capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_CLOSE == (UINT32_C(1) << 11),
               "close capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_MASK_V1 == UINT32_C(0xfff),
               "capability op v1 mask changed");
_Static_assert(IOURINGD_COMPLETION_RES_OK == 0, "completion ok value changed");
_Static_assert((int32_t)-1 < 0, "completion res must be signed");

_Static_assert(sizeof(struct iouringd_task_id_record_v1) == 8,
               "task id record size changed");
_Static_assert(_Alignof(struct iouringd_task_id_record_v1) ==
                   _Alignof(iouringd_task_id_t),
               "task id record alignment changed");
_Static_assert(offsetof(struct iouringd_task_id_record_v1, task_id) == 0,
               "task id record offset changed");

_Static_assert(sizeof(struct iouringd_resource_id_record_v1) == 4,
               "resource id record size changed");
_Static_assert(_Alignof(struct iouringd_resource_id_record_v1) ==
                   _Alignof(iouringd_resource_id_t),
               "resource id record alignment changed");
_Static_assert(offsetof(struct iouringd_resource_id_record_v1, resource_id) == 0,
               "resource id record offset changed");

_Static_assert(sizeof(struct iouringd_protocol_header_v1) == 8,
               "protocol header size changed");
_Static_assert(offsetof(struct iouringd_protocol_header_v1, magic) == 0,
               "protocol header magic offset changed");
_Static_assert(offsetof(struct iouringd_protocol_header_v1, version_major) == 4,
               "protocol header major offset changed");
_Static_assert(offsetof(struct iouringd_protocol_header_v1, version_minor) == 6,
               "protocol header minor offset changed");

_Static_assert(sizeof(struct iouringd_handshake_request_v1) == 16,
               "handshake request size changed");
_Static_assert(offsetof(struct iouringd_handshake_request_v1, header) == 0,
               "handshake request header offset changed");
_Static_assert(offsetof(struct iouringd_handshake_request_v1, abi_major) == 8,
               "handshake request abi major offset changed");
_Static_assert(offsetof(struct iouringd_handshake_request_v1, abi_minor) == 10,
               "handshake request abi minor offset changed");
_Static_assert(offsetof(struct iouringd_handshake_request_v1, reserved) == 12,
               "handshake request reserved offset changed");

_Static_assert(sizeof(struct iouringd_handshake_response_v1) == 16,
               "handshake response size changed");
_Static_assert(offsetof(struct iouringd_handshake_response_v1, header) == 0,
               "handshake response header offset changed");
_Static_assert(offsetof(struct iouringd_handshake_response_v1, abi_major) == 8,
               "handshake response abi major offset changed");
_Static_assert(offsetof(struct iouringd_handshake_response_v1, abi_minor) == 10,
               "handshake response abi minor offset changed");
_Static_assert(offsetof(struct iouringd_handshake_response_v1, status) == 12,
               "handshake response status offset changed");
_Static_assert(offsetof(struct iouringd_handshake_response_v1, reserved) == 14,
               "handshake response reserved offset changed");

_Static_assert(sizeof(struct iouringd_capability_descriptor_v1) == 36,
               "capability descriptor size changed");
_Static_assert(IOURINGD_CAPABILITY_DESCRIPTOR_V1_STRUCT_SIZE ==
                   sizeof(struct iouringd_capability_descriptor_v1),
               "capability descriptor struct_size value changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, struct_size) == 0,
               "capability descriptor size offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, features) == 4,
               "capability descriptor features offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, ring_entries) == 8,
               "capability descriptor ring entries offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, cq_entries) == 12,
               "capability descriptor cq entries offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, op_mask) == 16,
               "capability descriptor op mask offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, submit_credits) == 20,
               "capability descriptor credits offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, registered_fd_slots) == 24,
               "capability descriptor fd slots offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, registered_buffer_slots) == 28,
               "capability descriptor buffer slots offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, io_bytes_max) == 32,
               "capability descriptor io bytes offset changed");

_Static_assert(sizeof(struct iouringd_handshake_result_v1) == 52,
               "handshake result size changed");
_Static_assert(offsetof(struct iouringd_handshake_result_v1, response) == 0,
               "handshake result response offset changed");
_Static_assert(offsetof(struct iouringd_handshake_result_v1, capabilities) == 16,
               "handshake result capabilities offset changed");

_Static_assert(sizeof(struct iouringd_submit_request_v1) == 16,
               "submit request size changed");
_Static_assert(offsetof(struct iouringd_submit_request_v1, header) == 0,
               "submit request header offset changed");
_Static_assert(offsetof(struct iouringd_submit_request_v1, task_kind) == 8,
               "submit request task kind offset changed");
_Static_assert(offsetof(struct iouringd_submit_request_v1, priority) == 10,
               "submit request priority offset changed");
_Static_assert(offsetof(struct iouringd_submit_request_v1, reserved1) == 12,
               "submit request reserved1 offset changed");

_Static_assert(sizeof(struct iouringd_control_request_v1) == 16,
               "control request size changed");
_Static_assert(offsetof(struct iouringd_control_request_v1, header) == 0,
               "control request header offset changed");
_Static_assert(offsetof(struct iouringd_control_request_v1, request_kind) == 8,
               "control request kind offset changed");
_Static_assert(offsetof(struct iouringd_control_request_v1, reserved1) == 12,
               "control request reserved1 offset changed");

_Static_assert(sizeof(struct iouringd_timeout_request_v1) == 24,
               "timeout request size changed");
_Static_assert(_Alignof(struct iouringd_timeout_request_v1) ==
                   _Alignof(uint64_t),
               "timeout request alignment changed");
_Static_assert(offsetof(struct iouringd_timeout_request_v1, submit) == 0,
               "timeout request submit offset changed");
_Static_assert(offsetof(struct iouringd_timeout_request_v1, submit.header) == 0,
               "timeout request header offset changed");
_Static_assert(offsetof(struct iouringd_timeout_request_v1, submit.task_kind) == 8,
               "timeout request task kind offset changed");
_Static_assert(offsetof(struct iouringd_timeout_request_v1, timeout_nsec) == 16,
               "timeout request timeout offset changed");

_Static_assert(sizeof(struct iouringd_cancel_request_v1) == 24,
               "cancel request size changed");
_Static_assert(_Alignof(struct iouringd_cancel_request_v1) == 8,
               "cancel request alignment changed");
_Static_assert(offsetof(struct iouringd_cancel_request_v1, header) == 0,
               "cancel request header offset changed");
_Static_assert(offsetof(struct iouringd_cancel_request_v1, task_kind) == 8,
               "cancel request task kind offset changed");
_Static_assert(offsetof(struct iouringd_cancel_request_v1, target) == 16,
               "cancel request target offset changed");
_Static_assert(offsetof(struct iouringd_cancel_request_v1, target.task_id) == 16,
               "cancel request target task id offset changed");

_Static_assert(sizeof(struct iouringd_sock_read_request_v1) == 24,
               "sock read request size changed");
_Static_assert(offsetof(struct iouringd_sock_read_request_v1, submit) == 0,
               "sock read request submit offset changed");
_Static_assert(offsetof(struct iouringd_sock_read_request_v1, resource) == 16,
               "sock read request resource offset changed");
_Static_assert(offsetof(struct iouringd_sock_read_request_v1, length) == 20,
               "sock read request length offset changed");

_Static_assert(sizeof(struct iouringd_sock_write_request_v1) == 24,
               "sock write request size changed");
_Static_assert(sizeof(struct iouringd_file_read_request_v1) == 24,
               "file read request size changed");
_Static_assert(sizeof(struct iouringd_file_write_request_v1) == 24,
               "file write request size changed");
_Static_assert(sizeof(struct iouringd_poll_request_v1) == 24,
               "poll request size changed");
_Static_assert(sizeof(struct iouringd_connect_request_v1) == 24,
               "connect request size changed");
_Static_assert(sizeof(struct iouringd_accept_request_v1) == 24,
               "accept request size changed");
_Static_assert(sizeof(struct iouringd_openat_request_v1) == 28,
               "openat request size changed");
_Static_assert(sizeof(struct iouringd_close_request_v1) == 24,
               "close request size changed");
_Static_assert(sizeof(struct iouringd_sock_read_fixed_request_v1) == 28,
               "sock read fixed request size changed");
_Static_assert(sizeof(struct iouringd_sock_write_fixed_request_v1) == 28,
               "sock write fixed request size changed");
_Static_assert(sizeof(struct iouringd_register_buffer_request_v1) == 24,
               "register buffer request size changed");
_Static_assert(offsetof(struct iouringd_sock_write_request_v1, submit) == 0,
               "sock write request submit offset changed");
_Static_assert(offsetof(struct iouringd_sock_write_request_v1, resource) == 16,
               "sock write request resource offset changed");
_Static_assert(offsetof(struct iouringd_sock_write_request_v1, length) == 20,
               "sock write request length offset changed");
_Static_assert(offsetof(struct iouringd_file_read_request_v1, submit) == 0,
               "file read request submit offset changed");
_Static_assert(offsetof(struct iouringd_file_read_request_v1, resource) == 16,
               "file read request resource offset changed");
_Static_assert(offsetof(struct iouringd_file_read_request_v1, length) == 20,
               "file read request length offset changed");
_Static_assert(offsetof(struct iouringd_file_write_request_v1, submit) == 0,
               "file write request submit offset changed");
_Static_assert(offsetof(struct iouringd_file_write_request_v1, resource) == 16,
               "file write request resource offset changed");
_Static_assert(offsetof(struct iouringd_file_write_request_v1, length) == 20,
               "file write request length offset changed");
_Static_assert(offsetof(struct iouringd_poll_request_v1, submit) == 0,
               "poll request submit offset changed");
_Static_assert(offsetof(struct iouringd_poll_request_v1, resource) == 16,
               "poll request resource offset changed");
_Static_assert(offsetof(struct iouringd_poll_request_v1, poll_mask) == 20,
               "poll request mask offset changed");
_Static_assert(offsetof(struct iouringd_poll_request_v1, reserved2) == 22,
               "poll request reserved2 offset changed");
_Static_assert(offsetof(struct iouringd_connect_request_v1, submit) == 0,
               "connect request submit offset changed");
_Static_assert(offsetof(struct iouringd_connect_request_v1, resource) == 16,
               "connect request resource offset changed");
_Static_assert(offsetof(struct iouringd_connect_request_v1, sockaddr_length) == 20,
               "connect request sockaddr length offset changed");
_Static_assert(offsetof(struct iouringd_accept_request_v1, submit) == 0,
               "accept request submit offset changed");
_Static_assert(offsetof(struct iouringd_accept_request_v1, resource) == 16,
               "accept request resource offset changed");
_Static_assert(offsetof(struct iouringd_accept_request_v1, sockaddr_length) == 20,
               "accept request sockaddr length offset changed");
_Static_assert(offsetof(struct iouringd_openat_request_v1, submit) == 0,
               "openat request submit offset changed");
_Static_assert(offsetof(struct iouringd_openat_request_v1, open_flags) == 16,
               "openat request flags offset changed");
_Static_assert(offsetof(struct iouringd_openat_request_v1, open_mode) == 20,
               "openat request mode offset changed");
_Static_assert(offsetof(struct iouringd_openat_request_v1, path_length) == 24,
               "openat request path length offset changed");
_Static_assert(offsetof(struct iouringd_close_request_v1, submit) == 0,
               "close request submit offset changed");
_Static_assert(offsetof(struct iouringd_close_request_v1, resource) == 16,
               "close request resource offset changed");
_Static_assert(offsetof(struct iouringd_close_request_v1, resource.resource_id) == 16,
               "close request resource id offset changed");
_Static_assert(offsetof(struct iouringd_close_request_v1, reserved2) == 20,
               "close request reserved2 offset changed");

_Static_assert(sizeof(struct iouringd_register_fd_request_v1) == 16,
               "register fd request size changed");
_Static_assert(offsetof(struct iouringd_register_fd_request_v1, control) == 0,
               "register fd request control offset changed");

_Static_assert(sizeof(struct iouringd_release_resource_request_v1) == 24,
               "release resource request size changed");
_Static_assert(offsetof(struct iouringd_release_resource_request_v1, control) == 0,
               "release resource request control offset changed");
_Static_assert(offsetof(struct iouringd_release_resource_request_v1, resource) == 16,
               "release resource request resource offset changed");
_Static_assert(offsetof(struct iouringd_release_resource_request_v1,
                        resource.resource_id) == 16,
               "release resource request resource id offset changed");
_Static_assert(sizeof(struct iouringd_get_stats_request_v1) == 16,
               "get stats request size changed");
_Static_assert(offsetof(struct iouringd_get_stats_request_v1, control) == 0,
               "get stats request control offset changed");
_Static_assert(sizeof(struct iouringd_get_trace_request_v1) == 32,
               "get trace request size changed");
_Static_assert(offsetof(struct iouringd_get_trace_request_v1, control) == 0,
               "get trace request control offset changed");
_Static_assert(offsetof(struct iouringd_get_trace_request_v1, after_sequence) == 16,
               "get trace request sequence offset changed");
_Static_assert(offsetof(struct iouringd_get_trace_request_v1, max_events) == 24,
               "get trace request max events offset changed");
_Static_assert(offsetof(struct iouringd_get_trace_request_v1, reserved2) == 28,
               "get trace request reserved2 offset changed");

_Static_assert(sizeof(struct iouringd_submit_result_v1) == 16,
               "submit result size changed");
_Static_assert(_Alignof(struct iouringd_submit_result_v1) == 8,
               "submit result alignment changed");
_Static_assert(offsetof(struct iouringd_submit_result_v1, task) == 0,
               "submit result task offset changed");
_Static_assert(offsetof(struct iouringd_submit_result_v1, task.task_id) == 0,
               "submit result task id offset changed");
_Static_assert(offsetof(struct iouringd_submit_result_v1, res) == 8,
               "submit result res offset changed");
_Static_assert(offsetof(struct iouringd_submit_result_v1, credits) == 12,
               "submit result credits offset changed");

_Static_assert(sizeof(struct iouringd_resource_result_v1) == 16,
               "resource result size changed");
_Static_assert(offsetof(struct iouringd_resource_result_v1, resource) == 0,
               "resource result resource offset changed");
_Static_assert(offsetof(struct iouringd_resource_result_v1, resource.resource_id) == 0,
               "resource result resource id offset changed");
_Static_assert(offsetof(struct iouringd_resource_result_v1, res) == 4,
               "resource result res offset changed");
_Static_assert(offsetof(struct iouringd_resource_result_v1, resources_available) == 8,
               "resource result available offset changed");
_Static_assert(offsetof(struct iouringd_resource_result_v1, reserved) == 12,
               "resource result reserved offset changed");

_Static_assert(sizeof(struct iouringd_stats_result_v1) == 56,
               "stats result size changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, active_clients) == 0,
               "stats result active clients offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, outstanding_tasks) == 4,
               "stats result outstanding tasks offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, outstanding_credit_tasks) == 8,
               "stats result outstanding credit tasks offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, available_credits) == 12,
               "stats result available credits offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, registered_files) == 16,
               "stats result registered files offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, registered_buffers) == 20,
               "stats result registered buffers offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, accepted_submits) == 24,
               "stats result accepted submits offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, rejected_submits) == 32,
               "stats result rejected submits offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, completions) == 40,
               "stats result completions offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, per_client_credit_limit) == 48,
               "stats result per-client credit limit offset changed");
_Static_assert(offsetof(struct iouringd_stats_result_v1, clients_at_credit_limit) == 52,
               "stats result clients-at-limit offset changed");
_Static_assert(sizeof(struct iouringd_trace_result_v1) == 24,
               "trace result size changed");
_Static_assert(offsetof(struct iouringd_trace_result_v1, oldest_sequence) == 0,
               "trace result oldest sequence offset changed");
_Static_assert(offsetof(struct iouringd_trace_result_v1, latest_sequence) == 8,
               "trace result latest sequence offset changed");
_Static_assert(offsetof(struct iouringd_trace_result_v1, count) == 16,
               "trace result count offset changed");
_Static_assert(offsetof(struct iouringd_trace_result_v1, reserved) == 20,
               "trace result reserved offset changed");
_Static_assert(sizeof(struct iouringd_trace_event_v1) == 48,
               "trace event size changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, sequence) == 0,
               "trace event sequence offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, timestamp_nsec) == 8,
               "trace event timestamp offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, task) == 16,
               "trace event task offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, resource) == 24,
               "trace event resource offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, res) == 28,
               "trace event res offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, credits) == 32,
               "trace event credits offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, event_kind) == 36,
               "trace event kind offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, task_kind) == 38,
               "trace event task kind offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, client_slot) == 40,
               "trace event client slot offset changed");
_Static_assert(offsetof(struct iouringd_trace_event_v1, priority) == 42,
               "trace event priority offset changed");

_Static_assert(sizeof(struct iouringd_completion_record_v1) == 24,
               "completion record size changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, header) == 0,
               "completion record header offset changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, task) == 8,
               "completion record task offset changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, task.task_id) == 8,
               "completion record task id offset changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, res) == 16,
               "completion record res offset changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, task_kind) == 20,
               "completion record task kind offset changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, payload_length) == 22,
               "completion record payload length offset changed");

#endif
