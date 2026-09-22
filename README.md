# Firmware Aforo y Ambiente — SelfIQ

Firmware del módulo de **control de aforo y monitoreo ambiental** del sistema SelfIQ.

Este repositorio contiene exclusivamente el firmware correspondiente al módulo basado en:

- ESP32-SUPERMINI o ESP32-C3 
- Sensor de presencia y posicionamiento HLK-LD2450
- Sensor de temperatura y humedad DHT22

El propósito del módulo es detectar presencia y contabilizar personas dentro del establecimiento, además de registrar información ambiental como temperatura y humedad. con el objetivo de cumplir con la normativa **PROY-NOM-011-SSPC-2025**

Los datos obtenidos son procesados localmente y enviados mediante MQTT hacia el gateway Raspberry Pi Zero 2 W.

---

## Descripción

Este módulo forma parte de la capa IoT local del sistema SelfIQ.

La ESP32-SUPERMINI se encarga de recibir información de dos sensores:

### HLK-LD2450

Sensor utilizado para:

- detectar presencia;
- identificar objetivos dentro de su rango;
- apoyar el conteo de personas;
- obtener información relacionada con la posición de los objetivos detectados.

El sensor puede detectar hasta tres personas simultáneamente.

### DHT22

Sensor utilizado para monitorear:

- temperatura;
- humedad relativa.

La información obtenida de ambos sensores es procesada localmente antes de publicarse mediante MQTT.

---

## Función dentro de la arquitectura

Este dispositivo no se comunica directamente con Internet.

El flujo general del módulo es:

```text
 ┌─────────────────┐
 │  HLK-LD2450     │
 │ Presencia/Aforo │
 └────────┬────────┘
          │
          │
          ▼
 ┌───────────────────┐
 │ ESP32-SUPERMINI   │
 │                   │
 │ Procesamiento     │
 │ local             │
 └─────────┬─────────┘
           ▲
           │
 ┌─────────┴─────────┐
 │      DHT22        │
 │ Temp. / Humedad   │
 └───────────────────┘
           │
           │ MQTT
           ▼
 ┌───────────────────┐
 │ Raspberry Pi      │
 │ Zero 2 W          │
 │                   │
 │ Mosquitto         │
 │ SQLite            │
 │ Gateway Edge      │
 └─────────┬─────────┘
           │
           │ HTTPS / TLS
           ▼
      Backend Cloud
