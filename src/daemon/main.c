#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include "handshake.h"
#include "submit.h"

#define IOURINGD_MAX_CLIENTS 64
#define IOURINGD_MAX_TASKS 64
#define IOURINGD_MAX_REGISTERED_FDS 32
#define IOURINGD_MAX_REGISTERED_BUFFERS 4
#define IOURINGD_DEFAULT_REGISTERED_BUFFERS 1u
#define IOURINGD_MAX_TOTAL_REGISTERED_FILES                                        \
    (IOURINGD_MAX_CLIENTS * IOURINGD_MAX_REGISTERED_FDS)
#define IOURINGD_MAX_TOTAL_REGISTERED_BUFFERS                                      \
    (IOURINGD_MAX_CLIENTS * IOURINGD_MAX_REGISTERED_BUFFERS)
#define IOURINGD_MAX_IO_BYTES 4096
#define IOURINGD_TRACE_CAPACITY 256u
#define IOURINGD_DEFAULT_RING_ENTRIES 4u
#define IOURINGD_MAX_REQUEST_BYTES                                             \
    (sizeof(struct iouringd_register_buffer_request_v1) + IOURINGD_MAX_IO_BYTES)

enum iouringd_client_phase {
    IOURINGD_CLIENT_PHASE_HANDSHAKE = 1,
    IOURINGD_CLIENT_PHASE_SUBMIT = 2
};

struct iouringd_resource_slot {
    iouringd_resource_id_t resource_id;
    int daemon_fd;
    unsigned in_flight;
    int registered;
    int closing;
};

struct iouringd_buffer_slot {
    iouringd_resource_id_t resource_id;
    unsigned in_flight;
    int registered;
    int closing;
    uint32_t valid_length;
};

struct iouringd_limits {
    unsigned max_clients;
    unsigned registered_fds_per_client;
    unsigned registered_buffers_per_client;
    unsigned per_client_credits;
    unsigned io_bytes_max;
    unsigned ring_entries;
};

struct iouringd_client {
    int fd;
    unsigned generation;
    enum iouringd_client_phase phase;
    int close_after_flush;
    int request_ready;
    int received_fd;
    size_t expected_input;
    size_t input_len;
    unsigned char input[IOURINGD_MAX_REQUEST_BYTES];
    unsigned char *output;
    size_t output_capacity;
    size_t output_len;
    size_t output_sent;
    struct iouringd_submit_request_v1 request;
    uint16_t control_op;
    uint64_t timeout_nsec;
    iouringd_task_id_t cancel_target_id;
    iouringd_resource_id_t resource_id;
    iouringd_resource_id_t buffer_resource_id;
    int32_t open_flags;
    int close_fd;
    uint32_t open_mode;
    uint32_t io_length;
    uint64_t trace_after_sequence;
    uint32_t trace_max_events;
    uint16_t poll_mask;
    size_t outstanding_tasks;
    size_t outstanding_credit_tasks;
    unsigned file_slot_limit;
    unsigned buffer_slot_limit;
    unsigned io_bytes_max;
    unsigned char io_payload[IOURINGD_MAX_IO_BYTES];
    unsigned next_resource_id;
    struct iouringd_resource_slot file_resources[IOURINGD_MAX_REGISTERED_FDS];
    struct iouringd_buffer_slot buffer_resources[IOURINGD_MAX_REGISTERED_BUFFERS];
};

struct iouringd_task_slot {
    int active;
    int consumes_credit;
    iouringd_task_id_t task_id;
    uint16_t task_kind;
    uint16_t submit_priority;
    size_t client_index;
    unsigned client_generation;
    int file_slot_index;
    int buffer_slot_index;
    int result_file_slot_index;
    struct __kernel_timespec timeout;
    iouringd_task_id_t cancel_target_id;
    iouringd_resource_id_t resource_id;
    iouringd_resource_id_t buffer_resource_id;
    iouringd_resource_id_t result_resource_id;
    int32_t open_flags;
    int close_fd;
    uint32_t open_mode;
    uint32_t io_length;
    uint16_t poll_mask;
    socklen_t sockaddr_length;
    struct iovec io_vector;
    unsigned char io_buffer[IOURINGD_MAX_IO_BYTES];
};

struct iouringd_runtime {
    const char *socket_path;
    int server_fd;
    int trace_to_stderr;
    int has_job_id;
    uint64_t job_id;
    pid_t pid;
    uint64_t next_task_id;
    unsigned next_client_generation;
    size_t outstanding_tasks;
    size_t outstanding_credit_tasks;
    size_t ring_capacity;
    size_t task_capacity;
    size_t dispatch_cursor;
    size_t client_output_capacity;
    size_t registered_file_capacity;
    size_t registered_buffer_capacity;
    size_t per_client_credit_limit;
    uint64_t accepted_submits;
    uint64_t rejected_submits;
    uint64_t completions;
    uint64_t next_trace_sequence;
    uint32_t trace_count;
    struct iouringd_limits limits;
    unsigned char *registered_buffer_storage;
    size_t registered_buffer_storage_size;
    struct iouringd_capability_descriptor_v1 capabilities;
    struct iouringd_ring ring;
    struct iouringd_trace_event_v1 trace_events[IOURINGD_TRACE_CAPACITY];
    struct iouringd_client clients[IOURINGD_MAX_CLIENTS];
    struct iouringd_task_slot tasks[IOURINGD_MAX_TASKS];
};

static volatile sig_atomic_t iouringd_stop_requested;
static volatile sig_atomic_t iouringd_stop_signal;
static volatile sig_atomic_t iouringd_server_fd = -1;

static void iouringd_handle_stop_signal(int signo)
{
    iouringd_stop_requested = 1;
    iouringd_stop_signal = signo;

    if (iouringd_server_fd >= 0) {
        close((int)iouringd_server_fd);
    }
}

static int iouringd_install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = iouringd_handle_stop_signal;
    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }

    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }

    return 0;
}

static int iouringd_finish_stop_signal(void)
{
    struct sigaction action;
    int signo = (int)iouringd_stop_signal;

    if (signo == 0) {
        return 0;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask) != 0) {
        return 128 + signo;
    }

    if (sigaction(signo, &action, NULL) != 0) {
        return 128 + signo;
    }

    raise(signo);
    return 128 + signo;
}

static int iouringd_set_nonblocking(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return -1;
    }

    return 0;
}

static int iouringd_set_cloexec(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) {
        return -1;
    }

    return 0;
}

static void iouringd_init_limits(struct iouringd_limits *limits)
{
    memset(limits, 0, sizeof(*limits));
    limits->max_clients = IOURINGD_MAX_CLIENTS;
    limits->registered_fds_per_client = IOURINGD_MAX_REGISTERED_FDS;
    limits->registered_buffers_per_client = IOURINGD_DEFAULT_REGISTERED_BUFFERS;
    limits->per_client_credits = IOURINGD_MAX_TASKS;
    limits->io_bytes_max = IOURINGD_MAX_IO_BYTES;
    limits->ring_entries = IOURINGD_DEFAULT_RING_ENTRIES;
}

static void iouringd_reset_client_input(struct iouringd_client *client)
{
    client->input_len = 0;
    client->expected_input = sizeof(struct iouringd_submit_request_v1);
}

static void iouringd_init_client_resources(struct iouringd_client *client)
{
    size_t index;

    client->received_fd = -1;
    client->next_resource_id = 1u;
    client->poll_mask = 0u;
    client->outstanding_tasks = 0u;
    client->outstanding_credit_tasks = 0u;
    for (index = 0; index < IOURINGD_MAX_REGISTERED_FDS; ++index) {
        client->file_resources[index].resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->file_resources[index].daemon_fd = -1;
        client->file_resources[index].in_flight = 0u;
        client->file_resources[index].registered = 0;
        client->file_resources[index].closing = 0;
    }
    for (index = 0; index < IOURINGD_MAX_REGISTERED_BUFFERS; ++index) {
        client->buffer_resources[index].resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resources[index].in_flight = 0u;
        client->buffer_resources[index].registered = 0;
        client->buffer_resources[index].closing = 0;
        client->buffer_resources[index].valid_length = 0u;
    }
}

static void iouringd_close_client(struct iouringd_client *client)
{
    size_t index;

    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }

    free(client->output);
    client->output = NULL;
    client->output_capacity = 0;
    client->output_len = 0;
    client->output_sent = 0;
    client->close_after_flush = 0;
    client->request_ready = 0;
    if (client->received_fd >= 0) {
        close(client->received_fd);
        client->received_fd = -1;
    }
    for (index = 0; index < IOURINGD_MAX_REGISTERED_FDS; ++index) {
        if (client->file_resources[index].registered != 0) {
            client->file_resources[index].closing = 1;
        }
    }
    for (index = 0; index < IOURINGD_MAX_REGISTERED_BUFFERS; ++index) {
        if (client->buffer_resources[index].registered != 0) {
            client->buffer_resources[index].closing = 1;
        }
    }
    client->phase = IOURINGD_CLIENT_PHASE_HANDSHAKE;
    memset(&client->request, 0, sizeof(client->request));
    client->control_op = 0u;
    client->timeout_nsec = 0;
    client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
    client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
    client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
    client->open_flags = 0;
    client->close_fd = -1;
    client->open_mode = 0u;
    client->io_length = 0u;
    client->trace_after_sequence = 0u;
    client->trace_max_events = 0u;
    client->poll_mask = 0u;
    memset(client->io_payload, 0, sizeof(client->io_payload));
    memset(client->input, 0, sizeof(client->input));
    iouringd_reset_client_input(client);
}

static void iouringd_init_runtime(struct iouringd_runtime *runtime)
{
    size_t index;

    memset(runtime, 0, sizeof(*runtime));
    runtime->server_fd = -1;
    runtime->pid = getpid();
    runtime->next_task_id = 1u;
    runtime->next_trace_sequence = 1u;
    runtime->ring.ring_fd = -1;
    iouringd_init_limits(&runtime->limits);

    for (index = 0; index < IOURINGD_MAX_CLIENTS; ++index) {
        runtime->clients[index].fd = -1;
        runtime->clients[index].phase = IOURINGD_CLIENT_PHASE_HANDSHAKE;
        iouringd_init_client_resources(&runtime->clients[index]);
        iouringd_reset_client_input(&runtime->clients[index]);
        runtime->clients[index].file_slot_limit = IOURINGD_MAX_REGISTERED_FDS;
        runtime->clients[index].buffer_slot_limit = IOURINGD_MAX_REGISTERED_BUFFERS;
        runtime->clients[index].io_bytes_max = IOURINGD_MAX_IO_BYTES;
    }
    for (index = 0; index < IOURINGD_MAX_TASKS; ++index) {
        runtime->tasks[index].file_slot_index = -1;
        runtime->tasks[index].buffer_slot_index = -1;
        runtime->tasks[index].result_file_slot_index = -1;
        runtime->tasks[index].close_fd = -1;
    }
}

static void iouringd_cleanup_runtime(struct iouringd_runtime *runtime)
{
    size_t index;

    if (runtime->server_fd >= 0) {
        close(runtime->server_fd);
        runtime->server_fd = -1;
    }
    iouringd_server_fd = -1;

    for (index = 0; index < runtime->limits.max_clients; ++index) {
        iouringd_close_client(&runtime->clients[index]);
    }

    runtime->outstanding_tasks = 0;
    runtime->outstanding_credit_tasks = 0;
    runtime->ring_capacity = 0;

    if (runtime->socket_path != NULL) {
        unlink(runtime->socket_path);
    }

    free(runtime->registered_buffer_storage);
    runtime->registered_buffer_storage = NULL;
    runtime->registered_buffer_storage_size = 0u;
    iouringd_ring_cleanup(&runtime->ring);
}

static int iouringd_init_registered_buffer_storage(struct iouringd_runtime *runtime)
{
    size_t total_size;

    if (runtime == NULL) {
        errno = EINVAL;
        return -1;
    }

    total_size = runtime->registered_buffer_capacity * runtime->limits.io_bytes_max;

    runtime->registered_buffer_storage = calloc(1u, total_size);
    if (runtime->registered_buffer_storage == NULL) {
        return -1;
    }

    runtime->registered_buffer_storage_size = total_size;
    return 0;
}

static int iouringd_queue_client_output(struct iouringd_client *client,
                                        const void *buf,
                                        size_t len)
{
    if (client->output_sent == client->output_len) {
        client->output_sent = 0;
        client->output_len = 0;
    } else if (client->output_sent != 0) {
        memmove(client->output,
                client->output + client->output_sent,
                client->output_len - client->output_sent);
        client->output_len -= client->output_sent;
        client->output_sent = 0;
    }

    if (len > client->output_capacity - client->output_len) {
        errno = EOVERFLOW;
        return -1;
    }

    memcpy(client->output + client->output_len, buf, len);
    client->output_len += len;
    return 0;
}

static int iouringd_flush_client_output(struct iouringd_client *client)
{
    while (client->output_sent < client->output_len) {
        ssize_t rc = write(client->fd,
                           client->output + client->output_sent,
                           client->output_len - client->output_sent);

        if (rc > 0) {
            client->output_sent += (size_t)rc;
            continue;
        }

        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        errno = rc == 0 ? EPIPE : errno;
        return -1;
    }

    client->output_len = 0;
    client->output_sent = 0;
    return 0;
}

static size_t iouringd_available_file_slots(const struct iouringd_client *client)
{
    size_t available = 0;
    size_t index;

    for (index = 0; index < client->file_slot_limit; ++index) {
        if (client->file_resources[index].registered == 0 &&
            client->file_resources[index].closing == 0 &&
            client->file_resources[index].in_flight == 0u) {
            available += 1u;
        }
    }

    return available;
}

static size_t iouringd_available_buffer_slots(const struct iouringd_client *client)
{
    size_t available = 0;
    size_t index;

    for (index = 0; index < client->buffer_slot_limit; ++index) {
        if (client->buffer_resources[index].registered == 0 &&
            client->buffer_resources[index].closing == 0 &&
            client->buffer_resources[index].in_flight == 0u) {
            available += 1u;
        }
    }

    return available;
}

static struct iouringd_resource_slot *iouringd_find_free_file_slot(
    struct iouringd_client *client)
{
    size_t index;

    for (index = 0; index < client->file_slot_limit; ++index) {
        if (client->file_resources[index].registered == 0 &&
            client->file_resources[index].closing == 0 &&
            client->file_resources[index].in_flight == 0u) {
            return &client->file_resources[index];
        }
    }

    return NULL;
}

static struct iouringd_buffer_slot *iouringd_find_free_buffer_slot(
    struct iouringd_client *client)
{
    size_t index;

