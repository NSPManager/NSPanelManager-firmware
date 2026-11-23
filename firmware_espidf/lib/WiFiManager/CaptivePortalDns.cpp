#include <CaptivePortalDns.hpp>
#include <cstring>
#include <esp_log.h>
#include <lwip/sockets.h>

namespace {
constexpr uint16_t DNS_PORT = 53;
constexpr size_t HEADER_SIZE = 12;
constexpr size_t MAX_PACKET = 512; // Classic DNS over UDP
constexpr uint16_t TYPE_A = 1;
constexpr uint16_t CLASS_IN = 1;
constexpr uint32_t ANSWER_TTL = 60;

uint16_t get16(const uint8_t *p) {
  return (uint16_t(p[0]) << 8) | p[1];
}

void put16(uint8_t *p, uint16_t value) {
  p[0] = value >> 8;
  p[1] = value & 0xFF;
}
} // namespace

esp_err_t CaptivePortalDns::start(esp_ip4_addr_t address) {
  CaptivePortalDns::_address = address.addr;
  if (CaptivePortalDns::_running.exchange(true)) {
    return ESP_OK; // Already running, now with the new address
  }
  if (xTaskCreate(CaptivePortalDns::_task, "captive_dns", 3072, NULL, 3, &CaptivePortalDns::_task_handle) != pdPASS) {
    CaptivePortalDns::_running = false;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void CaptivePortalDns::stop() {
  // The task notices within its receive timeout, then closes the socket and deletes itself.
  CaptivePortalDns::_running = false;
}

void CaptivePortalDns::_task(void *param) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in bind_address = {};
  bind_address.sin_family = AF_INET;
  bind_address.sin_port = htons(DNS_PORT);
  bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
  timeval timeout = {.tv_sec = 1, .tv_usec = 0}; // So stop() is noticed
  if (sock < 0 || bind(sock, (sockaddr *)&bind_address, sizeof(bind_address)) != 0 || setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
    ESP_LOGE("WiFiManager", "Failed to start DNS server for captive portal (errno %d). Captive portal popup will not work in client device.", errno);
    if (sock >= 0) {
      close(sock);
    }
    CaptivePortalDns::_running = false;
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI("WiFiManager", "DNS Server for captive portal popup started.");

  uint8_t packet[MAX_PACKET];
  while (CaptivePortalDns::_running) {
    sockaddr_in client = {};
    socklen_t client_length = sizeof(client);
    int length = recvfrom(sock, packet, sizeof(packet), 0, (sockaddr *)&client, &client_length);
    if (length <= 0) {
      continue; // Timeout, check _running again
    }
    size_t response_length = CaptivePortalDns::_build_response(packet, length, sizeof(packet), CaptivePortalDns::_address);
    if (response_length > 0) {
      sendto(sock, packet, response_length, 0, (sockaddr *)&client, client_length);
    }
  }

  close(sock);
  ESP_LOGI("WiFiManager", "DNS Server for captive portal popup stopped.");
  CaptivePortalDns::_task_handle = NULL;
  vTaskDelete(NULL);
}

size_t CaptivePortalDns::_build_response(uint8_t *packet, size_t length, size_t capacity, uint32_t address) {
  if (length < HEADER_SIZE) {
    return 0;
  }
  uint16_t flags = get16(packet + 2);
  bool is_query = (flags & 0x8000) == 0;
  bool standard_query = ((flags >> 11) & 0x0F) == 0;
  if (!is_query || !standard_query || get16(packet + 4) == 0) {
    return 0;
  }

  // Walk the first question's name: labels until a zero length byte.
  size_t offset = HEADER_SIZE;
  while (offset < length && packet[offset] != 0) {
    if ((packet[offset] & 0xC0) != 0) {
      return 0; // Compression is not used in questions
    }
    offset += packet[offset] + 1;
  }
  offset += 1; // The zero length byte
  if (offset + 4 > length) {
    return 0;
  }
  uint16_t type = get16(packet + offset);
  uint16_t qclass = get16(packet + offset + 2);
  size_t question_end = offset + 4;

  // Answer with just the first question, dropping any further questions and EDNS records.
  put16(packet + 2, 0x8000 | (flags & 0x0100) | 0x0080); // Response, recursion desired as asked, recursion available, no error
  put16(packet + 4, 1);                                  // Questions
  put16(packet + 8, 0);                                  // Authority records
  put16(packet + 10, 0);                                 // Additional records

  bool answer = type == TYPE_A && qclass == CLASS_IN;
  put16(packet + 6, answer ? 1 : 0); // Answers. Other types (e.g. AAAA) get none, so clients use the A record.
  if (!answer) {
    return question_end;
  }
  if (question_end + 16 > capacity) {
    return 0;
  }
  uint8_t *record = packet + question_end;
  put16(record, 0xC000 | HEADER_SIZE); // Name: pointer to the name in the question
  put16(record + 2, TYPE_A);
  put16(record + 4, CLASS_IN);
  put16(record + 6, ANSWER_TTL >> 16);
  put16(record + 8, ANSWER_TTL & 0xFFFF);
  put16(record + 10, 4);         // Address length
  memcpy(record + 12, &address, 4); // Already in network byte order
  return question_end + 16;
}
