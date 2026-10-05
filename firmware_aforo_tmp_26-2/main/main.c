/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor erik garcia chavez 
 * @date octuber 2026 
 * @assginature : proyecto de carrera 
 * @teacher : isabel rocha garcia 
 *
 **************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// modulos de freerots
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// drivers
#include <driver/gpio.h>


// logs
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

// wifi
#include <esp_wifi.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <nvs_flash.h>

// biblioteclas propias
#include "global.h"
#include "modules/DHT11/dht11_lib.h"
#include "modules/MQTT/mqtt_lib.h"
#include "modules/WIFI/wifi_lib.h"
// macros

//+++++++++++++ colas

//+++++++++++++ grupos de eventos
EventGroupHandle_t s_wifi_event_group;
esp_wifi_t esp_wifi;

// semaforos?

// ++++++++++++++++++++++++ variables globales ++++++++++++++++++++=

char g_wifi_ssid[33] = {0};
char g_wifi_pass[65] = {0};
char g_broker_ip[16] = {0}; 

//+++++++++++++++++++++++++ funciones ++++++++++++++++++++++++++++

// tengo que actualizar esta funcion, porque ya no me acuerdo como la habia puesto, pero dejaremos para poder inciar sesion en redes de empresa para conectarnos a la red de UABC. por si no traigo datos. 
/**
* @note actualizacion de funcion, para aceptar redes de hogar y de epresa  
***/
esp_err_t update_setup_cred(char *key, char *anchor, char *pswd_ent,
                            char *identificator);






// funciones para flash, para averiguar si hay credenicales guardadas. 
esp_err_t nvs_save_str(const char *key, const char *value);
esp_err_t nvs_load_str(const char *key, char *buf, size_t len);

// tareas
//
void app_main(void) {

  /**
   * lo primero que tendriamos que hacer es inciar WIFI,
   *
   * */

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);


  // Grupo de eventos WiFi
  s_wifi_event_group = xEventGroupCreate();

  // --- Carga/obtencion de credenciales WiFi
  bool creds_in_nvs = false;
  if (nvs_load_str("wifi_ssid", g_wifi_ssid, sizeof(g_wifi_ssid)) == ESP_OK &&
      nvs_load_str("wifi_pass", g_wifi_pass, sizeof(g_wifi_pass)) == ESP_OK) {
    creds_in_nvs = true;
  }

  if (!creds_in_nvs) {
    /*****************************************
     *      no hay credenciales, entonces ahora la ESP no lazan una AP, lo que hara la ESP32 
     *      es buscar la IP, por defecto que tiene la rasberry pi zero 2 w, que estra corrinedo en pararelo 
     *      como AP, en la cual por medio de sockets TCP, vamos a solicitar las credenciales de WIFI, 
     *      
     *      esto con el objetivo de que nomas se tenga que introducir las credenciales 1 vez, por ejemplo
     *      al adquirir por primera vez el dispositvo, despues si se adquiere mas o se actualiza las credenciales     * 
     *      solo se introducen 1 vez y cada esp va a soictar a la rasberry las credenciales o las nuevas 
     *      la rasberry tiene un maximo de 8 disposiovos conectados a la vez, lo cual no es un problema 
     *      porque este solo va a sociitar y esperar 1 vez, que es cunado se solciita, se desconecta y 
     *      se conecta a la red wifi 
     *
     *
     *
     ******************************************/
    
    //esta seccion sera esa parte, la coneeccion y obtencion de datos -> puede que mandemos a llama a una funcion, ya veremos 



  }
  
  // Conectar a WiFi en modo estación
  update_setup_cred(g_wifi_ssid, g_wifi_pass, NULL, "SSID");
  xEventGroupSetBits(s_wifi_event_group, WIFI_CREDS_READY);
  wifi_init_sta();
  // Restaurar el bit que fue borrado en wifi_init_sta()
  xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

// NVS helpers

esp_err_t nvs_save_str(const char *key, const char *value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;
  err = nvs_set_str(handle, key, value);
  if (err == ESP_OK)
    nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t nvs_load_str(const char *key, char *buf, size_t len) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
  if (err != ESP_OK)
    return err;
  size_t required = len;
  err = nvs_get_str(handle, key, buf, &required);
  nvs_close(handle) return err;
}






// ─────────────────────────────────────────────────────────────────────────────
// update_setup_cred
// ─────────────────────────────────────────────────────────────────────────────
esp_err_t update_setup_cred(char *ssid, char *pswd, char *user, char *type) {
  if (strcmp(type, "SSID") == 0) {
    esp_wifi.ssid = realloc(esp_wifi.ssid, strlen(ssid) + 1);
    esp_wifi.pswd = realloc(esp_wifi.pswd, strlen(pswd) + 1);
    if (esp_wifi.ssid != NULL && esp_wifi.pswd != NULL) {
      strcpy(esp_wifi.ssid, ssid);
      strcpy(esp_wifi.pswd, pswd);
      free(esp_wifi.user_name);
      esp_wifi.user_name = NULL;
      esp_wifi.type_connected = 0;
      return ESP_OK;
    }
    uart_write_bytes(UART_NUM_0, "ERR:NO_MEM\n", 11);
    return ESP_FAIL;
  }

  if (strcmp(type, "ENT") == 0) {
    esp_wifi.ssid = realloc(esp_wifi.ssid, strlen(ssid) + 1);
    esp_wifi.pswd = realloc(esp_wifi.pswd, strlen(pswd) + 1);
    esp_wifi.user_name = realloc(esp_wifi.user_name, strlen(user) + 1);
    if (esp_wifi.ssid != NULL && esp_wifi.pswd != NULL &&
        esp_wifi.user_name != NULL) {
      strcpy(esp_wifi.ssid, ssid);
      strcpy(esp_wifi.pswd, pswd);
      strcpy(esp_wifi.user_name, user);
      esp_wifi.type_connected = 1;
      return ESP_OK;
    }
    uart_write_bytes(UART_NUM_0, "ERR:NO_MEM\n", 11);
    return ESP_FAIL;
  }

  return ESP_FAIL;


}
