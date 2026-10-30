/* *********************************************
 *              selfIQ 2026-2
 *  Driver HLK-LD2450 - Radar mmWave 24GHz
 *  Detección de hasta 3 objetivos
 ************************************************* */

#ifndef LD2450_H
#define LD2450_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "esp_err.h"

// Configuración por defecto (puedes sobreescribir en menuconfig)
#define LD2450_UART_PORT        UART_NUM_2
#define LD2450_UART_TX_GPIO     17      // Ajusta según tu hardware
#define LD2450_UART_RX_GPIO     16      // Ajusta según tu hardware
#define LD2450_UART_BAUD_RATE   256000
#define LD2450_UART_BUF_SIZE    1024

// Protocolo
#define LD2450_FRAME_HEADER_0   0xAA
#define LD2450_FRAME_HEADER_1   0xFF
#define LD2450_FRAME_HEADER_2   0x03
#define LD2450_FRAME_HEADER_3   0x00
#define LD2450_FRAME_FOOTER_0   0x55
#define LD2450_FRAME_FOOTER_1   0xCC
#define LD2450_FRAME_SIZE       30
#define LD2450_TARGET_COUNT     3
#define LD2450_TARGET_BLOCK_SIZE 8

// Límites físicos del sensor (para validar)
#define LD2450_Y_MIN_MM        (-1000)   // tolerancia "detrás del sensor"
#define LD2450_Y_MAX_MM        6000      // alcance máximo real
#define LD2450_X_MIN_MM        (-3000)
#define LD2450_X_MAX_MM        3000
#define LD2450_SPEED_MAX_CMS   300       // 3 m/s, más que eso es ruido

/* ─────────────────────────────────────────────────────────────
 * ZONA DE INTERÉS (ROI) DE LA PUERTA
 *
 * El LD2450 no permite "cambiar" su FOV: su abanico es de ±60° en
 * azimut (±35° en elevación). Lo que hacemos es descartar en software
 * los objetivos que caen fuera del rectángulo de la puerta.
 *
 * El sensor entrega (x, y) en SU PROPIO PLANO, en mm:
 *   x = lateral (ancho de la puerta)
 *   y = profundidad sobre el plano del radar
 *
 * Si el sensor está inclinado hacia abajo, "y" NO es la distancia
 * horizontal al piso. Para que la configuración sea intuitiva,
 * LD2450_ROI_USE_FLOOR_GEOMETRY calcula los límites de "y" a partir
 * de la geometría de montaje:
 *
 *   y_radar = F*cos(tilt) + (altura_sensor - altura_objetivo)*sin(tilt)
 *
 * donde F es la distancia horizontal al plano de la puerta
 * (F=0 en la puerta, F>0 hacia donde apunta el sensor).
 *
 * Esta fórmula supone que el blanco (torso de una persona) está a
 * LD2450_TARGET_NOMINAL_HEIGHT_MM. Si la persona es más alta o baja,
 * la "y" se corre. Por eso conviene dejar un margen y verificar con
 * los logs de calibración.
 *
 * Si prefieres calibrar a mano, pon LD2450_ROI_USE_FLOOR_GEOMETRY en 0
 * y ajusta LD2450_ROI_X_* / LD2450_ROI_Y_* con lo que veas en el log.
 *
 * Recomendación de montaje: el radar mide en un plano (abanico 2D).
 * Si se apunta casi verticalmente hacia el piso, "y" pasa a medir
 * principalmente la altura del blanco y se pierde la profundidad.
 * Para distinguir "adelante/atrás" conviene inclinarlo entre 20° y
 * 50° respecto a la horizontal y calibrar en sitio.
 * ───────────────────────────────────────────────────────────── */
#define LD2450_ROI_ENABLED                 1
#define LD2450_ROI_USE_FLOOR_GEOMETRY      1

// Zona deseada en el piso (mm)
#define LD2450_DOOR_WIDTH_MM               1800   // ancho de la puerta
#define LD2450_ROI_FRONT_MM                700    // 70 cm hacia adelante
#define LD2450_ROI_BACK_MM                 700    // 70 cm hacia atrás

// Geometría de montaje (mm / grados)
#define LD2450_MOUNT_HEIGHT_MM             2000   // altura del sensor
#define LD2450_MOUNT_TILT_DEG              60     // inclinación bajo la horizontal
#define LD2450_TARGET_NOMINAL_HEIGHT_MM    1300   // altura media del torso

// Margen extra en Y (mm) para absorber personas más altas/bajas que el
// torso nominal. Se suma por ambos lados del rectángulo.
#define LD2450_ROI_MARGIN_MM               100