    for (index = 0; index < client->buffer_slot_limit; ++index) {
        if (client->buffer_resources[index].registered == 0 &&
            client->buffer_resources[index].closing == 0 &&
            client->buffer_resources[index].in_flight == 0u) {
            return &client->buffer_resources[index];
        }
    }

    return NULL;
}

static struct iouringd_resource_slot *iouringd_find_file_slot(
    struct iouringd_client *client,
    iouringd_resource_id_t resource_id)
{
    size_t index;

    for (index = 0; index < client->file_slot_limit; ++index) {
        if (client->file_resources[index].registered != 0 &&
            client->file_resources[index].resource_id == resource_id) {
            return &client->file_resources[index];
        }
    }

    return NULL;
}

static struct iouringd_buffer_slot *iouringd_find_buffer_slot(
    struct iouringd_client *client,
    iouringd_resource_id_t resource_id)
{
    size_t index;

    for (index = 0; index < client->buffer_slot_limit; ++index) {
        if (client->buffer_resources[index].registered != 0 &&
            client->buffer_resources[index].resource_id == resource_id) {
            return &client->buffer_resources[index];
        }
    }

    return NULL;
}

static int iouringd_resource_id_in_use(const struct iouringd_client *client,
                                       iouringd_resource_id_t resource_id)
{
    size_t index;

    for (index = 0; index < client->file_slot_limit; ++index) {
        if (client->file_resources[index].registered != 0 &&
            client->file_resources[index].resource_id == resource_id) {
            return 1;
        }
        if (client->file_resources[index].resource_id == resource_id &&
            (client->file_resources[index].in_flight != 0u ||
             client->file_resources[index].closing != 0)) {
            return 1;
        }
    }

    for (index = 0; index < client->buffer_slot_limit; ++index) {
        if (client->buffer_resources[index].registered != 0 &&
            client->buffer_resources[index].resource_id == resource_id) {
            return 1;
        }
        if (client->buffer_resources[index].resource_id == resource_id &&
            (client->buffer_resources[index].in_flight != 0u ||
             client->buffer_resources[index].closing != 0)) {
            return 1;
        }
    }

    return 0;
}

static int iouringd_client_has_retained_resources(const struct iouringd_client *client)
{
    size_t index;

    for (index = 0; index < client->file_slot_limit; ++index) {
        if (client->file_resources[index].registered != 0 ||
            client->file_resources[index].closing != 0 ||
            client->file_resources[index].in_flight != 0u) {
            return 1;
        }
    }

    for (index = 0; index < client->buffer_slot_limit; ++index) {
        if (client->buffer_resources[index].registered != 0 ||
            client->buffer_resources[index].closing != 0 ||
            client->buffer_resources[index].in_flight != 0u) {
            return 1;
        }
    }

    return 0;
}

static size_t iouringd_file_slot_index(const struct iouringd_client *client,
                                       const struct iouringd_resource_slot *slot)
{
    return (size_t)(slot - client->file_resources);
}

static size_t iouringd_buffer_slot_index(const struct iouringd_client *client,
                                         const struct iouringd_buffer_slot *slot)
{
    return (size_t)(slot - client->buffer_resources);
}

static unsigned iouringd_global_file_index(const struct iouringd_runtime *runtime,
                                           size_t client_index,
                                           size_t slot_index)
{
    return (unsigned)(client_index * runtime->limits.registered_fds_per_client +
                      slot_index);
}

static unsigned iouringd_global_buffer_index(const struct iouringd_runtime *runtime,
                                             size_t client_index,
                                             size_t slot_index)
{
    return (unsigned)(client_index * runtime->limits.registered_buffers_per_client +
                      slot_index);
}

static unsigned char *iouringd_buffer_storage_at(
    const struct iouringd_runtime *runtime,
    size_t client_index,
    size_t slot_index)
{
    return runtime->registered_buffer_storage +
           ((size_t)iouringd_global_buffer_index(runtime, client_index, slot_index) *
            runtime->limits.io_bytes_max);
}

static void iouringd_clear_file_slot(struct iouringd_resource_slot *slot)
{
    if (slot->daemon_fd >= 0) {
        close(slot->daemon_fd);
    }
    slot->resource_id = IOURINGD_RESOURCE_ID_INVALID;
    slot->daemon_fd = -1;
    slot->in_flight = 0u;
    slot->registered = 0;
    slot->closing = 0;
}

static void iouringd_clear_buffer_slot(struct iouringd_buffer_slot *slot)
{
    slot->resource_id = IOURINGD_RESOURCE_ID_INVALID;
    slot->in_flight = 0u;
    slot->registered = 0;
    slot->closing = 0;
    slot->valid_length = 0u;
}

static int iouringd_try_release_file_slot(struct iouringd_runtime *runtime,
                                          struct iouringd_client *client,
                                          size_t client_index,
                                          size_t slot_index)
{
    struct iouringd_resource_slot *slot;

    if (runtime == NULL || client == NULL || slot_index >= client->file_slot_limit) {
        errno = EINVAL;
        return -1;
    }

    slot = &client->file_resources[slot_index];
    if (slot->registered == 0) {
        if (slot->in_flight == 0u) {
            iouringd_clear_file_slot(slot);
        }
        return 0;
    }

    if (slot->in_flight != 0u || slot->closing == 0) {
        return 0;
    }

    if (iouringd_ring_update_registered_file(&runtime->ring,
                                             iouringd_global_file_index(runtime,
                                                                        client_index,
                                                                        slot_index),
                                             -1) != 0) {
        return -1;
    }

    iouringd_clear_file_slot(slot);
    return 0;
}

static int iouringd_try_release_buffer_slot(struct iouringd_client *client,
                                            size_t slot_index)
{
    struct iouringd_buffer_slot *slot;

    if (client == NULL || slot_index >= client->buffer_slot_limit) {
        errno = EINVAL;
        return -1;
    }

    slot = &client->buffer_resources[slot_index];
    if (slot->registered == 0) {
        if (slot->in_flight == 0u) {
            iouringd_clear_buffer_slot(slot);
        }
        return 0;
    }

    if (slot->in_flight != 0u || slot->closing == 0) {
        return 0;
    }

    iouringd_clear_buffer_slot(slot);
    return 0;
}

static void iouringd_open_client(struct iouringd_runtime *runtime, int fd)
{
    size_t index;

    if (iouringd_set_nonblocking(fd) != 0) {
        close(fd);
        return;
    }

    for (index = 0; index < runtime->limits.max_clients; ++index) {
        struct iouringd_client *client = &runtime->clients[index];

        if (client->fd >= 0 || iouringd_client_has_retained_resources(client) != 0) {
            continue;
        }

        if (client->outstanding_tasks != 0u) {
            continue;
        }

        client->output = malloc(runtime->client_output_capacity);
        if (client->output == NULL) {
            close(fd);
            return;
        }

        client->fd = fd;
        client->generation = runtime->next_client_generation + 1u;
        runtime->next_client_generation += 1u;
        client->phase = IOURINGD_CLIENT_PHASE_HANDSHAKE;
        client->output_capacity = runtime->client_output_capacity;
        client->output_len = 0;
        client->output_sent = 0;
        client->close_after_flush = 0;
        client->request_ready = 0;
        client->received_fd = -1;
        client->file_slot_limit = runtime->limits.registered_fds_per_client;
        client->buffer_slot_limit = runtime->limits.registered_buffers_per_client;
        client->io_bytes_max = runtime->limits.io_bytes_max;
        memset(&client->request, 0, sizeof(client->request));
        client->control_op = 0u;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->open_flags = 0;
        client->close_fd = -1;
        client->open_mode = 0u;
        client->io_length = 0u;
        client->trace_after_sequence = 0u;
        client->trace_max_events = 0u;
        client->poll_mask = 0u;
        memset(client->io_payload, 0, sizeof(client->io_payload));
        memset(client->input, 0, sizeof(client->input));
        iouringd_init_client_resources(client);
        iouringd_reset_client_input(client);
        return;
    }

    close(fd);
}

static int iouringd_accept_clients(struct iouringd_runtime *runtime)
{
    int progress = 0;

    for (;;) {
        int client_fd;

        client_fd = accept(runtime->server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return progress;
            }

            if (iouringd_stop_requested != 0 &&
                (errno == EINTR || errno == EBADF)) {
                return progress;
            }

            return -1;
        }

        if (iouringd_set_cloexec(client_fd) != 0) {
            close(client_fd);
            return -1;
        }

        if (iouringd_stop_requested != 0) {
            close(client_fd);
            return progress;
        }

        iouringd_open_client(runtime, client_fd);
        progress = 1;
    }
}

static int iouringd_client_is_active(const struct iouringd_client *client)
{
    return client->fd >= 0;
}

static int iouringd_client_has_output(const struct iouringd_client *client)
{
    return client->output_len > client->output_sent;
}

static int iouringd_request_header_is_valid(
    const struct iouringd_submit_request_v1 *request)
{
    return request->header.magic == IOURINGD_PROTOCOL_WIRE_MAGIC &&
           request->header.version_major == IOURINGD_PROTOCOL_V1_MAJOR &&
           request->header.version_minor == IOURINGD_PROTOCOL_V1_MINOR &&
           request->reserved1 == 0u;
}

static int iouringd_request_kind_is_control(uint16_t request_kind)
{
    return request_kind >= IOURINGD_REQUEST_KIND_REGISTER_FD &&
           request_kind <= IOURINGD_REQUEST_KIND_GET_TRACE;
}

static int iouringd_submit_priority_is_valid(uint16_t submit_priority)
{
    return submit_priority == IOURINGD_SUBMIT_PRIORITY_NORMAL ||
           submit_priority == IOURINGD_SUBMIT_PRIORITY_LOW ||
           submit_priority == IOURINGD_SUBMIT_PRIORITY_HIGH;
}

static int iouringd_request_priority_is_valid(
    const struct iouringd_submit_request_v1 *request)
{
    if (request->task_kind.value >= IOURINGD_TASK_KIND_NOP &&
        request->task_kind.value < IOURINGD_TASK_KIND_MAX_V1) {
        return iouringd_submit_priority_is_valid(request->priority);
    }

    if (iouringd_request_kind_is_control(request->task_kind.value) != 0) {
        return request->priority == IOURINGD_SUBMIT_PRIORITY_NORMAL;
    }

    return 0;
}

static uint32_t iouringd_available_credits(const struct iouringd_runtime *runtime)
{
    return (uint32_t)(runtime->task_capacity - runtime->outstanding_credit_tasks);
}

static uint32_t iouringd_available_file_resources(const struct iouringd_client *client)
{
    return (uint32_t)iouringd_available_file_slots(client);
}

static uint32_t iouringd_available_buffer_resources(const struct iouringd_client *client)
{
    return (uint32_t)iouringd_available_buffer_slots(client);
}

static uint32_t iouringd_count_active_clients(const struct iouringd_runtime *runtime)
{
    uint32_t active = 0u;
    size_t index;

    for (index = 0; index < runtime->limits.max_clients; ++index) {
        if (runtime->clients[index].fd >= 0) {
            active += 1u;
        }
    }

    return active;
}

static uint32_t iouringd_count_registered_files(const struct iouringd_runtime *runtime)
{
    uint32_t count = 0u;
    size_t client_index;

    for (client_index = 0; client_index < runtime->limits.max_clients; ++client_index) {
        const struct iouringd_client *client = &runtime->clients[client_index];
        size_t slot_index;

        for (slot_index = 0; slot_index < client->file_slot_limit; ++slot_index) {
            if (client->file_resources[slot_index].registered != 0) {
                count += 1u;
            }
        }
    }

    return count;
}

static uint32_t iouringd_count_registered_buffers(const struct iouringd_runtime *runtime)
{
    uint32_t count = 0u;
    size_t client_index;

    for (client_index = 0; client_index < runtime->limits.max_clients; ++client_index) {
        const struct iouringd_client *client = &runtime->clients[client_index];
        size_t slot_index;

        for (slot_index = 0; slot_index < client->buffer_slot_limit; ++slot_index) {
            if (client->buffer_resources[slot_index].registered != 0) {
                count += 1u;
            }
        }
    }

    return count;
}

static uint32_t iouringd_count_clients_at_credit_limit(
    const struct iouringd_runtime *runtime)
{
    uint32_t count = 0u;
    size_t index;

    if (runtime->per_client_credit_limit == 0u) {
        return 0u;
    }

    for (index = 0; index < runtime->limits.max_clients; ++index) {
        const struct iouringd_client *client = &runtime->clients[index];

        if (client->fd < 0) {
            continue;
        }

        if (client->outstanding_credit_tasks >= runtime->per_client_credit_limit) {
            count += 1u;
        }
    }

    return count;
}

static const char *iouringd_trace_event_name(uint16_t event_kind)
{
    switch (event_kind) {
    case IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT:
        return "submit_accept";
    case IOURINGD_TRACE_EVENT_SUBMIT_REJECT:
        return "submit_reject";
    case IOURINGD_TRACE_EVENT_COMPLETION:
        return "completion";
    case IOURINGD_TRACE_EVENT_RESOURCE_REGISTER:
        return "resource_register";
    case IOURINGD_TRACE_EVENT_RESOURCE_RELEASE:
        return "resource_release";
    default:
        return "unknown";
    }
}

static const char *iouringd_task_kind_name(uint16_t task_kind)
{
    switch (task_kind) {
    case 0u:
        return "control";
    case IOURINGD_TASK_KIND_NOP:
        return "nop";
    case IOURINGD_TASK_KIND_TIMEOUT:
        return "timeout";
    case IOURINGD_TASK_KIND_CANCEL:
        return "cancel";
    case IOURINGD_TASK_KIND_SOCK_READ:
        return "sock_read";
    case IOURINGD_TASK_KIND_SOCK_WRITE:
        return "sock_write";
    case IOURINGD_TASK_KIND_SOCK_READ_FIXED:
        return "sock_read_fixed";
    case IOURINGD_TASK_KIND_SOCK_WRITE_FIXED:
        return "sock_write_fixed";
    case IOURINGD_TASK_KIND_FILE_READ:
        return "file_read";
    case IOURINGD_TASK_KIND_FILE_WRITE:
        return "file_write";
    case IOURINGD_TASK_KIND_POLL:
        return "poll";
    case IOURINGD_TASK_KIND_CONNECT:
        return "connect";
    case IOURINGD_TASK_KIND_ACCEPT:
        return "accept";
    case IOURINGD_TASK_KIND_OPENAT:
        return "openat";
    case IOURINGD_TASK_KIND_CLOSE:
        return "close";
    default:
        return "unknown";
    }
}

static const char *iouringd_priority_name(uint16_t priority)
{
    switch (priority) {
    case IOURINGD_SUBMIT_PRIORITY_NORMAL:
        return "normal";
    case IOURINGD_SUBMIT_PRIORITY_LOW:
        return "low";
    case IOURINGD_SUBMIT_PRIORITY_HIGH:
        return "high";
    default:
        return "unknown";
    }
}

