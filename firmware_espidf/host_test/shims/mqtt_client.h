#pragma once
#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum {
  MQTT_EVENT_ANY = -1,
  MQTT_EVENT_ERROR = 0,
  MQTT_EVENT_CONNECTED,
  MQTT_EVENT_DISCONNECTED,
  MQTT_EVENT_SUBSCRIBED,
  MQTT_EVENT_UNSUBSCRIBED,
  MQTT_EVENT_PUBLISHED,
  MQTT_EVENT_DATA,
  MQTT_EVENT_BEFORE_CONNECT,
  MQTT_EVENT_DELETED,
} esp_mqtt_event_id_t;

typedef enum { MQTT_TRANSPORT_UNKNOWN = 0, MQTT_TRANSPORT_OVER_TCP, MQTT_TRANSPORT_OVER_SSL } esp_mqtt_transport_t;

typedef enum {
  MQTT_ERROR_TYPE_NONE = 0,
  MQTT_ERROR_TYPE_TCP_TRANSPORT,
  MQTT_ERROR_TYPE_CONNECTION_REFUSED,
  MQTT_ERROR_TYPE_SUBSCRIBE_FAILED,
} esp_mqtt_error_type_t;

typedef struct esp_mqtt_error_codes {
  esp_err_t esp_tls_last_esp_err;
  int esp_tls_stack_err;
  int esp_transport_sock_errno;
  esp_mqtt_error_type_t error_type;
} esp_mqtt_error_codes_t;

typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

typedef struct {
  esp_mqtt_client_handle_t client;
  esp_mqtt_event_id_t event_id;
  char *data;
  int data_len;
  int total_data_len;
  int current_data_offset;
  char *topic;
  int topic_len;
  int msg_id;
  bool retain;
  int qos;
  esp_mqtt_error_codes_t *error_handle;
} esp_mqtt_event_t;
typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;

typedef struct {
  struct {
    struct {
      const char *uri;
      const char *hostname;
      esp_mqtt_transport_t transport;
      uint32_t port;
      const char *path;
    } address;
    struct { const char *certificate; } verification;
  } broker;
  struct {
    const char *username;
    const char *client_id;
    bool set_null_client_id;
    struct { const char *password; } authentication;
  } credentials;
  struct {
    struct {
      const char *topic;
      const char *msg;
      int msg_len;
      int qos;
      int retain;
    } last_will;
    bool disable_clean_session;
    int keepalive;
    bool disable_keepalive;
    int message_retransmit_timeout;
  } session;
  struct {
    int priority;
    int stack_size;
  } task;
  struct {
    int size;
    int out_size;
  } buffer;
  struct {
    bool set_null_client_id;
    int network_timeout_ms;
    int refresh_connection_after_ms;
    int reconnect_timeout_ms;
    int timeout_ms; /* abort a network operation that has not finished in this long */
    bool disable_auto_reconnect;
  } network;
} esp_mqtt_client_config_t;

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain);
int esp_mqtt_client_subscribe_single(esp_mqtt_client_handle_t client, const char *topic, int qos);
int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client, const char *topic);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event, esp_event_handler_t handler, void *arg);
esp_err_t esp_mqtt_client_unregister_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event, esp_event_handler_t handler);
