#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "iouringd/api.h"

_Static_assert(IOURINGD_COMPLETION_RES_OK == 0, "completion ok value changed");
_Static_assert((int32_t)-1 < 0, "completion res must be signed");
_Static_assert(sizeof(struct iouringd_completion_record_v1) == 24,
               "completion record size changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, task.task_id) == 8,
               "completion task id offset changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, res) == 16,
               "completion res offset changed");
_Static_assert(offsetof(struct iouringd_completion_record_v1, task_kind) == 20,
               "completion task kind offset changed");

int main(void)
{
    assert(IOURINGD_COMPLETION_RES_OK == 0);
    assert(IOURINGD_COMPLETION_RES_IS_ERROR(-1) != 0);
    assert(IOURINGD_COMPLETION_RES_IS_ERROR(0) == 0);
    assert(IOURINGD_COMPLETION_RES_IS_ERROR(1) == 0);
    assert(offsetof(struct iouringd_completion_record_v1, res) == 16);
    assert(offsetof(struct iouringd_completion_record_v1, task_kind) == 20);

    return 0;
}
