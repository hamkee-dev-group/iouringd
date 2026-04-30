#ifndef IOURINGD_SUBMIT_H
#define IOURINGD_SUBMIT_H

#include "iouringd/api.h"

int iouringd_submit_priority_is_valid(uint16_t priority);
int iouringd_submit_request_set_priority(struct iouringd_submit_request_v1 *request,
                                         uint16_t priority);
uint16_t iouringd_submit_request_priority(
    const struct iouringd_submit_request_v1 *request);

int iouringd_submit_nop_result(int fd,
                               struct iouringd_submit_result_v1 *result);
int iouringd_submit_timeout_result(int fd,
                                   uint64_t timeout_nsec,
                                   struct iouringd_submit_result_v1 *result);
int iouringd_submit_cancel_result(int fd,
                                  iouringd_task_id_t target_task_id,
                                  struct iouringd_submit_result_v1 *result);
int iouringd_submit_sock_read_result(int fd,
                                     iouringd_resource_id_t resource_id,
                                     uint32_t length,
                                     struct iouringd_submit_result_v1 *result);
int iouringd_submit_sock_write_result(int fd,
                                      iouringd_resource_id_t resource_id,
                                      const void *buf,
                                      uint32_t length,
                                      struct iouringd_submit_result_v1 *result);
int iouringd_submit_file_read_result(int fd,
                                     iouringd_resource_id_t resource_id,
                                     uint32_t length,
                                     struct iouringd_submit_result_v1 *result);
int iouringd_submit_file_write_result(int fd,
                                      iouringd_resource_id_t resource_id,
                                      const void *buf,
                                      uint32_t length,
                                      struct iouringd_submit_result_v1 *result);
int iouringd_submit_poll_result(int fd,
                                iouringd_resource_id_t resource_id,
                                uint16_t poll_mask,
                                struct iouringd_submit_result_v1 *result);
int iouringd_submit_connect_result(int fd,
                                   iouringd_resource_id_t resource_id,
                                   const void *sockaddr_buf,
                                   uint32_t sockaddr_length,
                                   struct iouringd_submit_result_v1 *result);
int iouringd_submit_accept_result(int fd,
                                  iouringd_resource_id_t resource_id,
                                  uint32_t sockaddr_length,
                                  struct iouringd_submit_result_v1 *result);
int iouringd_submit_openat_result(int fd,
                                  const char *path,
                                  int32_t open_flags,
                                  uint32_t open_mode,
                                  struct iouringd_submit_result_v1 *result);
int iouringd_submit_close_result(int fd,
                                 iouringd_resource_id_t resource_id,
                                 struct iouringd_submit_result_v1 *result);
int iouringd_submit_sock_read_fixed_result(int fd,
                                           iouringd_resource_id_t file_resource_id,
                                           iouringd_resource_id_t buffer_resource_id,
                                           uint32_t length,
                                           struct iouringd_submit_result_v1 *result);
int iouringd_submit_sock_write_fixed_result(int fd,
                                            iouringd_resource_id_t file_resource_id,
                                            iouringd_resource_id_t buffer_resource_id,
                                            uint32_t length,
                                            struct iouringd_submit_result_v1 *result);
int iouringd_register_fd_result(int fd,
                                int resource_fd,
                                struct iouringd_resource_result_v1 *result);
int iouringd_register_buffer_result(int fd,
                                    const void *buf,
                                    uint32_t length,
                                    struct iouringd_resource_result_v1 *result);
int iouringd_release_resource_result(int fd,
                                     iouringd_resource_id_t resource_id,
                                     struct iouringd_resource_result_v1 *result);
int iouringd_get_stats_result(int fd, struct iouringd_stats_result_v1 *result);
int iouringd_get_trace_result(int fd,
                              uint64_t after_sequence,
                              uint32_t max_events,
                              struct iouringd_trace_result_v1 *result,
                              struct iouringd_trace_event_v1 *events,
                              size_t event_capacity,
                              size_t *event_count);
int iouringd_submit_nop(int fd, struct iouringd_task_id_record_v1 *task);
int iouringd_submit_timeout(int fd,
                            uint64_t timeout_nsec,
                            struct iouringd_task_id_record_v1 *task);
int iouringd_submit_cancel(int fd,
                           iouringd_task_id_t target_task_id,
                           struct iouringd_task_id_record_v1 *task);
int iouringd_submit_sock_read(int fd,
                              iouringd_resource_id_t resource_id,
                              uint32_t length,
                              struct iouringd_task_id_record_v1 *task);
int iouringd_submit_sock_write(int fd,
                               iouringd_resource_id_t resource_id,
                               const void *buf,
                               uint32_t length,
                               struct iouringd_task_id_record_v1 *task);
int iouringd_submit_file_read(int fd,
                              iouringd_resource_id_t resource_id,
                              uint32_t length,
                              struct iouringd_task_id_record_v1 *task);
int iouringd_submit_file_write(int fd,
                               iouringd_resource_id_t resource_id,
                               const void *buf,
                               uint32_t length,
                               struct iouringd_task_id_record_v1 *task);
int iouringd_submit_poll(int fd,
                         iouringd_resource_id_t resource_id,
                         uint16_t poll_mask,
                         struct iouringd_task_id_record_v1 *task);
int iouringd_submit_connect(int fd,
                            iouringd_resource_id_t resource_id,
                            const void *sockaddr_buf,
                            uint32_t sockaddr_length,
                            struct iouringd_task_id_record_v1 *task);
int iouringd_submit_accept(int fd,
                           iouringd_resource_id_t resource_id,
                           uint32_t sockaddr_length,
                           struct iouringd_task_id_record_v1 *task);
int iouringd_submit_openat(int fd,
                           const char *path,
                           int32_t open_flags,
                           uint32_t open_mode,
                           struct iouringd_task_id_record_v1 *task);
int iouringd_submit_close(int fd,
                          iouringd_resource_id_t resource_id,
                          struct iouringd_task_id_record_v1 *task);
int iouringd_submit_sock_read_fixed(int fd,
                                    iouringd_resource_id_t file_resource_id,
                                    iouringd_resource_id_t buffer_resource_id,
                                    uint32_t length,
                                    struct iouringd_task_id_record_v1 *task);
int iouringd_submit_sock_write_fixed(int fd,
                                     iouringd_resource_id_t file_resource_id,
                                     iouringd_resource_id_t buffer_resource_id,
                                     uint32_t length,
                                     struct iouringd_task_id_record_v1 *task);
int iouringd_register_fd(int fd,
                         int resource_fd,
                         struct iouringd_resource_id_record_v1 *resource);
int iouringd_register_buffer(int fd,
                             const void *buf,
                             uint32_t length,
                             struct iouringd_resource_id_record_v1 *resource);
int iouringd_release_resource(int fd,
                              iouringd_resource_id_t resource_id,
                              struct iouringd_resource_id_record_v1 *resource);
int iouringd_get_stats(int fd, struct iouringd_stats_result_v1 *result);
int iouringd_get_trace(int fd,
                       uint64_t after_sequence,
                       uint32_t max_events,
                       struct iouringd_trace_result_v1 *result,
                       struct iouringd_trace_event_v1 *events,
                       size_t event_capacity,
                       size_t *event_count);
int iouringd_wait_completion(int fd, struct iouringd_completion_record_v1 *completion);
int iouringd_read_completion_payload(int fd,
                                     const struct iouringd_completion_record_v1 *completion,
                                     void *buf,
                                     size_t buf_capacity,
                                     size_t *payload_length);

#endif
