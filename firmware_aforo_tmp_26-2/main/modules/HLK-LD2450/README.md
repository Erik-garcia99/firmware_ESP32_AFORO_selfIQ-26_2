# HLK-LD2450 — zona de detección de la puerta

## Especificaciones relevantes

| Parámetro | Valor |
|---|---|
| Banda | 24 GHz (FMCW) |
| Alcance | hasta 8 m (el manual típico reporta 6 m) |
| Azimut | ±60° (120° total) |
| Elevación | ±35° (70° total) |
| Frecuencia de datos | 10 fps |
| Objetivos simultáneos | 3 |
| UART | 256000 8N1, 3.3 V |

El sensor **no permite reducir su FOV por software**. Lo que se hace en este
driver es filtrar las coordenadas: los objetivos que caen fuera del rectángulo
de la puerta se descartan para la presencia/conteo.

## Sistema de coordenadas

El LD2450 entrega `(x, y)` en **su propio plano**, en milímetros:

- `x`: lateral (el ancho de la puerta).
- `y`: profundidad medida sobre el plano inclinado del radar.

El origen es el sensor, no el piso ni el umbral de la puerta.

> El eje `x` del radar debe quedar paralelo al ancho de la puerta. Si lo montas
> girado 90°, tendrás que intercambiar los límites de X y Y en la ROI.

### Sensor inclinado hacia abajo

Si el sensor está a una altura `h_s` y su boresight se inclina `tilt` grados
bajo la horizontal, para un blanco a altura `h_t` y distancia horizontal `F`
respecto al plano de la puerta:

```
y_radar = F * cos(tilt) + (h_s - h_t) * sin(tilt)
```

- `F = 0` es el plano de la puerta.
- `F > 0` es hacia donde apunta el sensor.
- `x_radar` no se ve afectado por la inclinación (si el sensor está nivelado
  lateralmente).

Por eso `y` no se puede interpretar directamente como "cm desde la puerta":
depende de la altura de la persona. El código usa una altura nominal de torso
(`LD2450_TARGET_NOMINAL_HEIGHT_MM`) y deja un margen para absorber personas más
altas o bajas.

> **Importante:** si el sensor se apunta casi verticalmente hacia el piso
> (`tilt ≈ 90°`), `cos(tilt) ≈ 0` y `y` pasa a medir principalmente la altura
> del blanco. En ese montaje se pierde la capacidad de distinguir "adelante" de
> "atrás". Para una puerta conviene inclinarlo hacia el lado que quieres cubrir
> (45°–70°) y calibrar en sitio.

### Cobertura real del abanico

El rectángulo de la ROI no cambia el abanico físico. Con el sensor a altura
`h` e inclinación `tilt`, los bordes del haz (±35° en elevación) tocan el piso
aproximadamente en:

```
F_cerca = h / tan(tilt + 35°)
F_lejos = h / tan(tilt - 35°)
```

Ejemplos con `h = 2.0 m`:

| tilt | F_cerca | F_lejos | Comentario |
|---|---|---|---|
| 45° | 0.35 m | 11.3 m | cubre solo hacia adelante; no llega al plano de la puerta |
| 60° | -0.18 m | 4.3 m | cubre un poco detrás; buen compromiso de un solo lado |
| 70° | -0.54 m | 2.9 m | cubre ambos lados, pero `y` ya es casi altura |
| 90° | -1.40 m | 1.40 m | cono vertical; no distingue profundidad |

Si `tilt ≤ 35°`, el borde superior del haz apunta hacia arriba y no toca el
piso; el alcance útil queda limitado por la sensibilidad del radar.

Por eso, para contar ocupación en un acceso suele ser más práctico inclinar el
sensor hacia el interior (un solo lado) que pretender cubrir ±70 cm simétricos
con un solo LD2450.

## Configuración (`ld2450.h`)

```c
#define LD2450_ROI_ENABLED                 1
#define LD2450_ROI_USE_FLOOR_GEOMETRY      1

#define LD2450_DOOR_WIDTH_MM               1800   // ancho de la puerta
#define LD2450_ROI_FRONT_MM                700    // 70 cm hacia adelante
#define LD2450_ROI_BACK_MM                 700    // 70 cm hacia atrás

#define LD2450_MOUNT_HEIGHT_MM             2000   // altura del sensor
#define LD2450_MOUNT_TILT_DEG              60     // inclinación bajo la horizontal
#define LD2450_TARGET_NOMINAL_HEIGHT_MM    1300   // altura media del torso
#define LD2450_ROI_MARGIN_MM               100    // margen por altura de personas
```

