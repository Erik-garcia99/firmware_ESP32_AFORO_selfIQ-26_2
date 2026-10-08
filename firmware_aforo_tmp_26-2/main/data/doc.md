



---- 
update_setup_cred(): 
  --> en identificador podra recibir: 
      --> para establecer credenciales de WIFI :  SETUP_WIFI
      --> para ceredenicales de empresa: SETUP_ENTERPRISE_WIFI
      --> para establecer la conexion con el servidor TCP: SETUP_TCP_CLIENT
      --> para establecer el broker MQTT : SETUP_BROKER_MQTT
      --> para establecer credeniclaes de usuario MQTT : SETUP_BROKER_MQTT


**************************
tendran que ser tramas binarias, no tramas en HEX, para hacer el paquete mas chico. 

@ UPDATE : modificar la estrucutra y lo de recv y send para manejrar tramas binarias 


nuevos HEADERS del frame 

para peticiones : 0xABCD
devolvinedo un ACK : 0x5433 -> solo es el complento A 2 de ABCD










