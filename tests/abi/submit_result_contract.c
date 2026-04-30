#include <assert.h>
#include <stddef.h>

#include "iouringd/api.h"

_Static_assert(sizeof(struct iouringd_submit_result_v1) == 16,
               "submit result size changed");
_Static_assert(_Alignof(struct iouringd_submit_result_v1) == 8,
               "submit result alignment changed");
_Static_assert(offsetof(struct iouringd_submit_result_v1, task) == 0,
               "submit result task offset changed");
_Static_assert(offsetof(struct iouringd_submit_result_v1, res) == 8,
               "submit result res offset changed");
_Static_assert(offsetof(struct iouringd_submit_result_v1, credits) == 12,
               "submit result credits offset changed");

int main(void)
{
    assert(sizeof(struct iouringd_submit_result_v1) == 16);
    assert(_Alignof(struct iouringd_submit_result_v1) == 8);
    assert(offsetof(struct iouringd_submit_result_v1, task) == 0);
    assert(offsetof(struct iouringd_submit_result_v1, res) == 8);
    assert(offsetof(struct iouringd_submit_result_v1, credits) == 12);
    return 0;
}
