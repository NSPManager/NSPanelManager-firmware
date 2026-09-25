#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct httpd_req { void *handle; int method; char uri[512]; size_t content_len; void *aux; void *user_ctx; void *sess_ctx; } httpd_req_t;
typedef void *httpd_handle_t;
typedef enum { HTTP_GET = 1, HTTP_POST = 3, HTTP_PUT = 4, HTTP_DELETE = 0, HTTP_ANY = 32 } httpd_method_t;
typedef struct { const char *uri; httpd_method_t method; esp_err_t (*handler)(httpd_req_t *r); void *user_ctx; } httpd_uri_t;
typedef enum { HTTPD_404_NOT_FOUND, HTTPD_500_INTERNAL_SERVER_ERROR, HTTPD_400_BAD_REQUEST, HTTPD_405_METHOD_NOT_ALLOWED } httpd_err_code_t;
typedef struct {
  unsigned task_priority; size_t stack_size; int core_id; uint16_t server_port; uint16_t ctrl_port;
  uint16_t max_open_sockets; uint16_t max_uri_handlers; uint16_t max_resp_headers; uint16_t backlog_conn;
  bool lru_purge_enable; uint16_t recv_wait_timeout; uint16_t send_wait_timeout; void *global_user_ctx;
  bool uri_match_fn; bool enable_so_linger; int linger_timeout;
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){5, 4096, 0, 80, 32768, 7, 8, 8, 5, false, 5, 5, NULL, false, false, 0})
#define HTTPD_RESP_USE_STRLEN (-1)

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri);
esp_err_t httpd_unregister_uri_handler(httpd_handle_t handle, const char *uri, httpd_method_t method);
esp_err_t httpd_register_err_handler(httpd_handle_t handle, httpd_err_code_t error, esp_err_t (*fn)(httpd_req_t *, httpd_err_code_t));
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_send_err(httpd_req_t *r, httpd_err_code_t error, const char *msg);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);
size_t httpd_req_get_url_query_len(httpd_req_t *r);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t buf_len);
esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t val_size);
esp_err_t httpd_resp_send_500(httpd_req_t *r);
esp_err_t httpd_resp_send_404(httpd_req_t *r);
esp_err_t httpd_resp_send_408(httpd_req_t *r);
