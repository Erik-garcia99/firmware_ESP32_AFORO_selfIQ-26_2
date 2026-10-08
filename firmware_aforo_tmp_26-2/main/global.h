/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

#ifndef GLOBAL_H
#define GLOBAL_H
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// macros
#define TRUE 1
#define FALSE 0

#define HEADER 0xABCD
#define ACK 0x5433
#define MAX_DATA 95
#define SSID_LEN_MAX 33
#define PSWD_LEN_MAX 26
#define USER_LEN_MAX 31

//+++++++++++++++++++++++++++++ Bits del grupo de eventos
// la peticion TCP recibio las credenciales con extoito: SSID/PSWD
// socket TCP

//++++++++++++++++++++++++++++++Constantes generales
extern QueueHandle_t tcp_rx_queue;

//+++++++++++++++++++++++++++ Estructuras

// estrucutras trama socket TCP

// sera el tipo que operacion que ser, mantenemos ACK y NACK para mas
// infromacion sobre si llegaron las peticiones o no
typedef enum {
  OPReqCredWifi = 0x00, // L:S
  OP_ACK,               // ACK
  OP_NACK,              // NACK
} TYPE_OP_TYPE_T;
extern TYPE_OP_TYPE_T op_type;

// accion que queremos haacer entre la ESP32 y la RPI, por el momento solo se
// tiene para hacer login
typedef enum {
  action_none = 0x00,
  eReqWifi, // soliciar credeniclaes WIFI a la RPI.
  eRespWifi,
  keepAlive = 0x5,
} ACTION_T;

extern ACTION_T action;

typedef enum {
  eNoType = 0x00,
  eWifiDefault,
  eWifiEnterprise,
  eRespACK,
} TYPE_FRAME_T;
extern TYPE_FRAME_T type_frame;

typedef enum {
  eDataSSID = 0x69, // para las 2 tipo de redes sera este mimso id para inidcar
                    // el inicio
  eDataPSWD = 0x70, // igual
  eDataUSR = 0x75,  // solo en el caso de empresa se usara este.
} DATA_FIELD_T;

/*************************************************************
 * @brief establece la estrucutra del frame entre peticiones y respuesta de ESP
 * con rasbery pi. para peticiones de credenicales de WIFI y otros proceso que
 * se pueda emplear.
 *
 * @member header: cabecera del frame indicando que se esta enviando,
 *    --> lo cuales puede ser:
 *          Peticion : 0x0
 *          Error (NACK) : 0xCAFE
 * @member len : tamanio completo del frame
 * @member action : que accion queremos hacer con la rpi cunado esta en mood AP
 *    --> puede ser, del enum < action >
 * @member type_frame :
 * @member type_red : si la red es privada o de empresa
 *   --> para empresa se requiere SSID, user y pswd del usuario
 *   --> para red privada solo SSID y pswd de la red
 * @member ssid : nombre de la red
 * @member pswd : contrasenia de la red o contrasenia de la red
 * @member user_network: usuario de la empresa
 *
 *********************************************************/

typedef struct {
  uint16_t header; // >> head of frame
  uint8_t len;     // >> lenght of frame
  TYPE_FRAME_T type_frame : 4;
  ACTION_T action : 4;
  uint8_t data[MAX_DATA];
} TYPE_FORMAT_REQUEST_T;

typedef struct {
  TYPE_OP_TYPE_T op_type;               // que operacion vamos a relaizar
  TYPE_FORMAT_REQUEST_T format_request; // trametos la trama a enviar
} TYPE_SEND_INFO_T;

extern TYPE_SEND_INFO_T send_info;

// ++++++++++++++++++++++++++++++++++++++ prototipo de funciones

#endif
