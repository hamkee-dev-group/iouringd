#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "iouringd/api.h"

_Static_assert(IOURINGD_CAPABILITY_OP_ASYNC_CANCEL == (UINT32_C(1) << 4),
               "async cancel capability bit changed");
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

int main(void)
{
    assert(IOURINGD_CAPABILITY_OP_ASYNC_CANCEL == (UINT32_C(1) << 4));
    assert(sizeof(struct iouringd_cancel_request_v1) == 24);
    assert(_Alignof(struct iouringd_cancel_request_v1) == 8);
    assert(offsetof(struct iouringd_cancel_request_v1, header) == 0);
    assert(offsetof(struct iouringd_cancel_request_v1, task_kind) == 8);
    assert(offsetof(struct iouringd_cancel_request_v1, target) == 16);
    assert(offsetof(struct iouringd_cancel_request_v1, target.task_id) == 16);

    return 0;
}