static uint32_t iouringd_trace_bytes_for_client_request(
    const struct iouringd_client *client)
{
    if (client == NULL) {
        return 0u;
    }

    switch (client->control_op) {
    case IOURINGD_REQUEST_KIND_REGISTER_BUFFER:
        return client->io_length;
    default:
        break;
    }

    switch (client->request.task_kind.value) {
    case IOURINGD_TASK_KIND_SOCK_READ:
    case IOURINGD_TASK_KIND_SOCK_WRITE:
    case IOURINGD_TASK_KIND_SOCK_READ_FIXED:
    case IOURINGD_TASK_KIND_SOCK_WRITE_FIXED:
    case IOURINGD_TASK_KIND_FILE_READ:
    case IOURINGD_TASK_KIND_FILE_WRITE:
        return client->io_length;
    default:
        return 0u;
    }
}

static uint32_t iouringd_trace_bytes_for_completion(uint16_t task_kind, int32_t res)
{
    if (res <= 0) {
        return 0u;
    }

    switch (task_kind) {
    case IOURINGD_TASK_KIND_SOCK_READ:
    case IOURINGD_TASK_KIND_SOCK_WRITE:
    case IOURINGD_TASK_KIND_SOCK_READ_FIXED:
    case IOURINGD_TASK_KIND_SOCK_WRITE_FIXED:
    case IOURINGD_TASK_KIND_FILE_READ:
    case IOURINGD_TASK_KIND_FILE_WRITE:
        return (uint32_t)res;
    default:
        return 0u;
    }
}

static void iouringd_log_job_field(const struct iouringd_runtime *runtime, FILE *stream)
{
    if (runtime->has_job_id != 0) {
        fprintf(stream, " job_id=%" PRIu64, runtime->job_id);
        return;
    }

    fputs(" job_id=-", stream);
}

static void iouringd_log_metrics(const struct iouringd_runtime *runtime,
                                 const char *phase)
{
    if (runtime->trace_to_stderr == 0) {
        return;
    }

    fprintf(stderr,
            "iouringd metrics phase=%s pid=%ld",
            phase,
            (long)runtime->pid);
    iouringd_log_job_field(runtime, stderr);
    fprintf(stderr,
            " socket=%s active_clients=%u outstanding_tasks=%zu"
            " outstanding_credit_tasks=%zu available_credits=%u"
            " registered_files=%u registered_buffers=%u"
            " accepted_submits=%" PRIu64
            " rejected_submits=%" PRIu64
            " completions=%" PRIu64
            " ring_entries=%u registered_fd_slots=%u"
            " registered_buffer_slots=%u io_bytes_max=%u"
            " per_client_credit_limit=%zu clients_at_credit_limit=%u\n",
            runtime->socket_path != NULL ? runtime->socket_path : "-",
            iouringd_count_active_clients(runtime),
            runtime->outstanding_tasks,
            runtime->outstanding_credit_tasks,
            iouringd_available_credits(runtime),
            iouringd_count_registered_files(runtime),
            iouringd_count_registered_buffers(runtime),
            runtime->accepted_submits,
            runtime->rejected_submits,
            runtime->completions,
            runtime->capabilities.ring_entries,
            runtime->capabilities.registered_fd_slots,
            runtime->capabilities.registered_buffer_slots,
            runtime->capabilities.io_bytes_max,
            runtime->per_client_credit_limit,
            iouringd_count_clients_at_credit_limit(runtime));
    fflush(stderr);
}

static uint64_t iouringd_trace_oldest_sequence(
    const struct iouringd_runtime *runtime)
{
    if (runtime->trace_count == 0u) {
        return 0u;
    }

    return runtime->next_trace_sequence - runtime->trace_count;
}

static uint64_t iouringd_monotonic_nsec(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }

    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void iouringd_emit_trace_event(struct iouringd_runtime *runtime,
                                      size_t client_index,
                                      uint16_t event_kind,
                                      uint16_t task_kind,
                                      uint16_t submit_priority,
                                      iouringd_task_id_t task_id,
                                      iouringd_resource_id_t resource_id,
                                      int32_t res,
                                      uint32_t credits,
                                      uint32_t bytes)
{
    struct iouringd_trace_event_v1 *event;
    uint64_t sequence;
    size_t slot_index;

    sequence = runtime->next_trace_sequence;
    runtime->next_trace_sequence += 1u;
    slot_index = (size_t)((sequence - 1u) % IOURINGD_TRACE_CAPACITY);
    event = &runtime->trace_events[slot_index];
    memset(event, 0, sizeof(*event));
    event->sequence = sequence;
    event->timestamp_nsec = iouringd_monotonic_nsec();
    event->task.task_id = task_id;
    event->resource.resource_id = resource_id;
    event->res = res;
    event->credits = credits;
    event->event_kind = event_kind;
    event->task_kind.value = task_kind;
    event->client_slot = (uint16_t)client_index;
    event->priority = submit_priority;
    if (runtime->trace_count < IOURINGD_TRACE_CAPACITY) {
        runtime->trace_count += 1u;
    }

    if (runtime->trace_to_stderr != 0) {
        fprintf(stderr,
                "iouringd trace sequence=%" PRIu64
                " timestamp_nsec=%" PRIu64
                " pid=%ld",
                event->sequence,
                event->timestamp_nsec,
                (long)runtime->pid);
        iouringd_log_job_field(runtime, stderr);
        fprintf(stderr,
                " client_slot=%u event=%s task_kind=%s priority=%s"
                " task_id=%" PRIu64
                " resource_id=%u bytes=%u result=%d credits=%u\n",
                (unsigned)event->client_slot,
                iouringd_trace_event_name(event_kind),
                iouringd_task_kind_name(task_kind),
                iouringd_priority_name(submit_priority),
                event->task.task_id,
                event->resource.resource_id,
                bytes,
                event->res,
                event->credits);
        fflush(stderr);
    }
}

static void iouringd_log_errno(const char *context)
{
    fprintf(stderr, "iouringd: %s: %s\n", context, strerror(errno));
}

static void iouringd_report_ring_init_error(int err)
{
    switch (err) {
    case ENOSYS:
        fprintf(stderr,
                "iouringd: io_uring setup is unavailable on this kernel"
                " (ENOSYS)\n");
        break;
    case EPERM:
    case EACCES:
        fprintf(stderr,
                "iouringd: io_uring setup blocked by kernel policy or sandbox"
                " (%s)\n",
                strerror(err));
        break;
    case ENOTSUP:
        fprintf(stderr,
                "iouringd: io_uring probe reported no supported operations"
                " (ENOTSUP)\n");
        break;
    default:
        errno = err;
        iouringd_log_errno("io_uring initialization failed");
        break;
    }
}

static void iouringd_print_usage(FILE *stream)
{
    fprintf(stream,
            "usage: iouringd [--ring-entries N] [--max-clients N]\n"
            "                 [--registered-fds N] [--registered-buffers N]\n"
            "                 [--per-client-credits N] [--io-bytes-max N]\n"
            "                 [--job-id N] [--trace-stderr]\n"
            "                 SOCKET_PATH\n");
}

static int iouringd_queue_handshake_result(
    const struct iouringd_runtime *runtime,
    struct iouringd_client *client,
    const struct iouringd_handshake_request_v1 *request)
{
    struct iouringd_handshake_result_v1 result;

    memset(&result, 0, sizeof(result));
    result.response.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    result.response.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    result.response.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    result.response.abi_major = IOURINGD_ABI_V1_MAJOR;
    result.response.abi_minor = IOURINGD_ABI_V1_MINOR;
    result.response.status = IOURINGD_HANDSHAKE_STATUS_ACCEPT;

    if (request->header.magic != IOURINGD_PROTOCOL_WIRE_MAGIC ||
        request->header.version_major != IOURINGD_PROTOCOL_V1_MAJOR ||
        request->header.version_minor != IOURINGD_PROTOCOL_V1_MINOR ||
        request->abi_major != IOURINGD_ABI_V1_MAJOR ||
        request->abi_minor != IOURINGD_ABI_V1_MINOR ||
        request->reserved != 0u) {
        result.response.status = IOURINGD_HANDSHAKE_STATUS_REJECT;
        client->close_after_flush = 1;
    } else {
        result.capabilities = runtime->capabilities;
        result.capabilities.submit_credits = iouringd_available_credits(runtime);
        client->phase = IOURINGD_CLIENT_PHASE_SUBMIT;
    }

    if (iouringd_queue_client_output(client, &result, sizeof(result)) != 0) {
        return -1;
    }

    iouringd_reset_client_input(client);
    return 0;
}

static void iouringd_clear_task_slot(struct iouringd_task_slot *task)
{
    memset(task, 0, sizeof(*task));
    task->file_slot_index = -1;
    task->buffer_slot_index = -1;
    task->result_file_slot_index = -1;
    task->close_fd = -1;
}

static int iouringd_read_client_input(struct iouringd_client *client, int *closed)
{
    *closed = 0;

    while (client->input_len < client->expected_input) {
        char control[CMSG_SPACE(sizeof(int))];
        struct iovec iov;
        struct msghdr msg;
        ssize_t rc;

        memset(control, 0, sizeof(control));
        memset(&msg, 0, sizeof(msg));
        iov.iov_base = client->input + client->input_len;
        iov.iov_len = client->expected_input - client->input_len;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        rc = recvmsg(client->fd, &msg, 0);

        if (rc > 0) {
            struct cmsghdr *cmsg;

            if ((msg.msg_flags & MSG_CTRUNC) != 0) {
                errno = EPROTO;
                return -1;
            }

            for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
                 cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                if (cmsg->cmsg_level != SOL_SOCKET ||
                    cmsg->cmsg_type != SCM_RIGHTS ||
                    cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
                    errno = EPROTO;
                    return -1;
                }

                if (client->received_fd >= 0) {
                    int received_fd;

                    memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(received_fd));
                    close(received_fd);
                    errno = EPROTO;
                    return -1;
                }

                memcpy(&client->received_fd, CMSG_DATA(cmsg), sizeof(client->received_fd));
            }

            client->input_len += (size_t)rc;
            continue;
        }

        if (rc == 0) {
            if (client->input_len == 0) {
                *closed = 1;
                return 0;
            }

            errno = EPROTO;
            return -1;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        return -1;
    }

    return 1;
}

static int iouringd_finish_submit_request(struct iouringd_client *client)
{
    if (!iouringd_request_header_is_valid(&client->request)) {
        errno = EPROTO;
        return -1;
    }

    if (iouringd_request_priority_is_valid(&client->request) == 0) {
        errno = EPROTO;
        return -1;
    }

    if (client->request.task_kind.value != IOURINGD_REQUEST_KIND_REGISTER_FD &&
        client->received_fd >= 0) {
        errno = EPROTO;
        return -1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_NOP) {
        client->request_ready = 1;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = 0u;
        client->control_op = 0u;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_TIMEOUT) {
        struct iouringd_timeout_request_v1 timeout_request;

        memcpy(&timeout_request, client->input, sizeof(timeout_request));
        client->request = timeout_request.submit;
        client->timeout_nsec = timeout_request.timeout_nsec;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = 0u;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CANCEL) {
        struct iouringd_cancel_request_v1 cancel_request;

        memcpy(&cancel_request, client->input, sizeof(cancel_request));
        client->request.header = cancel_request.header;
        client->request.task_kind = cancel_request.task_kind;
        client->request.priority = cancel_request.reserved0;
        client->request.reserved1 = cancel_request.reserved1;
        client->timeout_nsec = 0;
        client->cancel_target_id = cancel_request.target.task_id;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = 0u;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ) {
        struct iouringd_sock_read_request_v1 read_request;

        memcpy(&read_request, client->input, sizeof(read_request));
        client->request = read_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = read_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = read_request.length;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_FILE_READ) {
        struct iouringd_file_read_request_v1 read_request;

        memcpy(&read_request, client->input, sizeof(read_request));
        client->request = read_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = read_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = read_request.length;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE) {
        struct iouringd_sock_write_request_v1 write_request;

        memcpy(&write_request, client->input, sizeof(write_request));
        client->request = write_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = write_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = write_request.length;
        if (client->io_length > client->io_bytes_max) {
            errno = EMSGSIZE;
            return -1;
        }
        memcpy(client->io_payload,
               client->input + sizeof(write_request),
               client->io_length);
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_FILE_WRITE) {
        struct iouringd_file_write_request_v1 write_request;

        memcpy(&write_request, client->input, sizeof(write_request));
        client->request = write_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = write_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = write_request.length;
        if (client->io_length > client->io_bytes_max) {
            errno = EMSGSIZE;
            return -1;
        }
        memcpy(client->io_payload,
               client->input + sizeof(write_request),
               client->io_length);
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_POLL) {
        struct iouringd_poll_request_v1 poll_request;

        memcpy(&poll_request, client->input, sizeof(poll_request));
        client->request = poll_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = poll_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = 0u;
        client->poll_mask = poll_request.poll_mask;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CONNECT) {
        struct iouringd_connect_request_v1 connect_request;

        memcpy(&connect_request, client->input, sizeof(connect_request));
        client->request = connect_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = connect_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = connect_request.sockaddr_length;
        if (client->io_length > client->io_bytes_max) {
            errno = EMSGSIZE;
            return -1;
        }
        memcpy(client->io_payload,
               client->input + sizeof(connect_request),
               client->io_length);
        client->poll_mask = 0u;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_ACCEPT) {
        struct iouringd_accept_request_v1 accept_request;

        memcpy(&accept_request, client->input, sizeof(accept_request));
        client->request = accept_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = accept_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = accept_request.sockaddr_length;
        client->poll_mask = 0u;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_OPENAT) {
        struct iouringd_openat_request_v1 openat_request;

        memcpy(&openat_request, client->input, sizeof(openat_request));
        client->request = openat_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->open_flags = openat_request.open_flags;
        client->open_mode = openat_request.open_mode;
        client->io_length = openat_request.path_length;
        if (client->io_length > client->io_bytes_max) {
            errno = EMSGSIZE;
            return -1;
        }
        memcpy(client->io_payload,
               client->input + sizeof(openat_request),
               client->io_length);
        if (client->io_length < 2u || client->io_payload[client->io_length - 1u] != '\0') {
            errno = EPROTO;
            return -1;
        }
        client->poll_mask = 0u;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CLOSE) {
        struct iouringd_close_request_v1 close_request;

        memcpy(&close_request, client->input, sizeof(close_request));
        client->request = close_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = close_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->open_flags = 0;
        client->close_fd = -1;
        client->open_mode = 0u;
        client->io_length = 0u;
        client->poll_mask = 0u;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ_FIXED) {
        struct iouringd_sock_read_fixed_request_v1 read_request;

        memcpy(&read_request, client->input, sizeof(read_request));
        client->request = read_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = read_request.file_resource.resource_id;
        client->buffer_resource_id = read_request.buffer_resource.resource_id;
        client->io_length = read_request.length;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE_FIXED) {
        struct iouringd_sock_write_fixed_request_v1 write_request;

        memcpy(&write_request, client->input, sizeof(write_request));
        client->request = write_request.submit;
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = write_request.file_resource.resource_id;
        client->buffer_resource_id = write_request.buffer_resource.resource_id;
        client->io_length = write_request.length;
        client->control_op = 0u;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_REQUEST_KIND_REGISTER_FD) {
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = 0u;
        client->control_op = IOURINGD_REQUEST_KIND_REGISTER_FD;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_REQUEST_KIND_REGISTER_BUFFER) {
        struct iouringd_register_buffer_request_v1 buffer_request;

        memcpy(&buffer_request, client->input, sizeof(buffer_request));
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = buffer_request.length;
        if (client->io_length > client->io_bytes_max) {
            errno = EMSGSIZE;
            return -1;
        }
        if (client->io_length != 0u) {
            memcpy(client->io_payload,
                   client->input + sizeof(buffer_request),
                   client->io_length);
        }
        client->control_op = IOURINGD_REQUEST_KIND_REGISTER_BUFFER;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_REQUEST_KIND_RELEASE_RESOURCE) {
        struct iouringd_release_resource_request_v1 release_request;

        memcpy(&release_request, client->input, sizeof(release_request));
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = release_request.resource.resource_id;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = 0u;
        client->control_op = IOURINGD_REQUEST_KIND_RELEASE_RESOURCE;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_REQUEST_KIND_GET_STATS) {
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = 0u;
        client->trace_after_sequence = 0u;
        client->trace_max_events = 0u;
        client->poll_mask = 0u;
        client->control_op = IOURINGD_REQUEST_KIND_GET_STATS;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_REQUEST_KIND_GET_TRACE) {
        struct iouringd_get_trace_request_v1 trace_request;

        memcpy(&trace_request, client->input, sizeof(trace_request));
        client->timeout_nsec = 0;
        client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
        client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
        client->io_length = 0u;
        client->trace_after_sequence = trace_request.after_sequence;
        client->trace_max_events = trace_request.max_events;
        client->poll_mask = 0u;
        client->control_op = IOURINGD_REQUEST_KIND_GET_TRACE;
        client->request_ready = 1;
        iouringd_reset_client_input(client);
        return 1;
    }

    errno = EPROTO;
    return -1;
}

