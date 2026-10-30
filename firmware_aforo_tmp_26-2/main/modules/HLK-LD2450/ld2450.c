/* *********************************************
 *              selfIQ 2026-2
 *  Driver HLK-LD2450 - Implementación
 ************************************************* */

#include "ld2450.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <math.h>
#include <string.h>

#define LD2450_CONFIRM_FRAMES  5    // ~0.5 s a 10 Hz; suficiente para una puerta
#define LD2450_PI              3.14159265358979323846f

static const char *TAG = "LD2450";

// Buffer circular simple para ir acumulando bytes del UART
static uint8_t  s_uart_buf[LD2450_UART_BUF_SIZE];
static size_t   s_buf_len = 0;

// ── Tracker de cruce de puerta (entradas/salidas) ──────────────────────
// El LD2450 no da IDs estables: emparejamos frame a frame por cercanía.
#define MAX_TRACKS        4
#define TRACK_GATE_MM     400   // distancia máxima para asociar un objetivo a un track
#define TRACK_TIMEOUT     3     // frames sin ver un track → se borra
#define CROSS_IN_MM       150   // banda "dentro" respecto a la línea de la puerta
#define CROSS_OUT_MM      150   // banda "fuera"

typedef struct {
    bool   active;
    int16_t x, y;
    int    state;        // 0=desconocido, 1=fuera, 2=dentro
    int    last_seen;    // índice de frame
    int    cooldown;     // frames restantes sin contar
} track_t;

static track_t s_tracks[MAX_TRACKS];
static int     s_frame_idx   = 0;
static int     s_entries     = 0;
static int     s_exits       = 0;
static int     s_occupancy   = 0;

static void (*s_crossing_cb)(bool entry) = NULL;

static void aforo_load_nvs(void) {
    nvs_handle_t h;
    if (nvs_open("aforo", NVS_READONLY, &h) != ESP_OK) return;
    int32_t e = 0, x = 0, o = 0;
    nvs_get_i32(h, "entries", &e);
    nvs_get_i32(h, "exits", &x);
    nvs_get_i32(h, "occ", &o);
    nvs_close(h);
    s_entries   = (int)e;
    s_exits     = (int)x;
    s_occupancy = (int)o;
    if (s_occupancy < 0) s_occupancy = 0;
}

