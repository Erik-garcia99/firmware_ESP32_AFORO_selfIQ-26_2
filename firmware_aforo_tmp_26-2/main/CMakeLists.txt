idf_component_register(
    SRCS
        "main.c"
        "modules/WIFI/wifi_lib.c"
        "modules/TCP/tcp_lib.c"
        "modules/MQTT/mqtt_lib.c"
        "modules/DHT11/dht11_lib.c"
        "modules/HLK-LD2450/ld2450.c"

    INCLUDE_DIRS
        "."
        "modules/WIFI"
        "modules/TCP"
        "modules/MQTT"
        "modules/DHT11/"
        "modules/HLK-LD2450"

    REQUIRES
        mdns
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        espressif__mqtt
        esp_driver_gpio
        esp_driver_uart
)
