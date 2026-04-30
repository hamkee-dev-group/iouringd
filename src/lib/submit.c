#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "iouringd/submit.h"

struct buffered_completion {
    int fd;
    struct iouringd_completion_record_v1 completion;
    size_t payload_length;
    unsigned char *payload;
    struct buffered_completion *next;
};

struct buffered_payload {
    int fd;
    iouringd_task_id_t task_id;
    size_t payload_length;
    unsigned char *payload;
    struct buffered_payload *next;
};

static struct buffered_completion *buffered_completions;
static struct buffered_payload *buffered_payloads;

static int socket_send_flags(void)
{
    return
#ifdef MSG_NOSIGNAL
        MSG_NOSIGNAL
#else
        0
#endif
        ;
}

static int write_full(int fd, const void *buf, size_t len)
{
    size_t offset = 0;
    const char *bytes = buf;

    while (offset < len) {
        ssize_t rc = send(fd,
                          bytes + offset,
                          len - offset,
                          socket_send_flags());

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        if (rc < 0) {
            return -1;
        }

        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

static int write_iov_full(int fd,
                          const void *first,
                          size_t first_len,
                          const void *second,
                          size_t second_len)
{
    size_t first_offset = 0;
    size_t second_offset = 0;

    while (first_offset < first_len || second_offset < second_len) {
        struct iovec iov[2];
        struct msghdr msg;
        int iovcnt = 0;
        ssize_t rc;

        if (first_offset < first_len) {
            iov[iovcnt].iov_base = (char *)first + first_offset;
            iov[iovcnt].iov_len = first_len - first_offset;
            iovcnt += 1;
        }

        if (second_offset < second_len) {
            iov[iovcnt].iov_base = (char *)second + second_offset;
            iov[iovcnt].iov_len = second_len - second_offset;
            iovcnt += 1;
        }

        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov;
        msg.msg_iovlen = (size_t)iovcnt;

        rc = sendmsg(fd, &msg, socket_send_flags());
        if (rc < 0 && errno == EINTR) {
            continue;
        }

        if (rc < 0) {
            return -1;
        }

        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }

        if ((size_t)rc <= first_len - first_offset) {
            first_offset += (size_t)rc;
        } else {
            size_t first_remaining = first_len - first_offset;

            first_offset = first_len;
            second_offset += (size_t)rc - first_remaining;
        }
    }

    return 0;
}

static int send_fd_with_request(int fd, int resource_fd, const void *buf, size_t len)
{
    char control[CMSG_SPACE(sizeof(int))];
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    ssize_t rc;

    memset(control, 0, sizeof(control));
    memset(&msg, 0, sizeof(msg));

    iov.iov_base = (void *)buf;
    iov.iov_len = len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &resource_fd, sizeof(resource_fd));

    for (;;) {
        rc = sendmsg(fd, &msg, socket_send_flags());
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    if (rc < 0) {
        return -1;
    }

    if (rc == 0) {
        errno = EPIPE;
        return -1;
    }

    if ((size_t)rc != len) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

static int read_full(int fd, void *buf, size_t len)
{
    size_t offset = 0;
    char *bytes = buf;

    while (offset < len) {
        ssize_t rc = recv(fd, bytes + offset, len - offset, 0);

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        if (rc < 0) {
            return -1;
        }

        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }

        offset += (size_t)rc;
    }

    return 0;
}

static int task_id_is_invalid(iouringd_task_id_t task_id)
{
    return task_id == IOURINGD_TASK_ID_INVALID ||
           task_id >= IOURINGD_TASK_ID_RESERVED_MIN;
}

static int resource_id_is_invalid(iouringd_resource_id_t resource_id)
{
    return resource_id == IOURINGD_RESOURCE_ID_INVALID;
}

int iouringd_submit_priority_is_valid(uint16_t priority)
{
    return priority == IOURINGD_SUBMIT_PRIORITY_NORMAL ||
           priority == IOURINGD_SUBMIT_PRIORITY_LOW ||
           priority == IOURINGD_SUBMIT_PRIORITY_HIGH;
}

int iouringd_submit_request_set_priority(struct iouringd_submit_request_v1 *request,
                                         uint16_t priority)
{
    if (request == NULL || iouringd_submit_priority_is_valid(priority) == 0) {
        errno = EINVAL;
        return -1;
    }

    request->priority = priority;
    return 0;
}

uint16_t iouringd_submit_request_priority(
    const struct iouringd_submit_request_v1 *request)
{
    if (request == NULL) {
        return IOURINGD_SUBMIT_PRIORITY_NORMAL;
    }

    return request->priority;
}

static int completion_record_is_valid(
    const struct iouringd_completion_record_v1 *completion)
{
    if (completion->header.magic != IOURINGD_PROTOCOL_WIRE_MAGIC ||
        completion->header.version_major != IOURINGD_PROTOCOL_V1_MAJOR ||
        completion->header.version_minor != IOURINGD_PROTOCOL_V1_MINOR ||
        task_id_is_invalid(completion->task.task_id) ||
        completion->task_kind.value < IOURINGD_TASK_KIND_NOP ||
        completion->task_kind.value >= IOURINGD_TASK_KIND_MAX_V1) {
        return 0;
    }

    if (completion->task_kind.value == IOURINGD_TASK_KIND_SOCK_READ ||
        completion->task_kind.value == IOURINGD_TASK_KIND_SOCK_READ_FIXED ||
        completion->task_kind.value == IOURINGD_TASK_KIND_FILE_READ) {
        if (completion->res < 0) {
            return completion->payload_length == 0u;
        }

        return completion->res <= UINT16_MAX &&
               completion->payload_length == (uint16_t)completion->res;
    }

    if (completion->task_kind.value == IOURINGD_TASK_KIND_ACCEPT) {
        if (completion->res < 0) {
            return completion->payload_length == 0u;
        }

        return completion->res > 0;
    }

    return completion->payload_length == 0u;
}

static int peek_protocol_header(int fd,
                                struct iouringd_protocol_header_v1 *header)
{
    for (;;) {
        ssize_t rc = recv(fd, header, sizeof(*header), MSG_PEEK | MSG_WAITALL);

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        if (rc < 0) {
            return -1;
        }

        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }

        if ((size_t)rc == sizeof(*header)) {
            return 0;
        }

        errno = EPROTO;
        return -1;
    }
}