Con los valores por defecto la ROI nativa queda aproximadamente:

```
x = [-900, +900] mm
y = [+156, +1056] mm   (equivalente a F ≈ [-0.90, +0.90] m para torso de 1.3 m)
```

El log de arranque imprime la ROI calculada y su equivalente en piso:

```
I (xxx) LD2450: ROI puerta: x=[-900, 900] mm (ancho=1800 mm), y=[156, 1056] mm (margen=100 mm)
I (xxx) LD2450: ROI equivalente en piso (torso 1300 mm): F=[-0.90, 0.90] m
```

## Calibración en sitio

1. Flashea y abre el monitor serie.
2. Marca en el piso los límites deseados (por ejemplo, ±70 cm desde el plano
   de la puerta) y el ancho de la puerta.
3. Camina por cada borde y observa en el log:

   ```
   T1: x=+0.42 m, y=+0.95 m, d=1.04 m, v=-0.35 m/s, ACERCA, res=320 mm, ROI=SI
   ```

4. Ajusta `LD2450_ROI_FRONT_MM`, `LD2450_ROI_BACK_MM`, `LD2450_DOOR_WIDTH_MM`,
   `LD2450_MOUNT_HEIGHT_MM` y `LD2450_MOUNT_TILT_DEG` hasta que los bordes
   coincidan.
5. Si prefieres calibrar directamente con los números nativos, pon
   `LD2450_ROI_USE_FLOOR_GEOMETRY` en `0` y ajusta `LD2450_ROI_X_*` y
   `LD2450_ROI_Y_*`.

## API

- `ld2450_parse_frame()`: llena `valid_count` (objetos físicos) y `roi_count`
  (objetos dentro de la puerta).
- `ld2450_in_roi(x, y)`: consulta puntual.
- `ld2450_presence_detected()`: presencia estable dentro de la ROI (debounce de
  ~0.5 s). Es la señal recomendada para telemetría/conteo.
- `ld2450_occupancy()`: aforo estable dentro de la ROI (`0..3`). Cambia solo
  cuando el nuevo valor se repite `LD2450_CONFIRM_FRAMES` frames (~0.5 s).
- `ld2450_get_roi()` / `ld2450_set_roi()`: leer o cambiar la ROI en caliente
  (por ejemplo, desde MQTT).

## Telemetría de aforo

`main.c` crea `aforo_telemetry_task`, que cada `LD2450_TELEMETRY_INTERVAL_MS`
(2 s por defecto) publica:

- `occupancy`: aforo acumulado (entradas − salidas), 0..N.
- `entries`: total de entradas detectadas desde el arranque.
- `exits`: total de salidas detectadas desde el arranque.

Además, cada cruce de la puerta dispara un evento inmediato `entry`/`exit`
(vía `ld2450_set_crossing_callback`), para tener el timestamp exacto de cada
paso en InfluxDB.

Topic: `selfiq/<branch_id>/<esp_id>/capacity_atmosphere/telemetry`
Payload: `{"event_id":"...","event_type":"occupancy","data":{"value":N}}`

El conteo es por **cruce de la línea de la puerta**, no por presencia dentro
de la ROI: una persona que camina por la tienda sin cruzar no altera el aforo.
La detección usa nearest-neighbor entre frames + máquina de estados
(fuera → dentro = entrada, dentro → fuera = salida) con histéresis de
±150 mm y cooldown de 0.5 s.

Limitaciones: máximo 3 objetivos simultáneos; si 4 personas cruzan a la vez,
una no se cuenta. El contador se **persiste en NVS**, así que sobrevive
reinicios del ESP32. Para reiniciarlo (p. ej. a medianoche) llama a
`ld2450_reset_counter()`.

> El radar no identifica personas: con gente muy pegada el tracker puede
> intercambiar identidades y generar un conteo erróneo.

## Nota sobre el protocolo

Los campos `x`, `y` y `speed` son signo-magnitud con esta convención:

- bit 15 = 1 → valor **positivo**
- bit 15 = 0 → valor **negativo**

Ejemplo: `0x030E` → `-782`, `0x86B1` → `+1713`.