static int iouringd_process_submit_input(struct iouringd_client *client)
{
    struct iouringd_submit_request_v1 request;

    if (client->input_len < sizeof(request)) {
        client->expected_input = sizeof(request);
        return 0;
    }

    memcpy(&request, client->input, sizeof(request));
    client->request = request;

    if (!iouringd_request_header_is_valid(&request)) {
        errno = EPROTO;
        return -1;
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_TIMEOUT ||
        request.task_kind.value == IOURINGD_TASK_KIND_CANCEL) {
        client->expected_input = sizeof(struct iouringd_cancel_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }
        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ ||
        request.task_kind.value == IOURINGD_TASK_KIND_FILE_READ) {
        client->expected_input = sizeof(struct iouringd_sock_read_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }
        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_POLL) {
        struct iouringd_poll_request_v1 poll_request;

        client->expected_input = sizeof(struct iouringd_poll_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }

        memcpy(&poll_request, client->input, sizeof(poll_request));
        if (poll_request.reserved2 != 0u) {
            errno = EPROTO;
            return -1;
        }
        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_CONNECT) {
        struct iouringd_connect_request_v1 connect_request;

        client->expected_input = sizeof(struct iouringd_connect_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }

        memcpy(&connect_request, client->input, sizeof(connect_request));
        if (connect_request.sockaddr_length == 0u ||
            connect_request.sockaddr_length > client->io_bytes_max ||
            connect_request.sockaddr_length > UINT16_MAX) {
            errno = EPROTO;
            return -1;
        }

        client->expected_input = sizeof(connect_request) + connect_request.sockaddr_length;
        if (client->expected_input > sizeof(client->input)) {
            errno = EPROTO;
            return -1;
        }

        if (client->input_len < client->expected_input) {
            return 0;
        }

        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_ACCEPT) {
        struct iouringd_accept_request_v1 accept_request;

        client->expected_input = sizeof(struct iouringd_accept_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }

        memcpy(&accept_request, client->input, sizeof(accept_request));
        if (accept_request.sockaddr_length > client->io_bytes_max ||
            accept_request.sockaddr_length > UINT16_MAX) {
            errno = EPROTO;
            return -1;
        }

        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_OPENAT) {
        struct iouringd_openat_request_v1 openat_request;

        client->expected_input = sizeof(struct iouringd_openat_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }

        memcpy(&openat_request, client->input, sizeof(openat_request));
        if (openat_request.path_length < 2u ||
            openat_request.path_length > client->io_bytes_max) {
            errno = EPROTO;
            return -1;
        }

        client->expected_input = sizeof(openat_request) + openat_request.path_length;
        if (client->expected_input > sizeof(client->input)) {
            errno = EPROTO;
            return -1;
        }

        if (client->input_len < client->expected_input) {
            return 0;
        }

        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_CLOSE) {
        struct iouringd_close_request_v1 close_request;

        client->expected_input = sizeof(struct iouringd_close_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }

        memcpy(&close_request, client->input, sizeof(close_request));
        if (close_request.reserved2 != 0u) {
            errno = EPROTO;
            return -1;
        }

        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ_FIXED) {
        client->expected_input = sizeof(struct iouringd_sock_read_fixed_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }
        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_REQUEST_KIND_RELEASE_RESOURCE) {
        client->expected_input = sizeof(struct iouringd_release_resource_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }
        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_REQUEST_KIND_GET_STATS) {
        client->expected_input = sizeof(struct iouringd_get_stats_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }
        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_REQUEST_KIND_GET_TRACE) {
        struct iouringd_get_trace_request_v1 trace_request;

        client->expected_input = sizeof(struct iouringd_get_trace_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }

        memcpy(&trace_request, client->input, sizeof(trace_request));
        if (trace_request.max_events > IOURINGD_TRACE_CAPACITY ||
            trace_request.reserved2 != 0u) {
            errno = EPROTO;
            return -1;
        }

        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE_FIXED) {
        client->expected_input = sizeof(struct iouringd_sock_write_fixed_request_v1);
        if (client->input_len < client->expected_input) {
            return 0;
        }
        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_REQUEST_KIND_REGISTER_BUFFER) {
        struct iouringd_register_buffer_request_v1 buffer_request;

        client->expected_input = sizeof(buffer_request);
        if (client->input_len < client->expected_input) {
            return 0;
        }

        memcpy(&buffer_request, client->input, sizeof(buffer_request));
        if (buffer_request.length > client->io_bytes_max ||
            buffer_request.reserved2 != 0u) {
            errno = EPROTO;
            return -1;
        }

        client->expected_input = sizeof(buffer_request) + buffer_request.length;
        if (client->expected_input > sizeof(client->input)) {
            errno = EPROTO;
            return -1;
        }

        if (client->input_len < client->expected_input) {
            return 0;
        }

        return iouringd_finish_submit_request(client);
    }

    if (request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE ||
        request.task_kind.value == IOURINGD_TASK_KIND_FILE_WRITE) {
        struct iouringd_sock_write_request_v1 write_request;

        client->expected_input = sizeof(write_request);
        if (client->input_len < client->expected_input) {
            return 0;
        }

        memcpy(&write_request, client->input, sizeof(write_request));
        if (write_request.length == 0u || write_request.length > client->io_bytes_max) {
            errno = EPROTO;
            return -1;
        }

        client->expected_input = sizeof(write_request) + write_request.length;
        if (client->expected_input > sizeof(client->input)) {
            errno = EPROTO;
            return -1;
        }

        if (client->input_len < client->expected_input) {
            return 0;
        }

        return iouringd_finish_submit_request(client);
    }

    return iouringd_finish_submit_request(client);
}

static int iouringd_service_client_readable(struct iouringd_runtime *runtime,
                                            size_t client_index)
{
    struct iouringd_client *client = &runtime->clients[client_index];
    int progress = 0;

    while (client->fd >= 0 && client->request_ready == 0) {
        int closed;
        int rc = iouringd_read_client_input(client, &closed);

        if (closed != 0) {
            iouringd_close_client(client);
            return 1;
        }

        if (rc < 0) {
            iouringd_close_client(client);
            return 1;
        }

        if (rc == 0) {
            return progress;
        }

        progress = 1;
        if (client->phase == IOURINGD_CLIENT_PHASE_HANDSHAKE) {
            struct iouringd_handshake_request_v1 request;

            memcpy(&request, client->input, sizeof(request));
            if (iouringd_queue_handshake_result(runtime, client, &request) != 0) {
                return -1;
            }
            return 1;
        }

        if (iouringd_process_submit_input(client) < 0) {
            iouringd_close_client(client);
            return 1;
        }

        if (client->request_ready != 0) {
            return 1;
        }
    }

    return progress;
}

static struct iouringd_task_slot *iouringd_find_free_task_slot(
    struct iouringd_runtime *runtime)
{
    size_t index;

    for (index = 0; index < runtime->task_capacity; ++index) {
        if (runtime->tasks[index].active == 0) {
            return &runtime->tasks[index];
        }
    }

    return NULL;
}

static struct iouringd_task_slot *iouringd_find_task_slot(
    struct iouringd_runtime *runtime,
    iouringd_task_id_t task_id)
{
    size_t index;

    for (index = 0; index < runtime->task_capacity; ++index) {
        if (runtime->tasks[index].active != 0 &&
            runtime->tasks[index].task_id == task_id) {
            return &runtime->tasks[index];
        }
    }

    return NULL;
}

static int iouringd_queue_submit_result(const struct iouringd_runtime *runtime,
                                        struct iouringd_client *client,
                                        iouringd_task_id_t task_id,
                                        int32_t res)
{
    struct iouringd_submit_result_v1 result;

    memset(&result, 0, sizeof(result));
    result.task.task_id = task_id;
    result.res = res;
    result.credits = iouringd_available_credits(runtime);
    return iouringd_queue_client_output(client, &result, sizeof(result));
}

static int iouringd_reject_submit(struct iouringd_runtime *runtime,
                                  struct iouringd_client *client,
                                  int32_t res)
{
    runtime->rejected_submits += 1u;
    if (iouringd_queue_submit_result(runtime,
                                     client,
                                     IOURINGD_TASK_ID_INVALID,
                                     res) != 0) {
        return -1;
    }

    iouringd_emit_trace_event(runtime,
                              (size_t)(client - runtime->clients),
                              IOURINGD_TRACE_EVENT_SUBMIT_REJECT,
                              client->request.task_kind.value,
                              client->request.priority,
                              IOURINGD_TASK_ID_INVALID,
                              client->resource_id,
                              res,
                              iouringd_available_credits(runtime),
                              iouringd_trace_bytes_for_client_request(client));
    return 0;
}

static int iouringd_queue_resource_result(struct iouringd_client *client,
                                          iouringd_resource_id_t resource_id,
                                          int32_t res,
                                          uint32_t resources_available)
{
    struct iouringd_resource_result_v1 result;

    memset(&result, 0, sizeof(result));
    result.resource.resource_id = resource_id;
    result.res = res;
    result.resources_available = resources_available;
    return iouringd_queue_client_output(client, &result, sizeof(result));
}

static int iouringd_queue_stats_result(const struct iouringd_runtime *runtime,
                                       struct iouringd_client *client)
{
    struct iouringd_stats_result_v1 result;

    memset(&result, 0, sizeof(result));
    result.active_clients = iouringd_count_active_clients(runtime);
    result.outstanding_tasks = (uint32_t)runtime->outstanding_tasks;
    result.outstanding_credit_tasks = (uint32_t)runtime->outstanding_credit_tasks;
    result.available_credits = iouringd_available_credits(runtime);
    result.registered_files = iouringd_count_registered_files(runtime);
    result.registered_buffers = iouringd_count_registered_buffers(runtime);
    result.accepted_submits = runtime->accepted_submits;
    result.rejected_submits = runtime->rejected_submits;
    result.completions = runtime->completions;
    result.per_client_credit_limit = (uint32_t)runtime->per_client_credit_limit;
    result.clients_at_credit_limit = iouringd_count_clients_at_credit_limit(runtime);
    return iouringd_queue_client_output(client, &result, sizeof(result));
}

static int iouringd_queue_trace_result(const struct iouringd_runtime *runtime,
                                       struct iouringd_client *client)
{
    struct iouringd_trace_result_v1 result;
    uint64_t oldest_sequence;
    uint64_t latest_sequence;
    uint64_t sequence;
    uint32_t emitted = 0u;

    memset(&result, 0, sizeof(result));
    oldest_sequence = iouringd_trace_oldest_sequence(runtime);
    latest_sequence = runtime->next_trace_sequence == 0u
                          ? 0u
                          : runtime->next_trace_sequence - 1u;
    result.oldest_sequence = oldest_sequence;
    result.latest_sequence = latest_sequence;

    if (client->trace_max_events != 0u && runtime->trace_count != 0u &&
        client->trace_after_sequence < latest_sequence) {
        sequence = client->trace_after_sequence + 1u;
        if (sequence < oldest_sequence) {
            sequence = oldest_sequence;
        }

        while (sequence <= latest_sequence && emitted < client->trace_max_events) {
            emitted += 1u;
            sequence += 1u;
        }
    }

    result.count = emitted;
    if (iouringd_queue_client_output(client, &result, sizeof(result)) != 0) {
        return -1;
    }

    if (emitted == 0u) {
        return 0;
    }

    sequence = client->trace_after_sequence + 1u;
    if (sequence < oldest_sequence) {
        sequence = oldest_sequence;
    }

    while (sequence <= latest_sequence && result.count != 0u) {
        const struct iouringd_trace_event_v1 *event =
            &runtime->trace_events[(size_t)((sequence - 1u) % IOURINGD_TRACE_CAPACITY)];

        if (iouringd_queue_client_output(client, event, sizeof(*event)) != 0) {
            return -1;
        }

        result.count -= 1u;
        sequence += 1u;
    }

    return 0;
}