static int store_buffered_payload_owned(int fd,
                                        iouringd_task_id_t task_id,
                                        unsigned char *payload,
                                        size_t payload_length)
{
    struct buffered_payload *entry;

    entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        free(payload);
        errno = ENOMEM;
        return -1;
    }

    entry->fd = fd;
    entry->task_id = task_id;
    entry->payload_length = payload_length;
    entry->payload = payload;
    entry->next = NULL;

    if (buffered_payloads == NULL) {
        buffered_payloads = entry;
        return 0;
    }

    {
        struct buffered_payload *tail = buffered_payloads;

        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = entry;
    }

    return 0;
}

static int queue_buffered_completion(
    int fd,
    const struct iouringd_completion_record_v1 *completion,
    unsigned char *payload,
    size_t payload_length)
{
    struct buffered_completion *entry;

    entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        free(payload);
        errno = ENOMEM;
        return -1;
    }

    entry->fd = fd;
    entry->completion = *completion;
    entry->payload_length = payload_length;
    entry->payload = payload;
    entry->next = NULL;

    if (buffered_completions == NULL) {
        buffered_completions = entry;
        return 0;
    }

    {
        struct buffered_completion *tail = buffered_completions;

        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = entry;
    }

    return 0;
}

static int pop_buffered_completion(int fd,
                                   struct iouringd_completion_record_v1 *completion)
{
    struct buffered_completion *current = buffered_completions;
    struct buffered_completion *previous = NULL;

    while (current != NULL) {
        if (current->fd == fd) {
            if (previous == NULL) {
                buffered_completions = current->next;
            } else {
                previous->next = current->next;
            }

            *completion = current->completion;
            if (current->payload_length != 0u &&
                store_buffered_payload_owned(fd,
                                             current->completion.task.task_id,
                                             current->payload,
                                             current->payload_length) != 0) {
                free(current);
                return -1;
            }

            if (current->payload_length == 0u) {
                free(current->payload);
            }
            free(current);
            return 1;
        }

        previous = current;
        current = current->next;
    }

    return 0;
}

