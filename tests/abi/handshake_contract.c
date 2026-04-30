#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "iouringd/api.h"
#include "iouringd/protocol.h"

_Static_assert(IOURINGD_ABI_V1_MAJOR == 1, "ABI major changed");
_Static_assert(IOURINGD_ABI_V1_MINOR == 12, "ABI minor changed");
_Static_assert(IOURINGD_PROTOCOL_V1_MAJOR == 1, "protocol major changed");
_Static_assert(IOURINGD_PROTOCOL_V1_MINOR == 12, "protocol minor changed");
_Static_assert(IOURINGD_PROTOCOL_ENDIAN_LITTLE == 1,
               "wire endianness declaration changed");
_Static_assert(IOURINGD_PROTOCOL_PACKING_NONE == 1,
               "wire packing declaration changed");
_Static_assert(IOURINGD_PROTOCOL_WIRE_MAGIC == UINT32_C(0x31444f49),
               "wire magic changed");
_Static_assert(sizeof(iouringd_task_id_t) == sizeof(uint64_t),
               "task id width changed");
_Static_assert((iouringd_task_id_t)-1 > 0, "task id signedness changed");
_Static_assert(_Generic((iouringd_task_id_t)0, uint64_t: 1, default: 0),
               "task id representation changed");
_Static_assert(IOURINGD_TASK_ID_INVALID == UINT64_C(0),
               "task id invalid value changed");
_Static_assert(IOURINGD_TASK_ID_RESERVED_MIN == (UINT64_MAX - UINT64_C(15)),
               "task id reserved minimum changed");
_Static_assert(IOURINGD_TASK_ID_RESERVED_MAX == UINT64_MAX,
               "task id reserved maximum changed");
_Static_assert(sizeof(struct iouringd_task_id_record_v1) == 8,
               "task id record size changed");
_Static_assert(_Alignof(struct iouringd_task_id_record_v1) == _Alignof(uint64_t),
               "task id record alignment changed");
_Static_assert(offsetof(struct iouringd_task_id_record_v1, task_id) == 0,
               "task id record offset changed");

_Static_assert(sizeof(struct iouringd_protocol_header_v1) == 8,
               "protocol header size changed");
_Static_assert(sizeof(struct iouringd_handshake_request_v1) == 16,
               "handshake request size changed");
_Static_assert(sizeof(struct iouringd_handshake_response_v1) == 16,
               "handshake response size changed");
_Static_assert(sizeof(struct iouringd_capability_descriptor_v1) == 36,
               "capability descriptor size changed");
_Static_assert(IOURINGD_CAPABILITY_DESCRIPTOR_V1_STRUCT_SIZE ==
                   sizeof(struct iouringd_capability_descriptor_v1),
               "capability descriptor struct_size value changed");
_Static_assert(IOURINGD_CAPABILITY_OP_NOP == UINT32_C(1),
               "nop capability bit changed");
_Static_assert(IOURINGD_CAPABILITY_OP_READV == (UINT32_C(1) << 2),
               "readv capability bit changed");
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
_Static_assert(IOURINGD_SUBMIT_PRIORITY_NORMAL == UINT16_C(0),
               "submit priority normal changed");
_Static_assert(IOURINGD_SUBMIT_PRIORITY_LOW == UINT16_C(1),
               "submit priority low changed");
_Static_assert(IOURINGD_SUBMIT_PRIORITY_HIGH == UINT16_C(2),
               "submit priority high changed");
_Static_assert(sizeof(struct iouringd_timeout_request_v1) == 24,
               "timeout request size changed");
_Static_assert(sizeof(struct iouringd_cancel_request_v1) == 24,
               "cancel request size changed");
_Static_assert(sizeof(struct iouringd_sock_read_request_v1) == 24,
               "sock read request size changed");
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
_Static_assert(sizeof(struct iouringd_get_trace_request_v1) == 32,
               "get trace request size changed");
_Static_assert(sizeof(struct iouringd_resource_result_v1) == 16,
               "resource result size changed");
_Static_assert(sizeof(struct iouringd_stats_result_v1) == 56,
               "stats result size changed");
_Static_assert(sizeof(struct iouringd_trace_result_v1) == 24,
               "trace result size changed");
_Static_assert(sizeof(struct iouringd_trace_event_v1) == 48,
               "trace event size changed");

_Static_assert(offsetof(struct iouringd_protocol_header_v1, version_major) == 4,
               "protocol header major offset changed");