static void iouringd_finish_client_request(struct iouringd_client *client)
{
    client->request_ready = 0;
    client->control_op = 0u;
    client->timeout_nsec = 0;
    client->cancel_target_id = IOURINGD_TASK_ID_INVALID;
    client->resource_id = IOURINGD_RESOURCE_ID_INVALID;
    client->buffer_resource_id = IOURINGD_RESOURCE_ID_INVALID;
    client->open_flags = 0;
    client->close_fd = -1;
    client->open_mode = 0u;
    client->io_length = 0u;
    client->trace_after_sequence = 0u;
    client->trace_max_events = 0u;
    client->poll_mask = 0u;
}

static iouringd_resource_id_t iouringd_allocate_resource_id(
    struct iouringd_client *client)
{
    size_t attempt;

    for (attempt = 0;
         attempt < client->file_slot_limit + client->buffer_slot_limit;
         ++attempt) {
        iouringd_resource_id_t candidate = (iouringd_resource_id_t)client->next_resource_id;

        client->next_resource_id += 1u;
        if (candidate == IOURINGD_RESOURCE_ID_INVALID) {
            continue;
        }
        if (iouringd_resource_id_in_use(client, candidate) == 0) {
            return candidate;
        }
    }

    return IOURINGD_RESOURCE_ID_INVALID;
}

static void iouringd_unallocate_resource_id(struct iouringd_client *client,
                                            iouringd_resource_id_t resource_id)
{
    if (resource_id != IOURINGD_RESOURCE_ID_INVALID) {
        client->next_resource_id = resource_id;
    }
}