static int read_completion_with_payload(int fd,
                                        struct iouringd_completion_record_v1 *completion,
                                        unsigned char **payload,
                                        size_t *payload_length)
{
    *payload = NULL;
    *payload_length = 0u;

    if (read_full(fd, completion, sizeof(*completion)) != 0) {
        return -1;
    }

    if (completion_record_is_valid(completion) == 0) {
        errno = EPROTO;
        return -1;
    }

    if (completion->payload_length == 0u) {
        return 0;
    }

    *payload = malloc(completion->payload_length);
    if (*payload == NULL) {
        errno = ENOMEM;
        return -1;
    }

    if (read_full(fd, *payload, completion->payload_length) != 0) {
        free(*payload);
        *payload = NULL;
        return -1;
    }

    *payload_length = completion->payload_length;
    return 0;
}

static int drain_completion_prefix(int fd)
{
    for (;;) {
        struct iouringd_protocol_header_v1 header;
        struct iouringd_completion_record_v1 completion;
        unsigned char *payload;
        size_t payload_length;

        if (peek_protocol_header(fd, &header) != 0) {
            return -1;
        }

        if (header.magic != IOURINGD_PROTOCOL_WIRE_MAGIC ||
            header.version_major != IOURINGD_PROTOCOL_V1_MAJOR ||
            header.version_minor != IOURINGD_PROTOCOL_V1_MINOR) {
            return 0;
        }

        if (read_completion_with_payload(fd,
                                         &completion,
                                         &payload,
                                         &payload_length) != 0) {
            return -1;
        }

        if (queue_buffered_completion(fd,
                                      &completion,
                                      payload,
                                      payload_length) != 0) {
            return -1;
        }
    }
}

static int submit_result_is_rejection(
    const struct iouringd_submit_result_v1 *result)
{
    return result->task.task_id == IOURINGD_TASK_ID_INVALID &&
           IOURINGD_COMPLETION_RES_IS_ERROR(result->res);
}

static int submit_result_is_valid(const struct iouringd_submit_result_v1 *result)
{
    if (submit_result_is_rejection(result) != 0) {
        return 1;
    }

    return result->res == IOURINGD_COMPLETION_RES_OK &&
           !task_id_is_invalid(result->task.task_id);
}

static int resource_result_is_rejection(
    const struct iouringd_resource_result_v1 *result)
{
    return result->resource.resource_id == IOURINGD_RESOURCE_ID_INVALID &&
           IOURINGD_COMPLETION_RES_IS_ERROR(result->res) &&
           result->reserved == 0u;
}

static int resource_result_is_valid(const struct iouringd_resource_result_v1 *result)
{
    if (resource_result_is_rejection(result) != 0) {
        return 1;
    }

    return result->reserved == 0u &&
           result->res == IOURINGD_COMPLETION_RES_OK &&
           !resource_id_is_invalid(result->resource.resource_id);
}

static int read_submit_result(int fd, struct iouringd_submit_result_v1 *result)
{
    if (drain_completion_prefix(fd) != 0) {
        return -1;
    }

    if (read_full(fd, result, sizeof(*result)) != 0) {
        return -1;
    }

    if (submit_result_is_valid(result) == 0) {
        errno = EPROTO;
        return -1;
    }

    if (submit_result_is_rejection(result) != 0) {
        errno = -result->res;
        return -1;
    }

    return 0;
}

static int read_resource_result(int fd, struct iouringd_resource_result_v1 *result)
{
    if (drain_completion_prefix(fd) != 0) {
        return -1;
    }

    if (read_full(fd, result, sizeof(*result)) != 0) {
        return -1;
    }

    if (resource_result_is_valid(result) == 0) {
        errno = EPROTO;
        return -1;
    }

    if (resource_result_is_rejection(result) != 0) {
        errno = -result->res;
        return -1;
    }

    return 0;
}

static int read_stats_result(int fd, struct iouringd_stats_result_v1 *result)
{
    if (drain_completion_prefix(fd) != 0) {
        return -1;
    }

    if (read_full(fd, result, sizeof(*result)) != 0) {
        return -1;
    }

    return 0;
}

static int trace_event_is_valid(const struct iouringd_trace_event_v1 *event)
{
    if (event->event_kind < IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT ||
        event->event_kind > IOURINGD_TRACE_EVENT_RESOURCE_RELEASE) {
        return 0;
    }

    if (event->task_kind.value >= IOURINGD_TASK_KIND_MAX_V1) {
        return 0;
    }

    return iouringd_submit_priority_is_valid(event->priority) != 0 &&
           event->reserved1 == 0u;
}