_Static_assert(offsetof(struct iouringd_handshake_request_v1, abi_major) == 8,
               "handshake request abi major offset changed");
_Static_assert(offsetof(struct iouringd_handshake_response_v1, status) == 12,
               "handshake response status offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, struct_size) == 0,
               "capability descriptor size offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, submit_credits) == 20,
               "capability descriptor credits offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, registered_fd_slots) == 24,
               "capability descriptor fd slots offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, registered_buffer_slots) == 28,
               "capability descriptor buffer slots offset changed");
_Static_assert(offsetof(struct iouringd_capability_descriptor_v1, io_bytes_max) == 32,
               "capability descriptor io bytes offset changed");
_Static_assert(sizeof(struct iouringd_submit_result_v1) == 16,
               "submit result size changed");
_Static_assert(offsetof(struct iouringd_submit_result_v1, credits) == 12,
               "submit result credits offset changed");
_Static_assert(offsetof(struct iouringd_submit_request_v1, priority) == 10,
               "submit request priority offset changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, payload_length) == 22,
               "completion payload offset changed");
_Static_assert(_Alignof(struct iouringd_timeout_request_v1) == _Alignof(uint64_t),
               "timeout request alignment changed");
_Static_assert(offsetof(struct iouringd_timeout_request_v1, submit) == 0,
               "timeout request submit offset changed");
_Static_assert(offsetof(struct iouringd_timeout_request_v1, submit.header) == 0,
               "timeout request header offset changed");
_Static_assert(offsetof(struct iouringd_timeout_request_v1, submit.task_kind) == 8,
               "timeout request task kind offset changed");
_Static_assert(offsetof(struct iouringd_timeout_request_v1, timeout_nsec) == 16,
               "timeout request timeout offset changed");
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

int main(void)
{
    assert(IOURINGD_CAPABILITY_DESCRIPTOR_V1_STRUCT_SIZE ==
           sizeof(struct iouringd_capability_descriptor_v1));
    assert(IOURINGD_CAPABILITY_OP_NOP == UINT32_C(1));
    assert(IOURINGD_CAPABILITY_OP_READV == (UINT32_C(1) << 2));
    assert(IOURINGD_CAPABILITY_OP_READ_FIXED == (UINT32_C(1) << 5));
    assert(IOURINGD_CAPABILITY_OP_WRITE_FIXED == (UINT32_C(1) << 6));
    assert(IOURINGD_CAPABILITY_OP_POLL == (UINT32_C(1) << 7));
    assert(IOURINGD_CAPABILITY_OP_CONNECT == (UINT32_C(1) << 8));
    assert(IOURINGD_CAPABILITY_OP_ACCEPT == (UINT32_C(1) << 9));
    assert(IOURINGD_CAPABILITY_OP_OPENAT == (UINT32_C(1) << 10));
    assert(IOURINGD_CAPABILITY_OP_CLOSE == (UINT32_C(1) << 11));
    assert(IOURINGD_CAPABILITY_OP_MASK_V1 == UINT32_C(0xfff));
    assert(IOURINGD_SUBMIT_PRIORITY_NORMAL == UINT16_C(0));
    assert(IOURINGD_SUBMIT_PRIORITY_LOW == UINT16_C(1));
    assert(IOURINGD_SUBMIT_PRIORITY_HIGH == UINT16_C(2));
    assert(IOURINGD_REQUEST_KIND_GET_TRACE == UINT16_C(260));
    assert(offsetof(struct iouringd_capability_descriptor_v1, submit_credits) == 20);
    assert(offsetof(struct iouringd_capability_descriptor_v1, registered_fd_slots) == 24);
    assert(offsetof(struct iouringd_capability_descriptor_v1, registered_buffer_slots) == 28);
    assert(offsetof(struct iouringd_capability_descriptor_v1, io_bytes_max) == 32);
    assert(sizeof(struct iouringd_submit_result_v1) == 16);
    assert(offsetof(struct iouringd_submit_result_v1, credits) == 12);
    assert(offsetof(struct iouringd_submit_request_v1, priority) == 10);
    assert(sizeof(struct iouringd_resource_result_v1) == 16);
    assert(sizeof(struct iouringd_stats_result_v1) == 56);
    assert(sizeof(struct iouringd_trace_result_v1) == 24);
    assert(sizeof(struct iouringd_trace_event_v1) == 48);
    return 0;
}
