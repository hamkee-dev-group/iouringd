#ifndef IOURINGD_PROTOCOL_H
#define IOURINGD_PROTOCOL_H

#include <stdint.h>

typedef uint64_t iouringd_task_id_t;
typedef uint32_t iouringd_resource_id_t;

#define IOURINGD_TASK_ID_INVALID ((iouringd_task_id_t)0)
#define IOURINGD_RESOURCE_ID_INVALID ((iouringd_resource_id_t)0)

enum iouringd_task_kind {
    IOURINGD_TASK_KIND_NOP = 1,
    IOURINGD_TASK_KIND_TIMEOUT = 2,
    IOURINGD_TASK_KIND_CANCEL = 3,
    IOURINGD_TASK_KIND_SOCK_READ = 4,
    IOURINGD_TASK_KIND_SOCK_WRITE = 5,
    IOURINGD_TASK_KIND_SOCK_READ_FIXED = 6,
    IOURINGD_TASK_KIND_SOCK_WRITE_FIXED = 7,
    IOURINGD_TASK_KIND_FILE_READ = 8,
    IOURINGD_TASK_KIND_FILE_WRITE = 9,
    IOURINGD_TASK_KIND_POLL = 10,
    IOURINGD_TASK_KIND_CONNECT = 11,
    IOURINGD_TASK_KIND_ACCEPT = 12,
    IOURINGD_TASK_KIND_OPENAT = 13,
    IOURINGD_TASK_KIND_CLOSE = 14,
    IOURINGD_TASK_KIND_MAX_V1 = 15
};

typedef struct iouringd_task_kind_wire_v1 {
    uint16_t value;
} iouringd_task_kind_wire_v1;

_Static_assert(sizeof(iouringd_task_id_t) == 8, "task id size changed");
_Static_assert(sizeof(iouringd_resource_id_t) == 4, "resource id size changed");
_Static_assert(sizeof(iouringd_task_kind_wire_v1) == sizeof(uint16_t),
               "task kind wire size changed");
_Static_assert(IOURINGD_TASK_KIND_NOP == 1, "task kind nop value changed");
_Static_assert(IOURINGD_TASK_KIND_TIMEOUT == 2,
               "task kind timeout value changed");
_Static_assert(IOURINGD_TASK_KIND_CANCEL == 3,
               "task kind cancel value changed");
_Static_assert(IOURINGD_TASK_KIND_SOCK_READ == 4,
               "task kind sock read value changed");
_Static_assert(IOURINGD_TASK_KIND_SOCK_WRITE == 5,
               "task kind sock write value changed");
_Static_assert(IOURINGD_TASK_KIND_SOCK_READ_FIXED == 6,
               "task kind sock read fixed value changed");
_Static_assert(IOURINGD_TASK_KIND_SOCK_WRITE_FIXED == 7,
               "task kind sock write fixed value changed");
_Static_assert(IOURINGD_TASK_KIND_FILE_READ == 8,
               "task kind file read value changed");
_Static_assert(IOURINGD_TASK_KIND_FILE_WRITE == 9,
               "task kind file write value changed");
_Static_assert(IOURINGD_TASK_KIND_POLL == 10,
               "task kind poll value changed");
_Static_assert(IOURINGD_TASK_KIND_CONNECT == 11,
               "task kind connect value changed");
_Static_assert(IOURINGD_TASK_KIND_ACCEPT == 12,
               "task kind accept value changed");
_Static_assert(IOURINGD_TASK_KIND_OPENAT == 13,
               "task kind openat value changed");
_Static_assert(IOURINGD_TASK_KIND_CLOSE == 14,
               "task kind close value changed");
_Static_assert(IOURINGD_TASK_KIND_MAX_V1 == 15,
               "task kind max v1 value changed");

#endif