// ROI manual (solo si LD2450_ROI_USE_FLOOR_GEOMETRY == 0)
#define LD2450_ROI_X_MIN_MM               (-900)
#define LD2450_ROI_X_MAX_MM               ( 900)
#define LD2450_ROI_Y_MIN_MM               (-700)
#define LD2450_ROI_Y_MAX_MM               ( 700)

// Telemetría de aforo (F-05: conteo en tiempo real <= 2 s)
#define LD2450_TELEMETRY_INTERVAL_MS      2000U   // una muestra cada 2 s
#define LD2450_TELEMETRY_TASK_STACK_BYTES 3072U
#define LD2450_TELEMETRY_TASK_PRIORITY    4U
#define LD2450_EVENT_OCCUPANCY            "occupancy"

// Dirección radial respecto al sensor (signo de la velocidad)
typedef enum {
    LD2450_DIR_NONE = 0,
    LD2450_DIR_APPROACHING,  // se acerca al sensor (speed < 0)
    LD2450_DIR_MOVING_AWAY,  // se aleja del sensor (speed > 0)
    LD2450_DIR_STATIONARY,   // speed == 0
} ld2450_direction_t;

// Estructura de un objetivo decodificado
typedef struct {
    bool     valid;       // true si el radar reporta un objetivo físico válido
    bool     in_roi;      // true si además cae dentro de la zona de la puerta
    int16_t  x_mm;        // coordenada X en milímetros
    int16_t  y_mm;        // coordenada Y en milímetros
    uint16_t distance_mm; // distancia radial calculada: sqrt(x² + y²)
    int16_t  speed_cms;   // velocidad en cm/s
    uint16_t resolution;  // resolución de distancia (0 = inválido)
    ld2450_direction_t direction; // según el signo de speed_cms
} ld2450_target_t;

// Frame completo parseado
typedef struct {
    ld2450_target_t targets[LD2450_TARGET_COUNT];
    uint8_t         valid_count; // objetivos físicamente válidos
    uint8_t         roi_count;   // objetivos válidos dentro de la ROI
} ld2450_frame_t;

// Rectángulo de la zona de interés, en coordenadas nativas del radar
typedef struct {
    int16_t x_min_mm;
    int16_t x_max_mm;
    int16_t y_min_mm;
    int16_t y_max_mm;
} ld2450_roi_t;

/**
 * @brief Inicializa el UART y la ROI del radar.
 * @return ESP_OK en éxito.
 */
esp_err_t ld2450_init(void);

/**
 * @brief Tarea de recepción continua. Debe crearse con xTaskCreate.
 *        Lee el UART, ensambla frames y los publica por ESP_LOG.
 */
void ld2450_task(void *pvParameters);

/**
 * @brief (Opcional) Parsea un frame crudo de 30 bytes.
 * @param buf  Puntero a 30 bytes que inician con AA FF 03 00.
 * @param out  Frame de salida.
 * @return true si el frame es válido y fue parseado.
 */
bool ld2450_parse_frame(const uint8_t *buf, ld2450_frame_t *out);

/**
 * @brief Indica si un punto (x, y) del radar cae dentro de la ROI.
 *        Si la ROI está deshabilitada, siempre devuelve true.
 */
bool ld2450_in_roi(int16_t x_mm, int16_t y_mm);

/**
 * @brief Copia la ROI activa (útil para logs/telemetría).
 */
void ld2450_get_roi(ld2450_roi_t *out);

/**
 * @brief Presencia: true si hay al menos una persona dentro de la ROI.
 */
bool ld2450_presence_detected(void);

/**
 * @brief Aforo acumulado (entradas - salidas), 0..255.
 */
uint8_t ld2450_occupancy(void);

/**
 * @brief Total de entradas detectadas desde el arranque.
 */
int ld2450_entries(void);

/**
 * @brief Total de salidas detectadas desde el arranque.
 */
int ld2450_exits(void);

/**
 * @brief Registra un callback que se invoca al detectar un cruce
 *        (true = entrada, false = salida). Puede ser NULL.
 */
void ld2450_set_crossing_callback(void (*cb)(bool entry));

/**
 * @brief Reinicia el contador acumulado (entradas, salidas, aforo) y lo guarda.
 */
void ld2450_reset_counter(void);

/**
 * @brief Cambia la ROI en caliente (por ejemplo, desde MQTT).
 * @return ESP_OK si el rectángulo es coherente.
 */
esp_err_t ld2450_set_roi(const ld2450_roi_t *roi);

#endif // LD2450_H
