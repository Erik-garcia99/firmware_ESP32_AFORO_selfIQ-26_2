/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor erik garcia chavez 
 * @date octuber 2026 
 * @assginature : proyecto de carrera 
 * @teacher : isabel rocha garcia 
 *
 ************************************************* */

#ifndef GLOBAL_H
#define GLOBAL_H

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

//macros 
#define TRUE 1
#define FALSE 0
#define RPI_IP "148.168.0.1" // aun no ponemos la IP correcta por defecto
#define RPI_PORT "50007"



//+++++++++++++++++++++++++++++ Bits del grupo de eventos
//WIFI
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_UPDATE BIT10
#define BREAK_UPDATE_WIFI BIT11
#define WIFI_CREDS_READY BIT12
#define BROKER_RECEIVED BIT3 // usado para ACK de SET_WIFI 
#define WIFI_CREDS_RECEIVED BIT13 // la peticion TCP recibio las credenciales con extoito: SSID/PSWD
//socket TCP
#define LOGIN_SUCCESS BIT4
#define LOGIN_FAIL    BIT3


                                  
                                  



//++++++++++++++++++++++++++++++Constantes generales

//+++++++++++++++++++++++++++ Estructuras

// estrucutras para controlar los diferenites elementos tanto para redes wifi de casa como redes de empresa 
// esto especialemntepara conectarnos a la red de UABC.
typedef struct {
  const char *alias;
  const char *ssid;
  const char *pswd;
} wifi_default_profile_t;

typedef struct {
  const char *alias;
  const char *ssid;
  const char *user;
  const char *pswd;
} wifi_default_ent_profile_t;

extern char g_wifi_ssid[33];
extern char g_wifi_pass[65];
extern char g_broker_ip[16];

// estrucutras trama socket TCP


// sera el tipo que operacion que ser, mantenemos ACK y NACK para mas infromacion sobre si llegaron las peticiones o no
typedef enum{
    OP_LOGIN,       // L:S
    OP_ACK,         // ACK
    OP_NACK,        // NACK
}op_type_t;
extern op_type_t op_type;


//accion que queremos haacer entre la ESP32 y la RPI, por el momento solo se 
//tiene para hacer login 
typedef enum{
  action_none  = 0x00,
	req_cred_rpi = 0x1, // soliciar credeniclaes WIFI a la RPI. 
	keep_alive = 0x5,
}action_t;

extern action_t action;

typedef enum{
    server = 0xf,
}resourse_t;
extern resourse_t resourse;


/*
 * @brief establece la estrucutra del frame entre peticiones y respuesta de ESP con rasbery pi. para peticiones de credenicales
 * de WIFI y otros proceso que se pueda emplear.
 *
 * @miember header: cabecera del frame indicando que se esta enviando, 
 *    --> lo cuales puede ser:
 *          Peticion : 0x0
 *          Error (NACK) : 0xCAFE
 * @miember len : tamanio completo del frame 
 * @miember action : que accion queremos hacer con la rpi cunado esta en mood AP 
 *    --> puede ser, del enum < action >
 *
 * **/

typedef struct{
    uint16_t header; // >> head of frame  
    uint8_t len; // >> lenght of frame  
    action_t action:4;
    resourse_t resourse:4;

}format_request_t;




// ++++++++++++++++++++++++++++++++++++++ prototipo de funciones 

extern esp_err_t nvs_save_str(const char *key, const char *value);
extern esp_err_t nvs_load_str(const char *key, char *buf, size_t len);

#endif
