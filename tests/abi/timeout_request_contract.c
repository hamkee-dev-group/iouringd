#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "iouringd/api.h"
#include "iouringd/protocol.h"

_Static_assert(IOURINGD_CAPABILITY_OP_TIMEOUT == (UINT32_C(1) << 1),
               "timeout capability bit changed");
_Static_assert(sizeof(struct iouringd_timeout_request_v1) == 24,
               "timeout request size changed");
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

int main(void)
{
    assert(IOURINGD_CAPABILITY_OP_TIMEOUT == (UINT32_C(1) << 1));
    assert(sizeof(struct iouringd_timeout_request_v1) == 24);
    assert(_Alignof(struct iouringd_timeout_request_v1) == _Alignof(uint64_t));
    assert(offsetof(struct iouringd_timeout_request_v1, submit) == 0);
    assert(offsetof(struct iouringd_timeout_request_v1, submit.header) == 0);
    assert(offsetof(struct iouringd_timeout_request_v1, submit.task_kind) == 8);
    assert(offsetof(struct iouringd_timeout_request_v1, timeout_nsec) == 16);

    return 0;
}
