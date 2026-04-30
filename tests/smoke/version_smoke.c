#include <stdio.h>
#include <string.h>

#include "iouringd/version.h"

int main(void)
{
    const char *version = iouringd_version_string();

    if (IOURINGD_VERSION_MAJOR != 0) {
        return 1;
    }

    if (IOURINGD_VERSION_MINOR != 1) {
        return 1;
    }

    if (IOURINGD_VERSION_PATCH != 0) {
        return 1;
    }

    if (IOURINGD_PROTOCOL_VERSION != 1) {
        return 1;
    }

    if (strcmp(version, IOURINGD_VERSION_STRING) != 0) {
        return 1;
    }

    printf("%s\n", version);
    return 0;
}