static int read_trace_result(int fd,
                             struct iouringd_trace_result_v1 *result,
                             struct iouringd_trace_event_v1 *events,
                             size_t event_capacity,
                             size_t *event_count)
{
    uint32_t index;

    if (result == NULL || event_count == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (drain_completion_prefix(fd) != 0) {
        return -1;
    }

    if (read_full(fd, result, sizeof(*result)) != 0) {
        return -1;
    }

    if (result->reserved != 0u) {
        errno = EPROTO;
        return -1;
    }

    if ((result->count != 0u && events == NULL) || result->count > event_capacity) {
        errno = EOVERFLOW;
        return -1;
    }

    for (index = 0; index < result->count; ++index) {
        if (read_full(fd, &events[index], sizeof(events[index])) != 0) {
            return -1;
        }

        if (trace_event_is_valid(&events[index]) == 0) {
            errno = EPROTO;
            return -1;
        }
    }

    *event_count = result->count;
    return 0;
}

static void init_submit_header(struct iouringd_submit_request_v1 *request,
                               uint16_t task_kind)
{
    memset(request, 0, sizeof(*request));
    request->header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    request->header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    request->header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    request->task_kind.value = task_kind;
}

static void init_control_header(struct iouringd_control_request_v1 *request,
                                uint16_t request_kind)
{
    memset(request, 0, sizeof(*request));
    request->header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    request->header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    request->header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    request->request_kind = request_kind;
}

int iouringd_submit_nop_result(int fd,
                               struct iouringd_submit_result_v1 *result)
{
    struct iouringd_submit_request_v1 request;

    if (fd < 0 || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    init_submit_header(&request, IOURINGD_TASK_KIND_NOP);
    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_timeout_result(int fd,
                                   uint64_t timeout_nsec,
                                   struct iouringd_submit_result_v1 *result)
{
    struct iouringd_timeout_request_v1 request;

    if (fd < 0 || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_TIMEOUT);
    request.timeout_nsec = timeout_nsec;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_cancel_result(int fd,
                                  iouringd_task_id_t target_task_id,
                                  struct iouringd_submit_result_v1 *result)
{
    struct iouringd_cancel_request_v1 request;

    if (fd < 0 || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    request.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    request.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    request.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    request.task_kind.value = IOURINGD_TASK_KIND_CANCEL;
    request.target.task_id = target_task_id;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_sock_read_result(int fd,
                                     iouringd_resource_id_t resource_id,
                                     uint32_t length,
                                     struct iouringd_submit_result_v1 *result)
{
    struct iouringd_sock_read_request_v1 request;

    if (fd < 0 || result == NULL || resource_id_is_invalid(resource_id) != 0 ||
        length == 0u) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_SOCK_READ);
    request.resource.resource_id = resource_id;
    request.length = length;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_sock_write_result(int fd,
                                      iouringd_resource_id_t resource_id,
                                      const void *buf,
                                      uint32_t length,
                                      struct iouringd_submit_result_v1 *result)
{
    struct iouringd_sock_write_request_v1 request;

    if (fd < 0 || result == NULL || buf == NULL ||
        resource_id_is_invalid(resource_id) != 0 || length == 0u) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_SOCK_WRITE);
    request.resource.resource_id = resource_id;
    request.length = length;

    if (write_iov_full(fd, &request, sizeof(request), buf, length) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_file_read_result(int fd,
                                     iouringd_resource_id_t resource_id,
                                     uint32_t length,
                                     struct iouringd_submit_result_v1 *result)
{
    struct iouringd_file_read_request_v1 request;

    if (fd < 0 || result == NULL || resource_id_is_invalid(resource_id) != 0 ||
        length == 0u) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_FILE_READ);
    request.resource.resource_id = resource_id;
    request.length = length;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_file_write_result(int fd,
                                      iouringd_resource_id_t resource_id,
                                      const void *buf,
                                      uint32_t length,
                                      struct iouringd_submit_result_v1 *result)
{
    struct iouringd_file_write_request_v1 request;

    if (fd < 0 || result == NULL || buf == NULL ||
        resource_id_is_invalid(resource_id) != 0 || length == 0u) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_FILE_WRITE);
    request.resource.resource_id = resource_id;
    request.length = length;

    if (write_iov_full(fd, &request, sizeof(request), buf, length) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_poll_result(int fd,
                                iouringd_resource_id_t resource_id,
                                uint16_t poll_mask,
                                struct iouringd_submit_result_v1 *result)
{
    struct iouringd_poll_request_v1 request;

    if (fd < 0 || result == NULL || resource_id_is_invalid(resource_id) != 0 ||
        poll_mask == 0u) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_POLL);
    request.resource.resource_id = resource_id;
    request.poll_mask = poll_mask;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_connect_result(int fd,
                                   iouringd_resource_id_t resource_id,
                                   const void *sockaddr_buf,
                                   uint32_t sockaddr_length,
                                   struct iouringd_submit_result_v1 *result)
{
    struct iouringd_connect_request_v1 request;

    if (fd < 0 || result == NULL || sockaddr_buf == NULL ||
        resource_id_is_invalid(resource_id) != 0 || sockaddr_length == 0u ||
        sockaddr_length > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_CONNECT);
    request.resource.resource_id = resource_id;
    request.sockaddr_length = sockaddr_length;

    if (write_iov_full(fd,
                       &request,
                       sizeof(request),
                       sockaddr_buf,
                       sockaddr_length) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_accept_result(int fd,
                                  iouringd_resource_id_t resource_id,
                                  uint32_t sockaddr_length,
                                  struct iouringd_submit_result_v1 *result)
{
    struct iouringd_accept_request_v1 request;

    if (fd < 0 || result == NULL || resource_id_is_invalid(resource_id) != 0 ||
        sockaddr_length > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_ACCEPT);
    request.resource.resource_id = resource_id;
    request.sockaddr_length = sockaddr_length;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_openat_result(int fd,
                                  const char *path,
                                  int32_t open_flags,
                                  uint32_t open_mode,
                                  struct iouringd_submit_result_v1 *result)
{
    struct iouringd_openat_request_v1 request;
    size_t path_length;

    if (fd < 0 || path == NULL || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    path_length = strlen(path) + 1u;
    if (path_length <= 1u || path_length > UINT32_C(4096)) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_OPENAT);
    request.open_flags = open_flags;
    request.open_mode = open_mode;
    request.path_length = (uint32_t)path_length;

    if (write_iov_full(fd, &request, sizeof(request), path, path_length) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_close_result(int fd,
                                 iouringd_resource_id_t resource_id,
                                 struct iouringd_submit_result_v1 *result)
{
    struct iouringd_close_request_v1 request;

    if (fd < 0 || result == NULL || resource_id_is_invalid(resource_id) != 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_CLOSE);
    request.resource.resource_id = resource_id;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_sock_read_fixed_result(
    int fd,
    iouringd_resource_id_t file_resource_id,
    iouringd_resource_id_t buffer_resource_id,
    uint32_t length,
    struct iouringd_submit_result_v1 *result)
{
    struct iouringd_sock_read_fixed_request_v1 request;

    if (fd < 0 || result == NULL ||
        resource_id_is_invalid(file_resource_id) != 0 ||
        resource_id_is_invalid(buffer_resource_id) != 0 || length == 0u) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_SOCK_READ_FIXED);
    request.file_resource.resource_id = file_resource_id;
    request.buffer_resource.resource_id = buffer_resource_id;
    request.length = length;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_submit_sock_write_fixed_result(
    int fd,
    iouringd_resource_id_t file_resource_id,
    iouringd_resource_id_t buffer_resource_id,
    uint32_t length,
    struct iouringd_submit_result_v1 *result)
{
    struct iouringd_sock_write_fixed_request_v1 request;

    if (fd < 0 || result == NULL ||
        resource_id_is_invalid(file_resource_id) != 0 ||
        resource_id_is_invalid(buffer_resource_id) != 0 || length == 0u) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_submit_header(&request.submit, IOURINGD_TASK_KIND_SOCK_WRITE_FIXED);
    request.file_resource.resource_id = file_resource_id;
    request.buffer_resource.resource_id = buffer_resource_id;
    request.length = length;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_submit_result(fd, result);
}

int iouringd_register_fd_result(int fd,
                                int resource_fd,
                                struct iouringd_resource_result_v1 *result)
{
    struct iouringd_register_fd_request_v1 request;

    if (fd < 0 || resource_fd < 0 || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    init_control_header(&request.control, IOURINGD_REQUEST_KIND_REGISTER_FD);
    if (send_fd_with_request(fd, resource_fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_resource_result(fd, result);
}

int iouringd_register_buffer_result(int fd,
                                    const void *buf,
                                    uint32_t length,
                                    struct iouringd_resource_result_v1 *result)
{
    struct iouringd_register_buffer_request_v1 request;

    if (fd < 0 || result == NULL || length > UINT32_C(4096) ||
        (length != 0u && buf == NULL)) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_control_header(&request.control, IOURINGD_REQUEST_KIND_REGISTER_BUFFER);
    request.length = length;

    if (length == 0u) {
        if (write_full(fd, &request, sizeof(request)) != 0) {
            return -1;
        }
    } else if (write_iov_full(fd, &request, sizeof(request), buf, length) != 0) {
        return -1;
    }

    return read_resource_result(fd, result);
}

int iouringd_release_resource_result(int fd,
                                     iouringd_resource_id_t resource_id,
                                     struct iouringd_resource_result_v1 *result)
{
    struct iouringd_release_resource_request_v1 request;

    if (fd < 0 || result == NULL || resource_id_is_invalid(resource_id) != 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_control_header(&request.control, IOURINGD_REQUEST_KIND_RELEASE_RESOURCE);
    request.resource.resource_id = resource_id;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_resource_result(fd, result);
}

int iouringd_get_stats_result(int fd, struct iouringd_stats_result_v1 *result)
{
    struct iouringd_get_stats_request_v1 request;

    if (fd < 0 || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    init_control_header(&request.control, IOURINGD_REQUEST_KIND_GET_STATS);
    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_stats_result(fd, result);
}

int iouringd_get_trace_result(int fd,
                              uint64_t after_sequence,
                              uint32_t max_events,
                              struct iouringd_trace_result_v1 *result,
                              struct iouringd_trace_event_v1 *events,
                              size_t event_capacity,
                              size_t *event_count)
{
    struct iouringd_get_trace_request_v1 request;

    if (fd < 0 || result == NULL || event_count == NULL ||
        (max_events != 0u && event_capacity < max_events)) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    init_control_header(&request.control, IOURINGD_REQUEST_KIND_GET_TRACE);
    request.after_sequence = after_sequence;
    request.max_events = max_events;

    if (write_full(fd, &request, sizeof(request)) != 0) {
        return -1;
    }

    return read_trace_result(fd, result, events, event_capacity, event_count);
}

int iouringd_submit_nop(int fd, struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_nop_result(fd, &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_timeout(int fd,
                            uint64_t timeout_nsec,
                            struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_timeout_result(fd, timeout_nsec, &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_cancel(int fd,
                           iouringd_task_id_t target_task_id,
                           struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_cancel_result(fd, target_task_id, &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_sock_read(int fd,
                              iouringd_resource_id_t resource_id,
                              uint32_t length,
                              struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_sock_read_result(fd, resource_id, length, &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_sock_write(int fd,
                               iouringd_resource_id_t resource_id,
                               const void *buf,
                               uint32_t length,
                               struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_sock_write_result(fd,
                                          resource_id,
                                          buf,
                                          length,
                                          &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_file_read(int fd,
                              iouringd_resource_id_t resource_id,
                              uint32_t length,
                              struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_file_read_result(fd, resource_id, length, &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_file_write(int fd,
                               iouringd_resource_id_t resource_id,
                               const void *buf,
                               uint32_t length,
                               struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_file_write_result(fd,
                                          resource_id,
                                          buf,
                                          length,
                                          &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_poll(int fd,
                         iouringd_resource_id_t resource_id,
                         uint16_t poll_mask,
                         struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_poll_result(fd, resource_id, poll_mask, &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_connect(int fd,
                            iouringd_resource_id_t resource_id,
                            const void *sockaddr_buf,
                            uint32_t sockaddr_length,
                            struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_connect_result(fd,
                                       resource_id,
                                       sockaddr_buf,
                                       sockaddr_length,
                                       &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_accept(int fd,
                           iouringd_resource_id_t resource_id,
                           uint32_t sockaddr_length,
                           struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_accept_result(fd,
                                      resource_id,
                                      sockaddr_length,
                                      &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_openat(int fd,
                           const char *path,
                           int32_t open_flags,
                           uint32_t open_mode,
                           struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_openat_result(fd,
                                      path,
                                      open_flags,
                                      open_mode,
                                      &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_close(int fd,
                          iouringd_resource_id_t resource_id,
                          struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_close_result(fd, resource_id, &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_sock_read_fixed(int fd,
                                    iouringd_resource_id_t file_resource_id,
                                    iouringd_resource_id_t buffer_resource_id,
                                    uint32_t length,
                                    struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_sock_read_fixed_result(fd,
                                               file_resource_id,
                                               buffer_resource_id,
                                               length,
                                               &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_submit_sock_write_fixed(int fd,
                                     iouringd_resource_id_t file_resource_id,
                                     iouringd_resource_id_t buffer_resource_id,
                                     uint32_t length,
                                     struct iouringd_task_id_record_v1 *task)
{
    struct iouringd_submit_result_v1 result;

    if (task == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_submit_sock_write_fixed_result(fd,
                                                file_resource_id,
                                                buffer_resource_id,
                                                length,
                                                &result) != 0) {
        return -1;
    }

    *task = result.task;
    return 0;
}

int iouringd_register_fd(int fd,
                         int resource_fd,
                         struct iouringd_resource_id_record_v1 *resource)
{
    struct iouringd_resource_result_v1 result;

    if (resource == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_register_fd_result(fd, resource_fd, &result) != 0) {
        return -1;
    }

    *resource = result.resource;
    return 0;
}

int iouringd_register_buffer(int fd,
                             const void *buf,
                             uint32_t length,
                             struct iouringd_resource_id_record_v1 *resource)
{
    struct iouringd_resource_result_v1 result;

    if (resource == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_register_buffer_result(fd, buf, length, &result) != 0) {
        return -1;
    }

    *resource = result.resource;
    return 0;
}

int iouringd_release_resource(int fd,
                              iouringd_resource_id_t resource_id,
                              struct iouringd_resource_id_record_v1 *resource)
{
    struct iouringd_resource_result_v1 result;

    if (resource == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (iouringd_release_resource_result(fd, resource_id, &result) != 0) {
        return -1;
    }

    *resource = result.resource;
    return 0;
}

int iouringd_get_stats(int fd, struct iouringd_stats_result_v1 *result)
{
    return iouringd_get_stats_result(fd, result);
}

int iouringd_get_trace(int fd,
                       uint64_t after_sequence,
                       uint32_t max_events,
                       struct iouringd_trace_result_v1 *result,
                       struct iouringd_trace_event_v1 *events,
                       size_t event_capacity,
                       size_t *event_count)
{
    return iouringd_get_trace_result(fd,
                                     after_sequence,
                                     max_events,
                                     result,
                                     events,
                                     event_capacity,
                                     event_count);
}

int iouringd_wait_completion(int fd,
                             struct iouringd_completion_record_v1 *completion)
{
    int buffered;
    unsigned char *payload;
    size_t payload_length;

    if (fd < 0 || completion == NULL) {
        errno = EINVAL;
        return -1;
    }

    buffered = pop_buffered_completion(fd, completion);
    if (buffered != 0) {
        return buffered < 0 ? -1 : 0;
    }

    if (read_completion_with_payload(fd, completion, &payload, &payload_length) != 0) {
        return -1;
    }

    if (payload_length != 0u &&
        store_buffered_payload_owned(fd,
                                     completion->task.task_id,
                                     payload,
                                     payload_length) != 0) {
        return -1;
    }

    if (payload_length == 0u) {
        free(payload);
    }

    return 0;
}

int iouringd_read_completion_payload(int fd,
                                     const struct iouringd_completion_record_v1 *completion,
                                     void *buf,
                                     size_t buf_capacity,
                                     size_t *payload_length)
{
    struct buffered_payload *current = buffered_payloads;
    struct buffered_payload *previous = NULL;

    if (fd < 0 || completion == NULL || payload_length == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (completion->task_kind.value != IOURINGD_TASK_KIND_SOCK_READ &&
        completion->task_kind.value != IOURINGD_TASK_KIND_SOCK_READ_FIXED &&
        completion->task_kind.value != IOURINGD_TASK_KIND_FILE_READ &&
        completion->task_kind.value != IOURINGD_TASK_KIND_ACCEPT) {
        errno = EINVAL;
        return -1;
    }

    if (completion->payload_length == 0u) {
        *payload_length = 0u;
        return 0;
    }

    if (buf == NULL || buf_capacity < completion->payload_length) {
        errno = EMSGSIZE;
        return -1;
    }

    while (current != NULL) {
        if (current->fd == fd && current->task_id == completion->task.task_id) {
            memcpy(buf, current->payload, current->payload_length);
            *payload_length = current->payload_length;

            if (previous == NULL) {
                buffered_payloads = current->next;
            } else {
                previous->next = current->next;
            }

            free(current->payload);
            free(current);
            return 0;
        }

        previous = current;
        current = current->next;
    }

    errno = EPROTO;
    return -1;
}