static int iouringd_dispatch_client_request(struct iouringd_runtime *runtime,
                                            size_t client_index)
{
    struct iouringd_client *client = &runtime->clients[client_index];
    struct iouringd_task_slot *task;
    struct iouringd_resource_slot *file_resource;
    struct iouringd_buffer_slot *buffer_resource;
    int uses_credit;

    if (client->request_ready == 0) {
        return 0;
    }

    if (client->control_op == IOURINGD_REQUEST_KIND_REGISTER_FD) {
        struct iouringd_resource_slot *slot;
        iouringd_resource_id_t resource_id = IOURINGD_RESOURCE_ID_INVALID;
        size_t slot_index;

        if (client->received_fd < 0) {
            if (iouringd_queue_resource_result(client,
                                               IOURINGD_RESOURCE_ID_INVALID,
                                               -EINVAL,
                                               iouringd_available_file_resources(client)) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        slot = iouringd_find_free_file_slot(client);
        if (slot == NULL) {
            if (iouringd_queue_resource_result(client,
                                               IOURINGD_RESOURCE_ID_INVALID,
                                               -EAGAIN,
                                               iouringd_available_file_resources(client)) != 0) {
                return -1;
            }
            close(client->received_fd);
            client->received_fd = -1;
            iouringd_finish_client_request(client);
            return 1;
        }

        resource_id = iouringd_allocate_resource_id(client);
        if (resource_id == IOURINGD_RESOURCE_ID_INVALID) {
            if (iouringd_queue_resource_result(client,
                                               IOURINGD_RESOURCE_ID_INVALID,
                                               -EOVERFLOW,
                                               iouringd_available_file_resources(client)) != 0) {
                return -1;
            }
            close(client->received_fd);
            client->received_fd = -1;
            iouringd_finish_client_request(client);
            return 1;
        }

        slot_index = iouringd_file_slot_index(client, slot);
        if (iouringd_ring_update_registered_file(&runtime->ring,
                                                 iouringd_global_file_index(runtime,
                                                                            client_index,
                                                                            slot_index),
                                                 client->received_fd) != 0) {
            int saved_errno = errno;

            close(client->received_fd);
            client->received_fd = -1;
            if (iouringd_queue_resource_result(client,
                                               IOURINGD_RESOURCE_ID_INVALID,
                                               -saved_errno,
                                               iouringd_available_file_resources(client)) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        slot->resource_id = resource_id;
        slot->daemon_fd = client->received_fd;
        slot->registered = 1;
        slot->closing = 0;
        slot->in_flight = 0u;
        client->received_fd = -1;
        if (iouringd_queue_resource_result(client,
                                           resource_id,
                                           0,
                                           iouringd_available_file_resources(client)) != 0) {
            return -1;
        }
        iouringd_emit_trace_event(runtime,
                                  client_index,
                                  IOURINGD_TRACE_EVENT_RESOURCE_REGISTER,
                                  0u,
                                  IOURINGD_SUBMIT_PRIORITY_NORMAL,
                                  IOURINGD_TASK_ID_INVALID,
                                  resource_id,
                                  0,
                                  iouringd_available_credits(runtime),
                                  0u);
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->control_op == IOURINGD_REQUEST_KIND_REGISTER_BUFFER) {
        struct iouringd_buffer_slot *slot;
        iouringd_resource_id_t resource_id;
        size_t slot_index;

        slot = iouringd_find_free_buffer_slot(client);
        if (slot == NULL) {
            if (iouringd_queue_resource_result(client,
                                               IOURINGD_RESOURCE_ID_INVALID,
                                               -EAGAIN,
                                               iouringd_available_buffer_resources(client)) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        resource_id = iouringd_allocate_resource_id(client);
        if (resource_id == IOURINGD_RESOURCE_ID_INVALID) {
            if (iouringd_queue_resource_result(client,
                                               IOURINGD_RESOURCE_ID_INVALID,
                                               -EOVERFLOW,
                                               iouringd_available_buffer_resources(client)) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        slot_index = iouringd_buffer_slot_index(client, slot);
        if (client->io_length != 0u) {
            memcpy(iouringd_buffer_storage_at(runtime, client_index, slot_index),
                   client->io_payload,
                   client->io_length);
        }
        slot->resource_id = resource_id;
        slot->registered = 1;
        slot->closing = 0;
        slot->in_flight = 0u;
        slot->valid_length = client->io_length;
        if (iouringd_queue_resource_result(client,
                                           resource_id,
                                           0,
                                           iouringd_available_buffer_resources(client)) != 0) {
            return -1;
        }
        iouringd_emit_trace_event(runtime,
                                  client_index,
                                  IOURINGD_TRACE_EVENT_RESOURCE_REGISTER,
                                  0u,
                                  IOURINGD_SUBMIT_PRIORITY_NORMAL,
                                  IOURINGD_TASK_ID_INVALID,
                                  resource_id,
                                  0,
                                  iouringd_available_credits(runtime),
                                  client->io_length);
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->control_op == IOURINGD_REQUEST_KIND_RELEASE_RESOURCE) {
        iouringd_resource_id_t released_id;

        if (client->resource_id == IOURINGD_RESOURCE_ID_INVALID) {
            if (iouringd_queue_resource_result(client,
                                               IOURINGD_RESOURCE_ID_INVALID,
                                               -EINVAL,
                                               0u) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        file_resource = iouringd_find_file_slot(client, client->resource_id);
        if (file_resource != NULL) {
            if (file_resource->in_flight != 0u) {
                if (iouringd_queue_resource_result(client,
                                                   IOURINGD_RESOURCE_ID_INVALID,
                                                   -EBUSY,
                                                   iouringd_available_file_resources(client)) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }

            released_id = file_resource->resource_id;
            file_resource->closing = 1;
            if (iouringd_try_release_file_slot(runtime,
                                               client,
                                               client_index,
                                               iouringd_file_slot_index(client,
                                                                        file_resource)) != 0) {
                return -1;
            }
            if (iouringd_queue_resource_result(client,
                                               released_id,
                                               0,
                                               iouringd_available_file_resources(client)) != 0) {
                return -1;
            }
            iouringd_emit_trace_event(runtime,
                                      client_index,
                                      IOURINGD_TRACE_EVENT_RESOURCE_RELEASE,
                                      0u,
                                      IOURINGD_SUBMIT_PRIORITY_NORMAL,
                                      IOURINGD_TASK_ID_INVALID,
                                      released_id,
                                      0,
                                      iouringd_available_credits(runtime),
                                      0u);
            iouringd_finish_client_request(client);
            return 1;
        }

        buffer_resource = iouringd_find_buffer_slot(client, client->resource_id);
        if (buffer_resource != NULL) {
            if (buffer_resource->in_flight != 0u) {
                if (iouringd_queue_resource_result(client,
                                                   IOURINGD_RESOURCE_ID_INVALID,
                                                   -EBUSY,
                                                   iouringd_available_buffer_resources(client)) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }

            released_id = buffer_resource->resource_id;
            buffer_resource->closing = 1;
            if (iouringd_try_release_buffer_slot(client,
                                                 iouringd_buffer_slot_index(client,
                                                                            buffer_resource)) != 0) {
                return -1;
            }
            if (iouringd_queue_resource_result(client,
                                               released_id,
                                               0,
                                               iouringd_available_buffer_resources(client)) != 0) {
                return -1;
            }
            iouringd_emit_trace_event(runtime,
                                      client_index,
                                      IOURINGD_TRACE_EVENT_RESOURCE_RELEASE,
                                      0u,
                                      IOURINGD_SUBMIT_PRIORITY_NORMAL,
                                      IOURINGD_TASK_ID_INVALID,
                                      released_id,
                                      0,
                                      iouringd_available_credits(runtime),
                                      0u);
            iouringd_finish_client_request(client);
            return 1;
        }

        if (iouringd_queue_resource_result(client,
                                           IOURINGD_RESOURCE_ID_INVALID,
                                           -ENOENT,
                                           0u) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->control_op == IOURINGD_REQUEST_KIND_GET_STATS) {
        if (iouringd_queue_stats_result(runtime, client) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->control_op == IOURINGD_REQUEST_KIND_GET_TRACE) {
        if (iouringd_queue_trace_result(runtime, client) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    uses_credit = client->request.task_kind.value != IOURINGD_TASK_KIND_CANCEL;
    if (uses_credit != 0 &&
        runtime->outstanding_credit_tasks >= runtime->task_capacity) {
        if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (uses_credit != 0 &&
        client->outstanding_credit_tasks >= runtime->per_client_credit_limit) {
        if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (uses_credit == 0 && runtime->outstanding_tasks >= runtime->ring_capacity) {
        if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CANCEL &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_ASYNC_CANCEL) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CANCEL &&
        (client->cancel_target_id == IOURINGD_TASK_ID_INVALID ||
         client->cancel_target_id >= IOURINGD_TASK_ID_RESERVED_MIN)) {
        if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if ((client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ ||
         client->request.task_kind.value == IOURINGD_TASK_KIND_FILE_READ) &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_READV) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if ((client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE ||
         client->request.task_kind.value == IOURINGD_TASK_KIND_FILE_WRITE) &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_WRITEV) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_POLL &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_POLL) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CONNECT &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_CONNECT) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_ACCEPT &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_ACCEPT) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_OPENAT &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_OPENAT) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CLOSE &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_CLOSE) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ_FIXED &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_READ_FIXED) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE_FIXED &&
        (runtime->capabilities.op_mask & IOURINGD_CAPABILITY_OP_WRITE_FIXED) == 0u) {
        if (iouringd_reject_submit(runtime, client, -ENOTSUP) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if ((client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ ||
         client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE ||
         client->request.task_kind.value == IOURINGD_TASK_KIND_FILE_READ ||
         client->request.task_kind.value == IOURINGD_TASK_KIND_FILE_WRITE) &&
        (client->resource_id == IOURINGD_RESOURCE_ID_INVALID ||
         client->io_length == 0u || client->io_length > client->io_bytes_max)) {
        if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_POLL &&
        (client->resource_id == IOURINGD_RESOURCE_ID_INVALID ||
         client->poll_mask == 0u)) {
        if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CONNECT &&
        (client->resource_id == IOURINGD_RESOURCE_ID_INVALID ||
         client->io_length == 0u || client->io_length > client->io_bytes_max ||
         client->io_length > UINT16_MAX)) {
        if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_ACCEPT &&
        (client->resource_id == IOURINGD_RESOURCE_ID_INVALID ||
         client->io_length > client->io_bytes_max ||
         client->io_length > UINT16_MAX)) {
        if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_OPENAT &&
        (client->io_length < 2u || client->io_length > client->io_bytes_max ||
         client->io_payload[client->io_length - 1u] != '\0')) {
        if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CLOSE &&
        client->resource_id == IOURINGD_RESOURCE_ID_INVALID) {
        if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if ((client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ_FIXED ||
         client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE_FIXED) &&
        (client->resource_id == IOURINGD_RESOURCE_ID_INVALID ||
         client->buffer_resource_id == IOURINGD_RESOURCE_ID_INVALID ||
         client->io_length == 0u || client->io_length > client->io_bytes_max)) {
        if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_OPENAT) {
        struct iouringd_resource_slot *result_file_resource;
        iouringd_resource_id_t result_resource_id;
        size_t result_file_slot_index;

        if (runtime->next_task_id >= IOURINGD_TASK_ID_RESERVED_MIN) {
            if (iouringd_reject_submit(runtime, client, -EOVERFLOW) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        result_file_resource = iouringd_find_free_file_slot(client);
        if (result_file_resource == NULL) {
            if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        result_resource_id = iouringd_allocate_resource_id(client);
        if (result_resource_id == IOURINGD_RESOURCE_ID_INVALID) {
            if (iouringd_reject_submit(runtime, client, -EOVERFLOW) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        task = iouringd_find_free_task_slot(runtime);
        if (task == NULL) {
            if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        result_file_slot_index = iouringd_file_slot_index(client, result_file_resource);
        iouringd_clear_task_slot(task);
        task->active = 1;
        task->consumes_credit = uses_credit;
        task->task_id = runtime->next_task_id;
        task->task_kind = client->request.task_kind.value;
        task->submit_priority = client->request.priority;
        task->client_index = client_index;
        task->client_generation = client->generation;
        task->result_file_slot_index = (int)result_file_slot_index;
        task->result_resource_id = result_resource_id;
        task->open_flags = client->open_flags;
        task->open_mode = client->open_mode;
        task->io_length = client->io_length;
        memcpy(task->io_buffer, client->io_payload, task->io_length);

        result_file_resource->resource_id = result_resource_id;
        result_file_resource->daemon_fd = -1;
        result_file_resource->registered = 0;
        result_file_resource->closing = 0;
        result_file_resource->in_flight = 1u;

        if (iouringd_ring_submit_openat(&runtime->ring,
                                        task->task_id,
                                        AT_FDCWD,
                                        (const char *)task->io_buffer,
                                        task->open_flags,
                                        task->open_mode,
                                        task->submit_priority) != 0) {
            int saved_errno = errno;

            if (result_file_resource->in_flight != 0u) {
                result_file_resource->in_flight -= 1u;
            }
            iouringd_clear_file_slot(result_file_resource);
            iouringd_unallocate_resource_id(client, result_resource_id);
            iouringd_clear_task_slot(task);
            if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        runtime->next_task_id += 1u;
        runtime->outstanding_tasks += 1u;
        runtime->outstanding_credit_tasks += 1u;
        client->outstanding_tasks += 1u;
        client->outstanding_credit_tasks += 1u;
        runtime->accepted_submits += 1u;
        if (iouringd_queue_submit_result(runtime,
                                         client,
                                         task->task_id,
                                         IOURINGD_COMPLETION_RES_OK) != 0) {
            runtime->outstanding_tasks -= 1u;
            runtime->outstanding_credit_tasks -= 1u;
            client->outstanding_tasks -= 1u;
            client->outstanding_credit_tasks -= 1u;
            if (result_file_resource->in_flight != 0u) {
                result_file_resource->in_flight -= 1u;
            }
            iouringd_clear_file_slot(result_file_resource);
            iouringd_clear_task_slot(task);
            return -1;
        }

        iouringd_emit_trace_event(runtime,
                                  client_index,
                                  IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT,
                                  task->task_kind,
                                  task->submit_priority,
                                  task->task_id,
                                  task->resource_id,
                                  IOURINGD_COMPLETION_RES_OK,
                                  iouringd_available_credits(runtime),
                                  iouringd_trace_bytes_for_client_request(client));
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_CLOSE) {
        size_t file_slot_index;
        int close_fd;

        file_resource = iouringd_find_file_slot(client, client->resource_id);
        if (file_resource == NULL || file_resource->closing != 0) {
            if (iouringd_reject_submit(runtime, client, -ENOENT) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        if (file_resource->in_flight != 0u || file_resource->daemon_fd < 0) {
            if (iouringd_reject_submit(runtime, client, -EBUSY) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        if (runtime->next_task_id >= IOURINGD_TASK_ID_RESERVED_MIN) {
            if (iouringd_reject_submit(runtime, client, -EOVERFLOW) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        task = iouringd_find_free_task_slot(runtime);
        if (task == NULL) {
            if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        file_slot_index = iouringd_file_slot_index(client, file_resource);
        close_fd = file_resource->daemon_fd;
        if (iouringd_ring_update_registered_file(&runtime->ring,
                                                 iouringd_global_file_index(runtime,
                                                                            client_index,
                                                                            file_slot_index),
                                                 -1) != 0) {
            return -1;
        }

        iouringd_clear_task_slot(task);
        task->active = 1;
        task->consumes_credit = uses_credit;
        task->task_id = runtime->next_task_id;
        task->task_kind = client->request.task_kind.value;
        task->submit_priority = client->request.priority;
        task->client_index = client_index;
        task->client_generation = client->generation;
        task->file_slot_index = (int)file_slot_index;
        task->resource_id = client->resource_id;
        task->close_fd = close_fd;

        file_resource->daemon_fd = -1;
        file_resource->registered = 0;
        file_resource->closing = 1;
        file_resource->in_flight = 1u;

        if (iouringd_ring_submit_close(&runtime->ring,
                                       task->task_id,
                                       close_fd,
                                       task->submit_priority) != 0) {
            int saved_errno = errno;

            file_resource->daemon_fd = close_fd;
            file_resource->registered = 1;
            file_resource->closing = 0;
            file_resource->in_flight = 0u;
            if (iouringd_ring_update_registered_file(&runtime->ring,
                                                     iouringd_global_file_index(runtime,
                                                                                client_index,
                                                                                file_slot_index),
                                                     close_fd) != 0) {
                return -1;
            }
            iouringd_clear_task_slot(task);
            if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        runtime->next_task_id += 1u;
        runtime->outstanding_tasks += 1u;
        runtime->outstanding_credit_tasks += 1u;
        client->outstanding_tasks += 1u;
        client->outstanding_credit_tasks += 1u;
        runtime->accepted_submits += 1u;
        if (iouringd_queue_submit_result(runtime,
                                         client,
                                         task->task_id,
                                         IOURINGD_COMPLETION_RES_OK) != 0) {
            runtime->outstanding_tasks -= 1u;
            runtime->outstanding_credit_tasks -= 1u;
            client->outstanding_tasks -= 1u;
            client->outstanding_credit_tasks -= 1u;
            file_resource->daemon_fd = close_fd;
            file_resource->registered = 1;
            file_resource->closing = 0;
            file_resource->in_flight = 0u;
            if (iouringd_ring_update_registered_file(&runtime->ring,
                                                     iouringd_global_file_index(runtime,
                                                                                client_index,
                                                                                file_slot_index),
                                                     close_fd) != 0) {
                return -1;
            }
            iouringd_clear_task_slot(task);
            return -1;
        }

        iouringd_emit_trace_event(runtime,
                                  client_index,
                                  IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT,
                                  task->task_kind,
                                  task->submit_priority,
                                  task->task_id,
                                  task->resource_id,
                                  IOURINGD_COMPLETION_RES_OK,
                                  iouringd_available_credits(runtime),
                                  iouringd_trace_bytes_for_client_request(client));
        iouringd_finish_client_request(client);
        return 1;
    }

    if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ ||
        client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE ||
        client->request.task_kind.value == IOURINGD_TASK_KIND_FILE_READ ||
        client->request.task_kind.value == IOURINGD_TASK_KIND_FILE_WRITE ||
        client->request.task_kind.value == IOURINGD_TASK_KIND_POLL ||
        client->request.task_kind.value == IOURINGD_TASK_KIND_CONNECT ||
        client->request.task_kind.value == IOURINGD_TASK_KIND_ACCEPT ||
        client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ_FIXED ||
        client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE_FIXED) {
        size_t file_slot_index;

        file_resource = iouringd_find_file_slot(client, client->resource_id);
        if (file_resource == NULL) {
            if (iouringd_reject_submit(runtime, client, -ENOENT) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        file_slot_index = iouringd_file_slot_index(client, file_resource);
        if (file_resource->closing != 0) {
            if (iouringd_reject_submit(runtime, client, -ENOENT) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        if (client->request.task_kind.value == IOURINGD_TASK_KIND_CONNECT &&
            file_resource->daemon_fd < 0) {
            if (iouringd_reject_submit(runtime, client, -EBADF) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        if (client->request.task_kind.value == IOURINGD_TASK_KIND_ACCEPT &&
            file_resource->daemon_fd < 0) {
            if (iouringd_reject_submit(runtime, client, -EBADF) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_READ_FIXED ||
            client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE_FIXED) {
            buffer_resource = iouringd_find_buffer_slot(client, client->buffer_resource_id);
            if (buffer_resource == NULL || buffer_resource->closing != 0) {
                if (iouringd_reject_submit(runtime, client, -ENOENT) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }

            if (client->request.task_kind.value == IOURINGD_TASK_KIND_SOCK_WRITE_FIXED &&
                client->io_length > buffer_resource->valid_length) {
                if (iouringd_reject_submit(runtime, client, -EINVAL) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }
        } else {
            buffer_resource = NULL;
        }

        if (runtime->next_task_id >= IOURINGD_TASK_ID_RESERVED_MIN) {
            if (iouringd_reject_submit(runtime, client, -EOVERFLOW) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        task = iouringd_find_free_task_slot(runtime);
        if (task == NULL) {
            if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }

        iouringd_clear_task_slot(task);
        task->active = 1;
        task->consumes_credit = uses_credit;
        task->task_id = runtime->next_task_id;
        task->task_kind = client->request.task_kind.value;
        task->submit_priority = client->request.priority;
        task->client_index = client_index;
        task->client_generation = client->generation;
        task->file_slot_index = (int)file_slot_index;
        task->resource_id = client->resource_id;
        task->buffer_resource_id = client->buffer_resource_id;
        task->io_length = client->io_length;
        task->poll_mask = client->poll_mask;
        task->sockaddr_length = (socklen_t)client->io_length;
        task->io_vector.iov_base = task->io_buffer;
        task->io_vector.iov_len = task->io_length;
        file_resource->in_flight += 1u;
        if (buffer_resource != NULL) {
            task->buffer_slot_index = (int)iouringd_buffer_slot_index(client, buffer_resource);
            buffer_resource->in_flight += 1u;
        }

        if (task->task_kind == IOURINGD_TASK_KIND_ACCEPT) {
            struct iouringd_resource_slot *result_file_resource;
            iouringd_resource_id_t result_resource_id;
            size_t result_file_slot_index;

            result_file_resource = iouringd_find_free_file_slot(client);
            if (result_file_resource == NULL) {
                file_resource->in_flight -= 1u;
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }

            result_resource_id = iouringd_allocate_resource_id(client);
            if (result_resource_id == IOURINGD_RESOURCE_ID_INVALID) {
                file_resource->in_flight -= 1u;
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -EOVERFLOW) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }

            result_file_slot_index =
                iouringd_file_slot_index(client, result_file_resource);
            result_file_resource->resource_id = result_resource_id;
            result_file_resource->daemon_fd = -1;
            result_file_resource->registered = 0;
            result_file_resource->closing = 0;
            result_file_resource->in_flight = 1u;
            task->result_file_slot_index = (int)result_file_slot_index;
            task->result_resource_id = result_resource_id;
        }

        if (task->task_kind == IOURINGD_TASK_KIND_SOCK_WRITE ||
            task->task_kind == IOURINGD_TASK_KIND_FILE_WRITE) {
            memcpy(task->io_buffer, client->io_payload, task->io_length);
            if (iouringd_ring_submit_writev(&runtime->ring,
                                            task->task_id,
                                            iouringd_global_file_index(runtime,
                                                                       client_index,
                                                                       file_slot_index),
                                            &task->io_vector,
                                            1u,
                                            task->submit_priority) != 0) {
                int saved_errno = errno;

                file_resource->in_flight -= 1u;
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }
        } else if (task->task_kind == IOURINGD_TASK_KIND_SOCK_READ ||
                   task->task_kind == IOURINGD_TASK_KIND_FILE_READ) {
            if (iouringd_ring_submit_readv(&runtime->ring,
                                           task->task_id,
                                           iouringd_global_file_index(runtime,
                                                                      client_index,
                                                                      file_slot_index),
                                           &task->io_vector,
                                           1u,
                                           task->submit_priority) != 0) {
                int saved_errno = errno;

                file_resource->in_flight -= 1u;
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }
        } else if (task->task_kind == IOURINGD_TASK_KIND_CONNECT) {
            memcpy(task->io_buffer, client->io_payload, task->io_length);
            if (iouringd_ring_submit_connect(&runtime->ring,
                                             task->task_id,
                                             file_resource->daemon_fd,
                                             task->io_buffer,
                                             task->io_length,
                                             task->submit_priority) != 0) {
                int saved_errno = errno;

                file_resource->in_flight -= 1u;
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }
        } else if (task->task_kind == IOURINGD_TASK_KIND_ACCEPT) {
            if (iouringd_ring_submit_accept(&runtime->ring,
                                            task->task_id,
                                            file_resource->daemon_fd,
                                            task->io_length != 0u ? task->io_buffer : NULL,
                                            task->io_length != 0u ? &task->sockaddr_length : NULL,
                                            SOCK_CLOEXEC | SOCK_NONBLOCK,
                                            task->submit_priority) != 0) {
                struct iouringd_resource_slot *result_file_resource =
                    &client->file_resources[task->result_file_slot_index];
                int saved_errno = errno;

                file_resource->in_flight -= 1u;
                if (result_file_resource->in_flight != 0u) {
                    result_file_resource->in_flight -= 1u;
                }
                iouringd_clear_file_slot(result_file_resource);
                iouringd_unallocate_resource_id(client, task->result_resource_id);
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }
        } else if (task->task_kind == IOURINGD_TASK_KIND_POLL) {
            if (iouringd_ring_submit_poll(&runtime->ring,
                                          task->task_id,
                                          iouringd_global_file_index(runtime,
                                                                     client_index,
                                                                     file_slot_index),
                                          task->poll_mask,
                                          task->submit_priority) != 0) {
                int saved_errno = errno;

                file_resource->in_flight -= 1u;
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }
        } else if (task->task_kind == IOURINGD_TASK_KIND_SOCK_WRITE_FIXED) {
            if (iouringd_ring_submit_write_fixed(
                    &runtime->ring,
                    task->task_id,
                    iouringd_global_file_index(runtime, client_index, file_slot_index),
                    iouringd_global_buffer_index(runtime,
                                                 client_index,
                                                 (size_t)task->buffer_slot_index),
                    iouringd_buffer_storage_at(runtime,
                                               client_index,
                                               (size_t)task->buffer_slot_index),
                    task->io_length,
                    task->submit_priority) != 0) {
                int saved_errno = errno;

                file_resource->in_flight -= 1u;
                if (buffer_resource != NULL) {
                    buffer_resource->in_flight -= 1u;
                }
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }
        } else {
            if (iouringd_ring_submit_read_fixed(
                    &runtime->ring,
                    task->task_id,
                    iouringd_global_file_index(runtime, client_index, file_slot_index),
                    iouringd_global_buffer_index(runtime,
                                                 client_index,
                                                 (size_t)task->buffer_slot_index),
                    iouringd_buffer_storage_at(runtime,
                                               client_index,
                                               (size_t)task->buffer_slot_index),
                    task->io_length,
                    task->submit_priority) != 0) {
                int saved_errno = errno;

                file_resource->in_flight -= 1u;
                if (buffer_resource != NULL) {
                    buffer_resource->in_flight -= 1u;
                }
                iouringd_clear_task_slot(task);
                if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                    return -1;
                }
                iouringd_finish_client_request(client);
                return 1;
            }
        }

        runtime->next_task_id += 1u;
        runtime->outstanding_tasks += 1u;
        runtime->outstanding_credit_tasks += 1u;
        client->outstanding_tasks += 1u;
        client->outstanding_credit_tasks += 1u;
        runtime->accepted_submits += 1u;
        if (iouringd_queue_submit_result(runtime,
                                         client,
                                         task->task_id,
                                         IOURINGD_COMPLETION_RES_OK) != 0) {
            runtime->outstanding_tasks -= 1u;
            runtime->outstanding_credit_tasks -= 1u;
            client->outstanding_tasks -= 1u;
            client->outstanding_credit_tasks -= 1u;
            file_resource->in_flight -= 1u;
            if (task->result_file_slot_index >= 0 &&
                task->result_file_slot_index < (int)client->file_slot_limit) {
                struct iouringd_resource_slot *result_file_resource =
                    &client->file_resources[task->result_file_slot_index];

                if (result_file_resource->in_flight != 0u) {
                    result_file_resource->in_flight -= 1u;
                }
                iouringd_clear_file_slot(result_file_resource);
            }
            if (buffer_resource != NULL) {
                buffer_resource->in_flight -= 1u;
            }
            iouringd_clear_task_slot(task);
            return -1;
        }
        iouringd_emit_trace_event(runtime,
                                  client_index,
                                  IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT,
                                  task->task_kind,
                                  task->submit_priority,
                                  task->task_id,
                                  task->resource_id,
                                  IOURINGD_COMPLETION_RES_OK,
                                  iouringd_available_credits(runtime),
                                  iouringd_trace_bytes_for_client_request(client));
        iouringd_finish_client_request(client);
        return 1;
    }

    if (runtime->next_task_id >= IOURINGD_TASK_ID_RESERVED_MIN) {
        if (iouringd_reject_submit(runtime, client, -EOVERFLOW) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    task = iouringd_find_free_task_slot(runtime);
    if (task == NULL) {
        if (iouringd_reject_submit(runtime, client, -EAGAIN) != 0) {
            return -1;
        }
        iouringd_finish_client_request(client);
        return 1;
    }

    iouringd_clear_task_slot(task);
    task->active = 1;
    task->consumes_credit = uses_credit;
    task->task_id = runtime->next_task_id;
    task->task_kind = client->request.task_kind.value;
    task->submit_priority = client->request.priority;
    task->client_index = client_index;
    task->client_generation = client->generation;

    if (task->task_kind == IOURINGD_TASK_KIND_TIMEOUT) {
        task->timeout.tv_sec =
            (int64_t)(client->timeout_nsec / UINT64_C(1000000000));
        task->timeout.tv_nsec =
            (long)(client->timeout_nsec % UINT64_C(1000000000));
        if (iouringd_ring_submit_timeout(&runtime->ring,
                                         task->task_id,
                                         &task->timeout,
                                         task->submit_priority) != 0) {
            int saved_errno = errno;

            iouringd_clear_task_slot(task);
            if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }
    } else if (task->task_kind == IOURINGD_TASK_KIND_CANCEL) {
        task->cancel_target_id = client->cancel_target_id;
        if (iouringd_ring_submit_cancel(&runtime->ring,
                                        task->task_id,
                                        task->cancel_target_id,
                                        task->submit_priority) != 0) {
            int saved_errno = errno;

            iouringd_clear_task_slot(task);
            if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }
    } else {
        if (iouringd_ring_submit_nop(&runtime->ring,
                                     task->task_id,
                                     task->submit_priority) != 0) {
            int saved_errno = errno;

            iouringd_clear_task_slot(task);
            if (iouringd_reject_submit(runtime, client, -saved_errno) != 0) {
                return -1;
            }
            iouringd_finish_client_request(client);
            return 1;
        }
    }

    runtime->next_task_id += 1u;
    runtime->outstanding_tasks += 1u;
    client->outstanding_tasks += 1u;
    if (uses_credit != 0) {
        runtime->outstanding_credit_tasks += 1u;
        client->outstanding_credit_tasks += 1u;
    }
    runtime->accepted_submits += 1u;

    if (iouringd_queue_submit_result(runtime,
                                     client,
                                     task->task_id,
                                     IOURINGD_COMPLETION_RES_OK) != 0) {
        runtime->outstanding_tasks -= 1u;
        client->outstanding_tasks -= 1u;
        if (uses_credit != 0) {
            runtime->outstanding_credit_tasks -= 1u;
            client->outstanding_credit_tasks -= 1u;
        }
        iouringd_clear_task_slot(task);
        return -1;
    }

    iouringd_emit_trace_event(runtime,
                              client_index,
                              IOURINGD_TRACE_EVENT_SUBMIT_ACCEPT,
                              task->task_kind,
                              task->submit_priority,
                              task->task_id,
                              task->resource_id,
                              IOURINGD_COMPLETION_RES_OK,
                              iouringd_available_credits(runtime),
                              iouringd_trace_bytes_for_client_request(client));
    iouringd_finish_client_request(client);
    return 1;
}

static int iouringd_dispatch_ready_requests(struct iouringd_runtime *runtime)
{
    static const int dispatch_bands[] = {3, 2, 1, 0};
    int progress = 0;
    size_t band_index;

    for (band_index = 0; band_index < sizeof(dispatch_bands) / sizeof(dispatch_bands[0]);
         ++band_index) {
        int dispatch_band = dispatch_bands[band_index];
        size_t offset;

        for (offset = 0; offset < runtime->limits.max_clients; ++offset) {
            const struct iouringd_client *client;
            int client_band;
            int rc;
            size_t index =
                (runtime->dispatch_cursor + offset) % runtime->limits.max_clients;

            client = &runtime->clients[index];
            if (!iouringd_client_is_active(client) || client->request_ready == 0) {
                continue;
            }

            if (client->control_op != 0u) {
                client_band = 3;
            } else if (client->request.priority == IOURINGD_SUBMIT_PRIORITY_HIGH) {
                client_band = 2;
            } else if (client->request.priority == IOURINGD_SUBMIT_PRIORITY_NORMAL) {
                client_band = 1;
            } else {
                client_band = 0;
            }

            if (client_band != dispatch_band) {
                continue;
            }

            rc = iouringd_dispatch_client_request(runtime, index);
            if (rc < 0) {
                return -1;
            }

            if (rc != 0) {
                progress = 1;
                runtime->dispatch_cursor =
                    (index + 1u) % runtime->limits.max_clients;
            }
        }
    }

    return progress;
}

static int iouringd_service_ready_inputs(struct iouringd_runtime *runtime)
{
    int progress = 0;
    size_t index;

    for (index = 0; index < runtime->limits.max_clients; ++index) {
        int rc;

        if (!iouringd_client_is_active(&runtime->clients[index])) {
            continue;
        }

        rc = iouringd_service_client_readable(runtime, index);
        if (rc < 0) {
            return -1;
        }

        progress |= rc;
    }

    return progress;
}

static void iouringd_prepare_accept_completion(
    struct iouringd_runtime *runtime,
    const struct iouringd_task_slot *task,
    struct iouringd_completion_record_v1 *completion,
    const unsigned char **payload)
{
    struct iouringd_client *client;
    struct iouringd_resource_slot *result_slot;
    int accepted_fd;

    if (task->task_kind != IOURINGD_TASK_KIND_ACCEPT || completion->res < 0) {
        return;
    }

    accepted_fd = completion->res;
    if (task->client_index >= runtime->limits.max_clients ||
        task->result_file_slot_index < 0) {
        close(accepted_fd);
        completion->res = -EIO;
        return;
    }

    client = &runtime->clients[task->client_index];
    if (task->result_file_slot_index >= (int)client->file_slot_limit) {
        close(accepted_fd);
        completion->res = -EIO;
        return;
    }

    if (task->sockaddr_length > task->io_length ||
        task->result_resource_id == IOURINGD_RESOURCE_ID_INVALID ||
        task->result_resource_id > (iouringd_resource_id_t)INT32_MAX) {
        close(accepted_fd);
        completion->res = -EPROTO;
        return;
    }

    result_slot = &client->file_resources[task->result_file_slot_index];
    if (client->fd < 0 || client->generation != task->client_generation) {
        close(accepted_fd);
        return;
    }

    if (iouringd_ring_update_registered_file(
            &runtime->ring,
            iouringd_global_file_index(runtime,
                                       task->client_index,
                                       (size_t)task->result_file_slot_index),
            accepted_fd) != 0) {
        int saved_errno = errno;

        close(accepted_fd);
        completion->res = -saved_errno;
        return;
    }

    result_slot->resource_id = task->result_resource_id;
    result_slot->daemon_fd = accepted_fd;
    result_slot->registered = 1;
    result_slot->closing = 0;
    completion->res = (int32_t)task->result_resource_id;
    if (task->sockaddr_length != 0u) {
        completion->payload_length = (uint16_t)task->sockaddr_length;
        *payload = task->io_buffer;
    }
}

static void iouringd_prepare_openat_completion(
    struct iouringd_runtime *runtime,
    const struct iouringd_task_slot *task,
    struct iouringd_completion_record_v1 *completion)
{
    struct iouringd_client *client;
    struct iouringd_resource_slot *result_slot;
    int opened_fd;

    if (task->task_kind != IOURINGD_TASK_KIND_OPENAT || completion->res < 0) {
        return;
    }

    opened_fd = completion->res;
    if (task->client_index >= runtime->limits.max_clients ||
        task->result_file_slot_index < 0) {
        close(opened_fd);
        completion->res = -EIO;
        return;
    }

    client = &runtime->clients[task->client_index];
    if (task->result_file_slot_index >= (int)client->file_slot_limit ||
        task->result_resource_id == IOURINGD_RESOURCE_ID_INVALID ||
        task->result_resource_id > (iouringd_resource_id_t)INT32_MAX) {
        close(opened_fd);
        completion->res = -EPROTO;
        return;
    }

    result_slot = &client->file_resources[task->result_file_slot_index];
    if (client->fd < 0 || client->generation != task->client_generation) {
        close(opened_fd);
        return;
    }

    if (iouringd_ring_update_registered_file(
            &runtime->ring,
            iouringd_global_file_index(runtime,
                                       task->client_index,
                                       (size_t)task->result_file_slot_index),
            opened_fd) != 0) {
        int saved_errno = errno;

        close(opened_fd);
        completion->res = -saved_errno;
        return;
    }

    result_slot->resource_id = task->result_resource_id;
    result_slot->daemon_fd = opened_fd;
    result_slot->registered = 1;
    result_slot->closing = 0;
    completion->res = (int32_t)task->result_resource_id;
}

static int iouringd_complete_task(struct iouringd_runtime *runtime,
                                  struct iouringd_task_slot *task,
                                  int32_t res)
{
    struct iouringd_completion_record_v1 completion;
    const unsigned char *payload = NULL;
    iouringd_resource_id_t resource_id = task->resource_id;

    memset(&completion, 0, sizeof(completion));
    completion.header.magic = IOURINGD_PROTOCOL_WIRE_MAGIC;
    completion.header.version_major = IOURINGD_PROTOCOL_V1_MAJOR;
    completion.header.version_minor = IOURINGD_PROTOCOL_V1_MINOR;
    completion.task.task_id = task->task_id;
    completion.res = res;
    completion.task_kind.value = task->task_kind;
    if ((task->task_kind == IOURINGD_TASK_KIND_SOCK_READ ||
         task->task_kind == IOURINGD_TASK_KIND_SOCK_READ_FIXED ||
         task->task_kind == IOURINGD_TASK_KIND_FILE_READ) &&
        res > 0) {
        completion.payload_length = (uint16_t)res;
    }

    iouringd_prepare_accept_completion(runtime, task, &completion, &payload);
    iouringd_prepare_openat_completion(runtime, task, &completion);

        if (task->client_index < runtime->limits.max_clients) {
            struct iouringd_client *client = &runtime->clients[task->client_index];

        if ((task->task_kind == IOURINGD_TASK_KIND_SOCK_READ ||
             task->task_kind == IOURINGD_TASK_KIND_FILE_READ) &&
            res > 0) {
            payload = task->io_buffer;
        } else if (task->task_kind == IOURINGD_TASK_KIND_SOCK_READ_FIXED &&
                   task->buffer_slot_index >= 0 &&
                   task->buffer_slot_index < (int)client->buffer_slot_limit) {
            struct iouringd_buffer_slot *slot =
                &client->buffer_resources[task->buffer_slot_index];

            if (res >= 0) {
                slot->valid_length = (uint32_t)res;
            }
            if (res > 0) {
                payload = iouringd_buffer_storage_at(runtime,
                                                    task->client_index,
                                                    (size_t)task->buffer_slot_index);
            }
        }

        if (client->fd >= 0 && client->generation == task->client_generation) {
            if (iouringd_queue_client_output(client, &completion, sizeof(completion)) != 0) {
                return -1;
            }
            if (completion.payload_length != 0u && payload != NULL &&
                iouringd_queue_client_output(client,
                                             payload,
                                             completion.payload_length) != 0) {
                return -1;
            }
        }

        if (task->file_slot_index >= 0 &&
            task->file_slot_index < (int)client->file_slot_limit) {
            struct iouringd_resource_slot *slot =
                &client->file_resources[task->file_slot_index];

            if (slot->in_flight != 0u) {
                slot->in_flight -= 1u;
            }
            if (slot->closing != 0 &&
                iouringd_try_release_file_slot(runtime,
                                               client,
                                               task->client_index,
                                               (size_t)task->file_slot_index) != 0) {
                return -1;
            }
        }

        if (task->result_file_slot_index >= 0 &&
            task->result_file_slot_index < (int)client->file_slot_limit) {
            struct iouringd_resource_slot *slot =
                &client->file_resources[task->result_file_slot_index];

            if (slot->in_flight != 0u) {
                slot->in_flight -= 1u;
            }
            if ((slot->registered == 0 || slot->closing != 0) &&
                iouringd_try_release_file_slot(runtime,
                                               client,
                                               task->client_index,
                                               (size_t)task->result_file_slot_index) != 0) {
                return -1;
            }
        }

        if (task->buffer_slot_index >= 0 &&
            task->buffer_slot_index < (int)client->buffer_slot_limit) {
            struct iouringd_buffer_slot *slot =
                &client->buffer_resources[task->buffer_slot_index];

            if (slot->in_flight != 0u) {
                slot->in_flight -= 1u;
            }
            if (slot->closing != 0 &&
                iouringd_try_release_buffer_slot(client,
                                                 (size_t)task->buffer_slot_index) != 0) {
                return -1;
            }
        }
    }

    if (task->consumes_credit != 0) {
        runtime->outstanding_credit_tasks -= 1u;
        if (task->client_index < runtime->limits.max_clients) {
            struct iouringd_client *client = &runtime->clients[task->client_index];

            if (client->outstanding_credit_tasks != 0u) {
                client->outstanding_credit_tasks -= 1u;
            }
        }
    }
    runtime->completions += 1u;
    runtime->outstanding_tasks -= 1u;
    if (task->client_index < runtime->limits.max_clients) {
        struct iouringd_client *client = &runtime->clients[task->client_index];

        if (client->outstanding_tasks != 0u) {
            client->outstanding_tasks -= 1u;
        }
    }
    if ((task->task_kind == IOURINGD_TASK_KIND_ACCEPT ||
         task->task_kind == IOURINGD_TASK_KIND_OPENAT) &&
        completion.res >= 0) {
        resource_id = task->result_resource_id;
    }
    iouringd_emit_trace_event(runtime,
                              task->client_index,
                              IOURINGD_TRACE_EVENT_COMPLETION,
                              task->task_kind,
                              task->submit_priority,
                              task->task_id,
                              resource_id,
                              completion.res,
                              iouringd_available_credits(runtime),
                              iouringd_trace_bytes_for_completion(task->task_kind,
                                                                  completion.res));
    iouringd_clear_task_slot(task);
    return 1;
}

static int iouringd_sweep_closing_resources(struct iouringd_runtime *runtime)
{
    int progress = 0;
    size_t client_index;

    for (client_index = 0; client_index < runtime->limits.max_clients; ++client_index) {
        struct iouringd_client *client = &runtime->clients[client_index];
        size_t slot_index;

        for (slot_index = 0; slot_index < client->file_slot_limit; ++slot_index) {
            const struct iouringd_resource_slot *slot = &client->file_resources[slot_index];

            if (slot->closing == 0 || slot->in_flight != 0u) {
                continue;
            }

            if (iouringd_try_release_file_slot(runtime,
                                               client,
                                               client_index,
                                               slot_index) != 0) {
                return -1;
            }
            progress = 1;
        }

        for (slot_index = 0; slot_index < client->buffer_slot_limit; ++slot_index) {
            const struct iouringd_buffer_slot *slot = &client->buffer_resources[slot_index];

            if (slot->closing == 0 || slot->in_flight != 0u) {
                continue;
            }

            if (iouringd_try_release_buffer_slot(client, slot_index) != 0) {
                return -1;
            }
            progress = 1;
        }
    }

    return progress;
}

static int iouringd_reap_completions(struct iouringd_runtime *runtime)
{
    int progress = 0;

    for (;;) {
        int available;
        int32_t res;
        iouringd_task_id_t task_id;
        struct iouringd_task_slot *task;

        if (iouringd_ring_peek_completion(&runtime->ring,
                                          &task_id,
                                          &res,
                                          &available) != 0) {
            return -1;
        }

        if (available == 0) {
            return progress;
        }

        task = iouringd_find_task_slot(runtime, task_id);
        if (task == NULL) {
            errno = EPROTO;
            return -1;
        }

        if (iouringd_complete_task(runtime, task, res) < 0) {
            return -1;
        }
        progress = 1;
    }
}

static int iouringd_flush_client_outputs(struct iouringd_runtime *runtime)
{
    int progress = 0;
    size_t index;

    for (index = 0; index < runtime->limits.max_clients; ++index) {
        struct iouringd_client *client = &runtime->clients[index];

        if (!iouringd_client_is_active(client) ||
            !iouringd_client_has_output(client)) {
            continue;
        }

        if (iouringd_flush_client_output(client) != 0) {
            iouringd_close_client(client);
            progress = 1;
            continue;
        }

        if (client->close_after_flush != 0) {
            iouringd_close_client(client);
            progress = 1;
            continue;
        }
    }

    return progress;
}

static int iouringd_client_wants_read(const struct iouringd_client *client)
{
    if (client->fd < 0 || client->close_after_flush != 0 ||
        client->request_ready != 0) {
        return 0;
    }

    if (client->phase == IOURINGD_CLIENT_PHASE_HANDSHAKE) {
        return 1;
    }

    if (client->input_len != 0) {
        return 1;
    }

    return 1;
}

static nfds_t iouringd_build_pollfds(const struct iouringd_runtime *runtime,
                                     struct pollfd *pollfds,
                                     int *client_slots)
{
    nfds_t count = 0;
    size_t index;

    if (runtime->server_fd >= 0) {
        pollfds[count].fd = runtime->server_fd;
        pollfds[count].events = POLLIN;
        pollfds[count].revents = 0;
        client_slots[count] = -1;
        count += 1u;
    }

    if (runtime->outstanding_tasks != 0u) {
        pollfds[count].fd = runtime->ring.ring_fd;
        pollfds[count].events = POLLIN;
        pollfds[count].revents = 0;
        client_slots[count] = -2;
        count += 1u;
    }

    for (index = 0; index < runtime->limits.max_clients; ++index) {
        const struct iouringd_client *client = &runtime->clients[index];
        short events = 0;

        if (!iouringd_client_is_active(client)) {
            continue;
        }

        if (iouringd_client_wants_read(client) != 0) {
            events |= POLLIN;
        }

        if (iouringd_client_has_output(client)) {
            events |= POLLOUT;
        }

        if (events == 0) {
            continue;
        }

        pollfds[count].fd = client->fd;
        pollfds[count].events = events;
        pollfds[count].revents = 0;
        client_slots[count] = (int)index;
        count += 1u;
    }

    return count;
}

static int iouringd_run(struct iouringd_runtime *runtime)
{
    struct pollfd pollfds[IOURINGD_MAX_CLIENTS + 2];
    int client_slots[IOURINGD_MAX_CLIENTS + 2];

    for (;;) {
        int progress = 0;
        int rc;
        nfds_t nfds;
        nfds_t index;

        rc = iouringd_service_ready_inputs(runtime);
        if (rc < 0) {
            return -1;
        }
        progress |= rc;

        rc = iouringd_dispatch_ready_requests(runtime);
        if (rc < 0) {
            return -1;
        }
        progress |= rc;

        rc = iouringd_reap_completions(runtime);
        if (rc < 0) {
            return -1;
        }
        progress |= rc;

        rc = iouringd_sweep_closing_resources(runtime);
        if (rc < 0) {
            return -1;
        }
        progress |= rc;

        progress |= iouringd_flush_client_outputs(runtime);

        if (iouringd_stop_requested != 0) {
            break;
        }

        if (progress != 0) {
            continue;
        }

        nfds = iouringd_build_pollfds(runtime, pollfds, client_slots);
        if (nfds == 0u) {
            continue;
        }

        rc = poll(pollfds, nfds, -1);
        if (rc < 0) {
            if (iouringd_stop_requested != 0 && errno == EINTR) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        for (index = 0; index < nfds; ++index) {
            short revents = pollfds[index].revents;
            int client_index = client_slots[index];

            if (revents == 0) {
                continue;
            }

            if (client_index == -1) {
                if ((revents & POLLIN) != 0) {
                    rc = iouringd_accept_clients(runtime);
                    if (rc < 0) {
                        return -1;
                    }
                }
                continue;
            }

            if (client_index == -2) {
                if ((revents & POLLIN) != 0) {
                    rc = iouringd_reap_completions(runtime);
                    if (rc < 0) {
                        return -1;
                    }
                }
                continue;
            }

            if ((revents & POLLOUT) != 0 &&
                iouringd_client_is_active(&runtime->clients[client_index])) {
                if (iouringd_flush_client_output(&runtime->clients[client_index]) != 0) {
                    iouringd_close_client(&runtime->clients[client_index]);
                    continue;
                }

                if (runtime->clients[client_index].close_after_flush != 0) {
                    iouringd_close_client(&runtime->clients[client_index]);
                    continue;
                }
            }

            if ((revents & POLLIN) != 0 &&
                iouringd_client_is_active(&runtime->clients[client_index])) {
                rc = iouringd_service_client_readable(runtime, (size_t)client_index);
                if (rc < 0) {
                    return -1;
                }
                continue;
            }

            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
                iouringd_client_is_active(&runtime->clients[client_index])) {
                iouringd_close_client(&runtime->clients[client_index]);
            }
        }
    }

    return 0;
}

static int iouringd_parse_unsigned_arg(const char *value, unsigned *result)
{
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || result == NULL || *value == '\0') {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    *result = (unsigned)parsed;
    return 0;
}

static int iouringd_parse_args(struct iouringd_runtime *runtime,
                               int argc,
                               char **argv)
{
    int index = 1;

    if (runtime == NULL || argv == NULL) {
        errno = EINVAL;
        return -1;
    }

    while (index < argc - 1) {
        unsigned value;

        if (strcmp(argv[index], "--trace-stderr") == 0) {
            runtime->trace_to_stderr = 1;
            index += 1;
            continue;
        }

        if (index + 1 >= argc - 0) {
            errno = EINVAL;
            return -1;
        }

        if (iouringd_parse_unsigned_arg(argv[index + 1], &value) != 0) {
            return -1;
        }

        if (strcmp(argv[index], "--ring-entries") == 0) {
            runtime->limits.ring_entries = value;
        } else if (strcmp(argv[index], "--max-clients") == 0) {
            runtime->limits.max_clients = value;
        } else if (strcmp(argv[index], "--registered-fds") == 0) {
            runtime->limits.registered_fds_per_client = value;
        } else if (strcmp(argv[index], "--registered-buffers") == 0) {
            runtime->limits.registered_buffers_per_client = value;
        } else if (strcmp(argv[index], "--per-client-credits") == 0) {
            runtime->limits.per_client_credits = value;
        } else if (strcmp(argv[index], "--io-bytes-max") == 0) {
            runtime->limits.io_bytes_max = value;
        } else if (strcmp(argv[index], "--job-id") == 0) {
            runtime->has_job_id = 1;
            runtime->job_id = value;
        } else {
            errno = EINVAL;
            return -1;
        }

        index += 2;
    }

    if (index != argc - 1) {
        errno = EINVAL;
        return -1;
    }

    if (runtime->limits.ring_entries < 2u ||
        runtime->limits.ring_entries > IOURINGD_MAX_TASKS ||
        runtime->limits.max_clients == 0u ||
        runtime->limits.max_clients > IOURINGD_MAX_CLIENTS ||
        runtime->limits.registered_fds_per_client == 0u ||
        runtime->limits.registered_fds_per_client > IOURINGD_MAX_REGISTERED_FDS ||
        runtime->limits.registered_buffers_per_client == 0u ||
        runtime->limits.registered_buffers_per_client > IOURINGD_MAX_REGISTERED_BUFFERS ||
        runtime->limits.per_client_credits == 0u ||
        runtime->limits.per_client_credits > IOURINGD_MAX_TASKS ||
        runtime->limits.io_bytes_max == 0u ||
        runtime->limits.io_bytes_max > IOURINGD_MAX_IO_BYTES) {
        errno = EINVAL;
        return -1;
    }

    runtime->socket_path = argv[index];
    runtime->registered_file_capacity =
        (size_t)runtime->limits.max_clients * runtime->limits.registered_fds_per_client;
    runtime->registered_buffer_capacity =
        (size_t)runtime->limits.max_clients * runtime->limits.registered_buffers_per_client;
    return 0;
}

int main(int argc, char **argv)
{
    struct iouringd_runtime runtime;
    struct sockaddr_un addr;
    struct iovec *iovecs = NULL;
    size_t index;

    if (argc < 2) {
        return 2;
    }

    iouringd_init_runtime(&runtime);
    if (iouringd_parse_args(&runtime, argc, argv) != 0) {
        iouringd_print_usage(stderr);
        return 2;
    }

    if (iouringd_install_signal_handlers() != 0) {
        iouringd_log_errno("installing signal handlers failed");
        return 1;
    }

    if (iouringd_ring_init(&runtime.ring,
                           runtime.limits.ring_entries,
                           &runtime.capabilities) != 0) {
        iouringd_report_ring_init_error(errno);
        return 1;
    }

    runtime.ring_capacity = iouringd_ring_capacity(&runtime.ring);
    if (runtime.ring_capacity <= 1u || runtime.ring_capacity > IOURINGD_MAX_TASKS) {
        fprintf(stderr,
                "iouringd: io_uring ring capacity %zu is outside the supported"
                " range\n",
                runtime.ring_capacity);
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }
    if (iouringd_init_registered_buffer_storage(&runtime) != 0) {
        iouringd_log_errno("allocating registered buffer storage failed");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }
    if (iouringd_ring_init_registered_files(&runtime.ring,
                                            (unsigned)runtime.registered_file_capacity) != 0) {
        iouringd_log_errno("registering fixed file table failed");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }
    iovecs = calloc(runtime.registered_buffer_capacity, sizeof(*iovecs));
    if (iovecs == NULL) {
        iouringd_log_errno("allocating registered buffer iovecs failed");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }
    for (index = 0; index < runtime.registered_buffer_capacity; ++index) {
        iovecs[index].iov_base = runtime.registered_buffer_storage +
                                 index * runtime.limits.io_bytes_max;
        iovecs[index].iov_len = runtime.limits.io_bytes_max;
    }
    if (iouringd_ring_init_registered_buffers(&runtime.ring,
                                              iovecs,
                                              (unsigned)runtime.registered_buffer_capacity) != 0) {
        iouringd_log_errno("registering fixed buffers failed");
        free(iovecs);
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }
    free(iovecs);
    runtime.task_capacity = runtime.ring_capacity - 1u;
    runtime.per_client_credit_limit = runtime.limits.per_client_credits;
    if (runtime.per_client_credit_limit > runtime.task_capacity) {
        runtime.per_client_credit_limit = runtime.task_capacity;
    }
    runtime.capabilities.submit_credits = (uint32_t)runtime.task_capacity;
    runtime.capabilities.registered_fd_slots = runtime.limits.registered_fds_per_client;
    runtime.capabilities.registered_buffer_slots = runtime.limits.registered_buffers_per_client;
    runtime.capabilities.io_bytes_max = runtime.limits.io_bytes_max;
    runtime.client_output_capacity =
        sizeof(struct iouringd_handshake_result_v1) +
        sizeof(struct iouringd_stats_result_v1) +
        sizeof(struct iouringd_trace_result_v1) +
        IOURINGD_TRACE_CAPACITY * sizeof(struct iouringd_trace_event_v1) +
        (runtime.limits.registered_fds_per_client +
         runtime.limits.registered_buffers_per_client) *
            sizeof(struct iouringd_resource_result_v1) +
        runtime.task_capacity *
            (sizeof(struct iouringd_submit_result_v1) +
             sizeof(struct iouringd_completion_record_v1) +
             runtime.limits.io_bytes_max);

    runtime.server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (runtime.server_fd < 0) {
        iouringd_log_errno("creating server socket failed");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }
    iouringd_server_fd = runtime.server_fd;

    if (iouringd_set_cloexec(runtime.server_fd) != 0) {
        iouringd_log_errno("setting server socket CLOEXEC failed");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }

    if (iouringd_set_nonblocking(runtime.server_fd) != 0) {
        iouringd_log_errno("setting server socket nonblocking failed");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }

    if (strlen(runtime.socket_path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "iouringd: socket path is too long\n");
        iouringd_cleanup_runtime(&runtime);
        return 2;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    unlink(runtime.socket_path);
    memcpy(addr.sun_path, runtime.socket_path, strlen(runtime.socket_path) + 1u);

    if (bind(runtime.server_fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        iouringd_log_errno("binding server socket failed");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }

    if (listen(runtime.server_fd, (int)runtime.limits.max_clients) != 0) {
        iouringd_log_errno("listening on server socket failed");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }

    iouringd_log_metrics(&runtime, "ready");
    if (iouringd_run(&runtime) != 0) {
        iouringd_log_errno("event loop failed");
        iouringd_log_metrics(&runtime, "shutdown");
        iouringd_cleanup_runtime(&runtime);
        return 1;
    }

    iouringd_log_metrics(&runtime, "shutdown");
    iouringd_cleanup_runtime(&runtime);
    if (iouringd_stop_requested != 0) {
        return iouringd_finish_stop_signal();
    }

    return 0;
}
