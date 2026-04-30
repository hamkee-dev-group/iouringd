#ifndef IOURINGD_DAEMON_HANDSHAKE_H
#define IOURINGD_DAEMON_HANDSHAKE_H

#include "iouringd/api.h"

int iouringd_serve_handshake_client(
    int client_fd,
    const struct iouringd_capability_descriptor_v1 *capabilities,
    uint16_t *status);

#endif