static void aforo_save_nvs(void) {
    nvs_handle_t h;
    if (nvs_open("aforo", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "entries", s_entries);
    nvs_set_i32(h, "exits", s_exits);
    nvs_set_i32(h, "occ", s_occupancy);
    nvs_commit(h);
    nvs_close(h);
}

// ROI activa (coordenadas nativas del radar, mm)
static ld2450_roi_t s_roi;
static bool         s_roi_ready = false;

/**
 * Desplaza el buffer hacia la izquierda descartando los primeros n bytes.
 */
static void buf_consume(size_t n) {
    if (n >= s_buf_len) { s_buf_len = 0; return; }
    memmove(s_uart_buf, s_uart_buf + n, s_buf_len - n);
    s_buf_len -= n;
}

/**
 * Busca el header AA FF 03 00 dentro del buffer.
 * Si lo encuentra, descarta todo lo anterior y devuelve true.
 */
static bool buf_sync_header(void) {
    for (size_t i = 0; i + 3 < s_buf_len; i++) {
        if (s_uart_buf[i]     == LD2450_FRAME_HEADER_0 &&
            s_uart_buf[i + 1] == LD2450_FRAME_HEADER_1 &&
            s_uart_buf[i + 2] == LD2450_FRAME_HEADER_2 &&
            s_uart_buf[i + 3] == LD2450_FRAME_HEADER_3) {
            if (i > 0) buf_consume(i); // descarta basura previa
            return true;
        }
    }
    // No se encontró header; conserva solo los últimos 3 bytes
    if (s_buf_len > 3) buf_consume(s_buf_len - 3);
    return false;
}

/**
 * Decodifica un bloque de 8 bytes (target) en la estructura.
 */
// static void parse_target_block(const uint8_t *block, ld2450_target_t *t) {
//     // x: int16 LE
//     uint16_t raw_x = (uint16_t)block[0] | ((uint16_t)block[1] << 8);
//     // y: int16 LE
//     uint16_t raw_y = (uint16_t)block[2] | ((uint16_t)block[3] << 8);
//     // speed: int16 LE
//     uint16_t raw_s = (uint16_t)block[4] | ((uint16_t)block[5] << 8);
//     // resolution: uint16 LE
//     uint16_t raw_r = (uint16_t)block[6] | ((uint16_t)block[7] << 8);

//     t->x_mm      = (int16_t)raw_x;
//     t->y_mm      = (int16_t)raw_y;
//     t->speed_cms = (int16_t)raw_s;
//     t->resolution = raw_r;
//     // Según el protocolo, resolution != 0 indica target válido[reference:8]
//     t->valid = (raw_r != 0);
// }


/**
 * Decodifica un campo de 16 bits del LD2450.
 *
 * El protocolo NO usa complemento a dos: usa signo-magnitud con una
 * convención particular:
 *   bit 15 = 1  -> valor POSITIVO
 *   bit 15 = 0  -> valor NEGATIVO
 *   bits 14..0  -> magnitud
 *
 * Ejemplo del datasheet: 0x030E -> -782, 0x86B1 -> +1713.
 */
static int16_t parse_signed_16(uint16_t raw) {
    int16_t magnitude = (int16_t)(raw & 0x7FFF);
    return (raw & 0x8000) ? magnitude : (int16_t)-magnitude;
}

/**
 * Calcula la ROI a partir de la configuración de ld2450.h.
 *
 * x: el eje lateral del radar es horizontal, así que el ancho de la
 *    puerta se aplica directo.
 * y: se corrige por la inclinación del montaje. Si el sensor mira
 *    hacia +F (hacia donde apunta su boresight) y la puerta está en
 *    F = 0:
 *
 *      y_radar = F*cos(tilt) + (h_sensor - h_torso)*sin(tilt)
 */
static void roi_init_from_config(void) {
#if LD2450_ROI_USE_FLOOR_GEOMETRY
    const float tilt   = LD2450_MOUNT_TILT_DEG * (LD2450_PI / 180.0f);
    const float c      = cosf(tilt);
    const float s      = sinf(tilt);
    const float offset = (float)(LD2450_MOUNT_HEIGHT_MM -
                                 LD2450_TARGET_NOMINAL_HEIGHT_MM) * s;

    s_roi.x_min_mm = (int16_t)-(LD2450_DOOR_WIDTH_MM / 2);
    s_roi.x_max_mm = (int16_t)( LD2450_DOOR_WIDTH_MM / 2);
    s_roi.y_min_mm = (int16_t)lroundf((float)(-LD2450_ROI_BACK_MM) * c + offset);
    s_roi.y_max_mm = (int16_t)lroundf((float)( LD2450_ROI_FRONT_MM) * c + offset);
#else
    s_roi.x_min_mm = LD2450_ROI_X_MIN_MM;
    s_roi.x_max_mm = LD2450_ROI_X_MAX_MM;
    s_roi.y_min_mm = LD2450_ROI_Y_MIN_MM;
    s_roi.y_max_mm = LD2450_ROI_Y_MAX_MM;
#endif

    // Margen para absorber la altura real de las personas
    s_roi.y_min_mm = (int16_t)(s_roi.y_min_mm - LD2450_ROI_MARGIN_MM);
    s_roi.y_max_mm = (int16_t)(s_roi.y_max_mm + LD2450_ROI_MARGIN_MM);

    // Por si la configuración quedó invertida
    if (s_roi.x_min_mm > s_roi.x_max_mm) {
        int16_t tmp = s_roi.x_min_mm;
        s_roi.x_min_mm = s_roi.x_max_mm;
        s_roi.x_max_mm = tmp;
    }
    if (s_roi.y_min_mm > s_roi.y_max_mm) {
        int16_t tmp = s_roi.y_min_mm;
        s_roi.y_min_mm = s_roi.y_max_mm;
        s_roi.y_max_mm = tmp;
    }

    s_roi_ready = true;
}

bool ld2450_in_roi(int16_t x_mm, int16_t y_mm) {
#if !LD2450_ROI_ENABLED
    (void)x_mm;
    (void)y_mm;
    return true;
#else
    if (!s_roi_ready) roi_init_from_config();
    return (x_mm >= s_roi.x_min_mm && x_mm <= s_roi.x_max_mm &&
            y_mm >= s_roi.y_min_mm && y_mm <= s_roi.y_max_mm);
#endif
}

void ld2450_get_roi(ld2450_roi_t *out) {
    if (!out) return;
    if (!s_roi_ready) roi_init_from_config();
    *out = s_roi;
}

bool ld2450_presence_detected(void) {
    return s_occupancy > 0;
}

uint8_t ld2450_occupancy(void) {
    return (uint8_t)(s_occupancy < 0 ? 0 : (s_occupancy > 255 ? 255 : s_occupancy));
}

int ld2450_entries(void) { return s_entries; }
int ld2450_exits(void)   { return s_exits; }

void ld2450_set_crossing_callback(void (*cb)(bool entry)) {
    s_crossing_cb = cb;
}

void ld2450_reset_counter(void) {
    s_entries = 0;
    s_exits = 0;
    s_occupancy = 0;
    aforo_save_nvs();
}

esp_err_t ld2450_set_roi(const ld2450_roi_t *roi) {
    if (!roi || roi->x_min_mm > roi->x_max_mm || roi->y_min_mm > roi->y_max_mm)
        return ESP_ERR_INVALID_ARG;

    s_roi = *roi;
    s_roi_ready = true;
    return ESP_OK;
}

static void parse_target_block(const uint8_t *block, ld2450_target_t *t) {
    uint16_t raw_x = (uint16_t)block[0] | ((uint16_t)block[1] << 8);
    uint16_t raw_y = (uint16_t)block[2] | ((uint16_t)block[3] << 8);
    uint16_t raw_s = (uint16_t)block[4] | ((uint16_t)block[5] << 8);
    uint16_t raw_r = (uint16_t)block[6] | ((uint16_t)block[7] << 8);

    t->x_mm       = parse_signed_16(raw_x);
    t->y_mm       = parse_signed_16(raw_y);
    t->speed_cms  = parse_signed_16(raw_s);
    t->resolution = raw_r;

    // Distancia radial (el protocolo entrega "distance resolution", no la
    // distancia; ésta se calcula con X/Y). Útil para telemetría.
    if (raw_r != 0) {
        int32_t d2 = (int32_t)t->x_mm * t->x_mm + (int32_t)t->y_mm * t->y_mm;
        t->distance_mm = (uint16_t)sqrtf((float)d2);
    } else {
        t->distance_mm = 0;
    }

    // Válido físicamente solo si:
    //   1) resolution != 0 (target confirmado por el radar)
    //   2) coordenadas dentro del rango físico del sensor
    //   3) velocidad dentro de un rango humano
    bool coord_ok = (t->x_mm >= LD2450_X_MIN_MM && t->x_mm <= LD2450_X_MAX_MM &&
                     t->y_mm >= LD2450_Y_MIN_MM && t->y_mm <= LD2450_Y_MAX_MM);
    bool speed_ok = (t->speed_cms >= -LD2450_SPEED_MAX_CMS &&
                     t->speed_cms <=  LD2450_SPEED_MAX_CMS);

    t->valid  = (raw_r != 0) && coord_ok && speed_ok;
    t->in_roi = t->valid && ld2450_in_roi(t->x_mm, t->y_mm);

    if (!t->valid) {
        t->direction = LD2450_DIR_NONE;
    } else if (t->speed_cms > 0) {
        t->direction = LD2450_DIR_MOVING_AWAY;
    } else if (t->speed_cms < 0) {
        t->direction = LD2450_DIR_APPROACHING;
    } else {
        t->direction = LD2450_DIR_STATIONARY;
    }
}

/**
 * Parsea un frame completo de 30 bytes.
 */
bool ld2450_parse_frame(const uint8_t *buf, ld2450_frame_t *out) {
    if (!buf || !out) return false;

    // Verificar header
    if (buf[0] != LD2450_FRAME_HEADER_0 || buf[1] != LD2450_FRAME_HEADER_1 ||
        buf[2] != LD2450_FRAME_HEADER_2 || buf[3] != LD2450_FRAME_HEADER_3) {
        return false;
    }
    // Verificar footer
    if (buf[28] != LD2450_FRAME_FOOTER_0 || buf[29] != LD2450_FRAME_FOOTER_1) {
        return false;
    }

    out->valid_count = 0;
    out->roi_count   = 0;
    for (int i = 0; i < LD2450_TARGET_COUNT; i++) {
        parse_target_block(&buf[4 + i * LD2450_TARGET_BLOCK_SIZE], &out->targets[i]);
        if (out->targets[i].valid)  out->valid_count++;
        if (out->targets[i].in_roi) out->roi_count++;
    }
    return true;
}

/**
 * Inicializa el UART.
 */
esp_err_t ld2450_init(void) {
    const uart_config_t cfg = {
        .baud_rate  = LD2450_UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(LD2450_UART_PORT,
                        LD2450_UART_BUF_SIZE * 2, 0, 0, NULL, 0),
                        TAG, "uart_driver_install falló");

    ESP_RETURN_ON_ERROR(uart_param_config(LD2450_UART_PORT, &cfg),
                        TAG, "uart_param_config falló");

    ESP_RETURN_ON_ERROR(uart_set_pin(LD2450_UART_PORT,
                        LD2450_UART_TX_GPIO, LD2450_UART_RX_GPIO,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin falló");

    s_buf_len = 0;
    for (int i = 0; i < MAX_TRACKS; i++) s_tracks[i].active = false;
    s_frame_idx  = 0;
    aforo_load_nvs();   // recupera el conteo de NVS (sobrevive reinicios)

    roi_init_from_config();

    ESP_LOGI(TAG, "UART%d inicializado a %d baudios (TX=%d, RX=%d)",
             LD2450_UART_PORT, LD2450_UART_BAUD_RATE,
             LD2450_UART_TX_GPIO, LD2450_UART_RX_GPIO);
    ESP_LOGI(TAG, "ROI puerta: x=[%d, %d] mm (ancho=%d mm), y=[%d, %d] mm (margen=%d mm)",
             s_roi.x_min_mm, s_roi.x_max_mm,
             s_roi.x_max_mm - s_roi.x_min_mm,
             s_roi.y_min_mm, s_roi.y_max_mm,
             LD2450_ROI_MARGIN_MM);
#if LD2450_ROI_USE_FLOOR_GEOMETRY
    if (LD2450_MOUNT_TILT_DEG >= 80)
        ESP_LOGW(TAG, "Inclinación %.0f° casi vertical: la Y mide sobre todo altura, "
                      "no profundidad. Calibra en sitio o inclina más el sensor.",
                 (double)LD2450_MOUNT_TILT_DEG);
    {
        const float tilt   = LD2450_MOUNT_TILT_DEG * (LD2450_PI / 180.0f);
        const float c      = cosf(tilt);
        const float s      = sinf(tilt);
        const float offset = (float)(LD2450_MOUNT_HEIGHT_MM -
                                     LD2450_TARGET_NOMINAL_HEIGHT_MM) * s;
        const float f_min  = (s_roi.y_min_mm - offset) / c;
        const float f_max  = (s_roi.y_max_mm - offset) / c;
        ESP_LOGI(TAG, "ROI equivalente en piso (torso %d mm): F=[%.2f, %.2f] m",
                 LD2450_TARGET_NOMINAL_HEIGHT_MM, f_min / 1000.0f, f_max / 1000.0f);
    }
    ESP_LOGI(TAG, "Montaje: altura=%d mm, inclinación=%d°, torso=%d mm, "
                  "frente=%d mm, atrás=%d mm",
             LD2450_MOUNT_HEIGHT_MM, LD2450_MOUNT_TILT_DEG,
             LD2450_TARGET_NOMINAL_HEIGHT_MM,
             LD2450_ROI_FRONT_MM, LD2450_ROI_BACK_MM);
#endif
    return ESP_OK;
}

/**
 * Tarea principal: lee UART, ensambla frames y loguea.
 */
// void ld2450_task(void *pvParameters) {
//     (void)pvParameters;
//     ld2450_frame_t frame;
//     uint8_t chunk[128];

//     ESP_LOGI(TAG, "Tarea de radar iniciada");

//     while(1) {
//         // Leer lo que haya disponible (timeout corto para no bloquear)
//         int n = uart_read_bytes(LD2450_UART_PORT, chunk, sizeof(chunk),
//                                 pdMS_TO_TICKS(50));
//         if (n > 0) {
//             // Acumular en el buffer interno (si hay espacio)
//             size_t espacio = sizeof(s_uart_buf) - s_buf_len;
//             size_t copiar  = (size_t)n < espacio ? (size_t)n : espacio;
//             memcpy(s_uart_buf + s_buf_len, chunk, copiar);
//             s_buf_len += copiar;

//             // Intentar extraer todos los frames completos posibles
//             while (s_buf_len >= LD2450_FRAME_SIZE) {
//                 if (!buf_sync_header()) break; // aún no hay header alineado

//                 if (s_buf_len < LD2450_FRAME_SIZE) break; // esperar más datos

//                 if (ld2450_parse_frame(s_uart_buf, &frame)) {
//                     // --- LOG de depuración ---
//                     ESP_LOGI(TAG, "Frame válido | targets=%u", frame.valid_count);
//                     for (int i = 0; i < LD2450_TARGET_COUNT; i++) {
//                         ld2450_target_t *t = &frame.targets[i];
//                         if (t->valid) {
//                             ESP_LOGI(TAG, "  T%d: x=%d mm, y=%d mm, v=%d cm/s, res=%u",
//                                      i + 1, t->x_mm, t->y_mm,
//                                      t->speed_cms, t->resolution);
//                         } else {
//                             ESP_LOGD(TAG, "  T%d: inválido (res=0)", i + 1);
//                         }
//                     }
//                 } else {
//                     ESP_LOGW(TAG, "Frame con header/footer inválido, descartando");
//                 }
//                 // Consumir el frame procesado
//                 buf_consume(LD2450_FRAME_SIZE);
//             }
//         }

//         // Pequeña pausa para ceder CPU; el sensor envía a ~10-20 Hz
//         vTaskDelay(pdMS_TO_TICKS(20));
//     }
// }


static const char *dir_str(ld2450_direction_t dir) {
    switch (dir) {
        case LD2450_DIR_APPROACHING: return "ACERCA";
        case LD2450_DIR_MOVING_AWAY: return "ALEJA";
        case LD2450_DIR_STATIONARY:  return "QUIETO";
        default:                     return "NA";
    }
}

static int door_line_y(void) {
    const float tilt = LD2450_MOUNT_TILT_DEG * (LD2450_PI / 180.0f);
    const float s    = sinf(tilt);
    return (int)((LD2450_MOUNT_HEIGHT_MM - LD2450_TARGET_NOMINAL_HEIGHT_MM) * s);
}

static void process_crossings(const ld2450_frame_t *frame) {
    s_frame_idx++;

    // decrementar cooldowns
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (s_tracks[i].active && s_tracks[i].cooldown > 0) s_tracks[i].cooldown--;
    }

    // recolectar objetivos válidos del frame
    int16_t tx[LD2450_TARGET_COUNT], ty[LD2450_TARGET_COUNT];
    int n_targets = 0;
    for (int i = 0; i < LD2450_TARGET_COUNT; i++) {
        if (frame->targets[i].valid && frame->targets[i].in_roi) {
            tx[n_targets] = frame->targets[i].x_mm;
            ty[n_targets] = frame->targets[i].y_mm;
            n_targets++;
        }
    }

    int line = door_line_y();
    int in_thr = line + CROSS_IN_MM;
    int out_thr = line - CROSS_OUT_MM;

    // emparejar objetivos con tracks por nearest-neighbor
    bool matched_target[LD2450_TARGET_COUNT];
    for (int i = 0; i < n_targets; i++) matched_target[i] = false;
    bool matched_track[MAX_TRACKS];
    for (int i = 0; i < MAX_TRACKS; i++) matched_track[i] = false;

    // greedy: para cada target, buscar el track más cercano no emparejado dentro del gate
    for (int t = 0; t < n_targets; t++) {
        int best = -1;
        int best_dist = TRACK_GATE_MM;
        for (int k = 0; k < MAX_TRACKS; k++) {
            if (!s_tracks[k].active || matched_track[k]) continue;
            int dx = tx[t] - s_tracks[k].x;
            int dy = ty[t] - s_tracks[k].y;
            int d = (int)sqrtf((float)(dx*dx + dy*dy));
            if (d < best_dist) { best_dist = d; best = k; }
        }
        if (best >= 0) {
            // actualizar track
            track_t *tr = &s_tracks[best];
            tr->x = tx[t];
            tr->y = ty[t];
            tr->last_seen = s_frame_idx;
            matched_track[best] = true;
            matched_target[t] = true;

            if (tr->cooldown > 0) continue;  // no contar durante cooldown

            // máquina de estados
            if (tr->state == 0) {
                // desconocido → establecer estado sin contar
                if (ty[t] >= in_thr) tr->state = 2;
                else if (ty[t] <= out_thr) tr->state = 1;
            } else if (tr->state == 1 && ty[t] >= in_thr) {
                // fuera → dentro: entrada
                tr->state = 2;
                s_entries++;
                s_occupancy++;
                tr->cooldown = 5;
                if (s_crossing_cb) s_crossing_cb(true);
                aforo_save_nvs();
            } else if (tr->state == 2 && ty[t] <= out_thr) {
                // dentro → fuera: salida
                tr->state = 1;
                s_exits++;
                if (s_occupancy > 0) s_occupancy--;
                tr->cooldown = 5;
                if (s_crossing_cb) s_crossing_cb(false);
                aforo_save_nvs();
            }
        }
    }

    // crear tracks nuevos para objetivos no emparejados
    for (int t = 0; t < n_targets; t++) {
        if (matched_target[t]) continue;
        // buscar slot libre
        int slot = -1;
        for (int k = 0; k < MAX_TRACKS; k++) {
            if (!s_tracks[k].active) { slot = k; break; }
        }
        if (slot < 0) continue;
        track_t *tr = &s_tracks[slot];
        tr->active = true;
        tr->x = tx[t];
        tr->y = ty[t];
        tr->last_seen = s_frame_idx;
        tr->cooldown = 0;
        if (ty[t] >= in_thr) tr->state = 2;
        else if (ty[t] <= out_thr) tr->state = 1;
        else tr->state = 0;
    }

    // expirar tracks no vistos
    for (int k = 0; k < MAX_TRACKS; k++) {
        if (s_tracks[k].active && (s_frame_idx - s_tracks[k].last_seen) > TRACK_TIMEOUT) {
            s_tracks[k].active = false;
        }
    }
}


void ld2450_task(void *pvParameters) {
    (void)pvParameters;
    ld2450_frame_t frame;
    uint8_t chunk[128];
    uint64_t rx_bytes = 0, frames_ok = 0;
    uint8_t  sample[16] = {0};
    size_t   sample_len = 0;
    TickType_t last_frame = 0;
    TickType_t last_report = xTaskGetTickCount();

    ESP_LOGI(TAG, "Tarea de radar iniciada");

    while (1) {
        int n = uart_read_bytes(LD2450_UART_PORT, chunk, sizeof(chunk),
                                pdMS_TO_TICKS(50));
        if (n > 0) {
            rx_bytes += (uint64_t)n;
            if (sample_len < sizeof(sample)) {
                size_t take = sizeof(sample) - sample_len;
                if ((size_t)n < take) take = (size_t)n;
                memcpy(sample + sample_len, chunk, take);
                sample_len += take;
            }
            size_t espacio = sizeof(s_uart_buf) - s_buf_len;
            size_t copiar  = (size_t)n < espacio ? (size_t)n : espacio;
            memcpy(s_uart_buf + s_buf_len, chunk, copiar);
            s_buf_len += copiar;

            while (s_buf_len >= LD2450_FRAME_SIZE) {
                if (!buf_sync_header()) break;
                if (s_buf_len < LD2450_FRAME_SIZE) break;

                if (ld2450_parse_frame(s_uart_buf, &frame)) {
                    frames_ok++;
                    last_frame = xTaskGetTickCount();
                    // ── Conteo de entradas/salidas por cruce de puerta ──
                    process_crossings(&frame);
                    // ────────────────────────────────────────────────────

                    ESP_LOGI(TAG, "Frame | físicos=%d | en ROI=%d | aforo=%d (E=%d S=%d)",
                             frame.valid_count, frame.roi_count, s_occupancy,
                             s_entries, s_exits);
                    for (int i = 0; i < LD2450_TARGET_COUNT; i++) {
                        ld2450_target_t *t = &frame.targets[i];
                        if (t->valid) {
                            ESP_LOGI(TAG, "  T%d: x=%+.2f m, y=%+.2f m, d=%.2f m, "
                                          "v=%+.2f m/s, %s, res=%u mm, ROI=%s",
                                     i + 1, t->x_mm / 1000.0f, t->y_mm / 1000.0f,
                                     t->distance_mm / 1000.0f,
                                     t->speed_cms / 100.0f, dir_str(t->direction),
                                     (unsigned)t->resolution,
                                     t->in_roi ? "SI" : "no");
                        } else {
                            ESP_LOGD(TAG, "  T%d: sin objetivo", i + 1);
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "Frame con header/footer inválido, descartando");
                }
                buf_consume(LD2450_FRAME_SIZE);
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (last_frame != 0 && (now - last_frame) > pdMS_TO_TICKS(3000)) {
            last_frame = 0;
            ESP_LOGW(TAG, "Sin frames por 3 s; radar sin datos (aforo se conserva: %d)",
                     s_occupancy);
        }
        if ((now - last_report) >= pdMS_TO_TICKS(5000)) {
            last_report = now;
            if (rx_bytes == 0) {
                ESP_LOGW(TAG, "UART%d sin bytes en 5 s: revisa 5V, GND y que TX del "
                              "radar vaya a GPIO%d (RX) y RX a GPIO%d (TX)",
                         LD2450_UART_PORT, LD2450_UART_RX_GPIO, LD2450_UART_TX_GPIO);
            } else if (frames_ok == 0) {
                ESP_LOGW(TAG, "UART%d rx=%llu bytes pero 0 frames. Primeros bytes: "
                              "%02X %02X %02X %02X %02X %02X %02X %02X "
                              "(esperado AA FF 03 00; baud=%d)",
                         LD2450_UART_PORT, (unsigned long long)rx_bytes,
                         sample[0], sample[1], sample[2], sample[3],
                         sample[4], sample[5], sample[6], sample[7],
                         LD2450_UART_BAUD_RATE);
            } else {
                ESP_LOGI(TAG, "UART%d rx=%llu bytes, frames=%llu, aforo=%u",
                         LD2450_UART_PORT, (unsigned long long)rx_bytes,
                         (unsigned long long)frames_ok, s_occupancy);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}