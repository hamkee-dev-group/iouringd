#include <assert.h>
#include <stdint.h>

#include "iouringd/protocol.h"

int main(void)
{
    assert(sizeof(iouringd_task_id_t) == 8);
    assert(IOURINGD_TASK_ID_INVALID == (iouringd_task_id_t)0);
    assert(IOURINGD_TASK_KIND_NOP == 1);
    assert(IOURINGD_TASK_KIND_TIMEOUT == 2);
    assert(IOURINGD_TASK_KIND_CANCEL == 3);
    assert(IOURINGD_TASK_KIND_SOCK_READ == 4);
    assert(IOURINGD_TASK_KIND_SOCK_WRITE == 5);
    assert(IOURINGD_TASK_KIND_SOCK_READ_FIXED == 6);
    assert(IOURINGD_TASK_KIND_SOCK_WRITE_FIXED == 7);
    assert(IOURINGD_TASK_KIND_FILE_READ == 8);
    assert(IOURINGD_TASK_KIND_FILE_WRITE == 9);
    assert(IOURINGD_TASK_KIND_POLL == 10);
    assert(IOURINGD_TASK_KIND_CONNECT == 11);
    assert(IOURINGD_TASK_KIND_ACCEPT == 12);
    assert(IOURINGD_TASK_KIND_OPENAT == 13);
    assert(IOURINGD_TASK_KIND_CLOSE == 14);
    assert(IOURINGD_TASK_KIND_MAX_V1 == 15);

    return 0;
}
