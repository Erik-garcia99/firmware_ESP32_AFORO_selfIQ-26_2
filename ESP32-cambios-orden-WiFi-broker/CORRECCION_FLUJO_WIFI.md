# Corrección: confirmar WiFi antes del broker

Reemplazar únicamente `main/main.c` en el proyecto. Compilar y cargar desde la raíz:

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Cambiar el puerto si tu placa usa otro. No hace falta borrar NVS para aplicar
esta corrección.

## Causa

El código anterior solicitaba WiFi, cerraba TCP y abría otra conexión para pedir
el broker ANTES de aplicar/probar las credenciales WiFi recibidas. Cuando Raspberry
respondía NACK a la operación de broker, se abandonaba también la etapa WiFi.
Además, al iniciar, no tener broker en NVS convertía `connected` a false aunque
la conexión a la red guardada hubiera funcionado.

Las peticiones eran secuenciales; no había dos solicitudes simultáneas. Cerrar TCP
no borra las credenciales. En ese punto la ESP seguía en SelfIQ-Admi porque aún no
se había ejecutado la conexión a la red definitiva.

## Flujo corregido

1. Probar la red WiFi guardada. Si conecta y obtiene IP, conservarla.
2. Si hace falta aprovisionar, conectar a SelfIQ-Admi y solicitar SOLO WiFi por TCP.
3. Cerrar el socket, aplicar las credenciales recibidas y conectar a la red definitiva.
4. Esperar confirmación WiFi/IP. Entonces guardar WiFi en NVS.
5. Iniciar la etapa de broker. Si ya está guardado, iniciar MQTT en la red definitiva.
6. Si falta el broker, consultar por TCP al AP de Raspberry en una etapa independiente.
7. Regresar siempre a la red definitiva verificada, también si hubo NACK o timeout.
8. Con respuesta válida, guardar el broker e iniciar MQTT. Con error, conservar WiFi
   y esperar 60 segundos antes de reintentar únicamente la consulta del broker.

Durante la consulta de broker, las credenciales definitivas se conservan en un
snapshot en RAM. El AP temporal nunca se guarda como red definitiva. Tras recibir
credenciales WiFi nuevas y comprobarlas, se invalida el destino MQTT anterior
para no reutilizar una IP de otra red; se conserva el UUID del nodo.

Como el único destino TCP conocido actualmente es `192.168.50.1:5000`, consultar
el broker después de confirmar WiFi requiere volver temporalmente al AP y luego
regresar. Los logs identifican ese paso como `AP SOLO PARA CONSULTAR BROKER`.
La conexión WiFi definitiva se interrumpe durante esa consulta. Para permanecer
en la red definitiva durante todo el proceso, hace falta descubrir o configurar
la IP LAN de Raspberry y disponer allí de un servidor que atienda esa petición;
no debe asumirse que la IP del router es la de Raspberry.

## Raspberry todavía debe responder al broker

El firmware ESP32 no puede obtener IP/puerto si Raspberry no implementa la operación:

- Petición: `AB CD 02 03 00`.
- Respuesta ejemplo para `192.168.1.25:1883`:
  `54 33 0B 54 76 04 C0 A8 01 19 77 02 07 5B`.
- Rechazo/NACK: `54 33 FF FF 00`.

Son bytes binarios. Raspberry debe devolver su IP accesible en la red definitiva.
Este parche corrige el orden y la independencia de etapas en ESP32; no implementa
el servidor Raspberry ni el puente MQTT. Si Raspberry sigue devolviendo NACK,
verás `Wi-Fi listo, MQTT pendiente` y la ESP reintentará el broker cada 60 s.
El AP Raspberry debe estar disponible para esas consultas.

## Credenciales guardadas y credenciales nuevas

Si la red guardada todavía conecta, la ESP la reutiliza. Una modificación en el
formulario Raspberry no actualiza automáticamente la NVS de una ESP ya conectada.
La búsqueda de credenciales nuevas ocurre al entrar al aprovisionamiento cuando
no puede conectarse a la red guardada. Esta política se conserva; el fallo de
broker por sí solo ya no reinicia el aprovisionamiento WiFi.

## Verificación

Se revisó la sintaxis C con interfaces simuladas y se ejecutaron cinco escenarios
del `app_main` corregido con mocks y AddressSanitizer/UndefinedBehaviorSanitizer:

- Nueva red válida: se confirma y persiste antes de pedir broker; dos NACK
  consecutivos no vuelven a solicitar WiFi y restauran la red definitiva.
- Red guardada sin broker: no se vuelve a pedir WiFi y se conserva tras NACK.
- Red y broker guardados: MQTT inicia sin abrir el AP/TCP de configuración.
- Nueva red inválida: no se guarda y no se solicita broker.
- Nueva red válida y respuesta de broker correcta: se guardan en orden y se inicia MQTT.

No se realizó compilación ESP-IDF ni prueba en placa aquí. Las pruebas host
verifican el flujo de aplicación, no las operaciones reales de radio, NVS o MQTT.
