#ifndef SELFIQ_BROKER_PARSER_H
#define SELFIQ_BROKER_PARSER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
/* Valores de protocolo: eBroker=5, eRespBroker=4, IP=0x76, puerto=0x77. */
typedef struct { char ip[16]; uint16_t port; } selfiq_broker_t;
static inline bool selfiq_decode_broker(uint16_t header, uint8_t length,
                                        const uint8_t *body, selfiq_broker_t *out) {
  if (!body || !out || header != 0x5433 || length < 1 || length > 161 || body[0] != 0x54)
    return false;
  selfiq_broker_t tmp = {0};
  bool ip = false, port = false;
  size_t offset = 1;
  while (offset < length) {
    if (length - offset < 2) return false;
    unsigned tag = body[offset++], size = body[offset++];
    if (size > length - offset) return false;
    if (tag == 0x76 && size == 4 && !ip) {
      /* Destino IPv4 unicast; se descartan unspecified, loopback y multicast. */
      if (body[offset] == 0 || body[offset] == 127 || body[offset] >= 224)
        return false;
      snprintf(tmp.ip, sizeof(tmp.ip), "%u.%u.%u.%u", body[offset],
               body[offset+1], body[offset+2], body[offset+3]);
      ip = true;
    } else if (tag == 0x77 && size == 2 && !port) {
      tmp.port = ((uint16_t)body[offset] << 8) | body[offset+1];
      if (!tmp.port) return false;
      port = true;
    } else return false;
    offset += size;
  }
  if (!ip || !port) return false;
  *out = tmp;
  return true;
}
#endif
