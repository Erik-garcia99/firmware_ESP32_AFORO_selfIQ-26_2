#ifndef SELFIQ_CREDENTIAL_PARSER_H
#define SELFIQ_CREDENTIAL_PARSER_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* Pure C parser, independently testable without ESP-IDF. No credential logs. */
typedef struct {
    char ssid[33];
    char password[65];
    char username[32];
    uint8_t kind;
} selfiq_credentials_t;

static inline bool selfiq_decode_credentials(uint16_t header, uint8_t length,
                                             const uint8_t *body,
                                             selfiq_credentials_t *output,
                                             const char **error) {
#define REJECT(why) do { if (error) *error = (why); return false; } while (0)
    if (!body || !output || header != 0x5433 || length < 1 || length > 161)
        REJECT("Cabecera o LEN invalido");
    unsigned kind = body[0] >> 4, action = body[0] & 15;
    if (action != 2 || (kind != 1 && kind != 2))
        REJECT("No es respuesta de credenciales (tipo/accion)");
    selfiq_credentials_t temporary = {0};
    temporary.kind = kind;
    size_t offset = 1;
    bool ssid = false, pass = false, user = false;
    while (offset < length) {
        if (length - offset < 2) REJECT("Cabecera TLV incompleta");
        unsigned id = body[offset++], len = body[offset++];
        if (len > length - offset) REJECT("Valor TLV truncado");
        if (memchr(body + offset, 0, len)) REJECT("TLV contiene NUL");
        char *dst;
        if (id == 0x69 && !ssid && len >= 1 && len <= 32) {
            dst = temporary.ssid; ssid = true;
        } else if (id == 0x70 && !pass && len >= 1 && len <= 64) {
            dst = temporary.password; pass = true;
        } else if (id == 0x75 && !user && kind == 2 && len >= 1 && len <= 31) {
            dst = temporary.username; user = true;
        } else REJECT("Campo TLV desconocido, repetido o longitud incorrecta");
        memcpy(dst, body + offset, len);
        offset += len;
    }
    if (!ssid || !pass || (kind == 2 && !user)) REJECT("Faltan campos obligatorios");
    if (kind == 1 && (strlen(temporary.password) < 8 || strlen(temporary.password) > 63))
        REJECT("WPA2 Personal requiere passphrase de 8 a 63 bytes");
    *output = temporary; /* Atomic update of caller result after full validation. */
    if (error) *error = NULL;
    return true;
#undef REJECT
}
#endif
