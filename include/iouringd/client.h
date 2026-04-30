#ifndef IOURINGD_CLIENT_H
#define IOURINGD_CLIENT_H

#include "iouringd/api.h"

const char *iouringd_client_version(void);

int iouringd_client_connect(const char *socket_path);
int iouringd_client_handshake_fd(int fd,
                                 struct iouringd_handshake_result_v1 *result);
int iouringd_client_handshake(const char *socket_path,
                              struct iouringd_handshake_result_v1 *result);

#endif
