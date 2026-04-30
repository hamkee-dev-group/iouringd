#include <string.h>

#include "iouringd/client.h"

int main(void)
{
    return strcmp(iouringd_client_version(), "0.1.0") == 0 ? 0 : 1;
}
