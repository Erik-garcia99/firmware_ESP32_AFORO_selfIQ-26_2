idf_component_register(
            SRCS 
            "main.c" 
            "modules/WIFI/wifi_lib.c"
            "modules/MQTT/mqtt_lib.h"
            "modules/DHT11/dht11_lib.c"
            "modules/TCP/tcp_lib.c"
                    
            INCLUDE_DIRS
            "." 
            "modules/WIFI"
            "modules/MQTT"
            "modules/DHT11"
            "modules/TCP"
          )
