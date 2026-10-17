#ifndef SELFIQ_IDENTITY_PARSER_H
#define SELFIQ_IDENTITY_PARSER_H
#include <stdbool.h>
#include <stddef.h>
static inline bool selfiq_valid_node_id(const char *id, size_t size) {
  if (!id || size != 36) return false;
  bool nonzero = false;
  for (size_t i = 0; i < size; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (id[i] != '-') return false;
    } else {
      char c = id[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) return false;
      if (c != '0') nonzero = true;
    }
  }
  return nonzero;
}
#endif
