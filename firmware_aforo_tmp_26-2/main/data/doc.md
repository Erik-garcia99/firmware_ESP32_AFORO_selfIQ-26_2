



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



-------------------
topicos y protocolo en MQTT 

lo primero, es que los ESP32 no conces la direccion del broker que esta en la rasberry pi, entonces necesita conocer la IP del broker. y el pureto tampoco lo sabra, pero debemos de indicar undo por defecto el de toda la vida port = 1883, ese seria el puerto por defecto, pero bien, el usuario pudo haberlo cambiado por eso, necesitamos soliitrar IP y puertos

entonces... 

estados detallando que la ESP32 no conoce nada de nada y cunado enciende es cunado va a estar solciitando todo. 

entonces la rasberry necesita una identificacion en todo el sistema, necesita algo que indica yo soy este y se guarde en flash, es una identificacion unica. 

por lo que lo que hara despues de poder conectarse a la red es por medio de sockets igual, por TCP, pedir la IP y el puerto al cual 

la rasberry respondera con el broker, la ip y el puerto al cual la rasberry se coenctarara, 

una vez recibido, entonces la rasberry debe de guardar esos datos igual en flash, casi lo mismo que en wifi, e inicar la comunicacion, 

lo primero que va a enviar sera un mensje a un topico en general. 

entonces una froma de identificarse, una identificacion que sabemos que es unica es la direccion MAC del esp. 

seria algo como:

selfiq/broker  -> como mensaje sera el MAC. 

entonces lo que hace la rasberry es darlo de alta en la base de datos, y el backedn regresara un identificador unico para ese ESP, que se utilizara en el backedn, la MAC es para saber que ese dispositivo no haya sido registrado si es asi entonces regresara el indentificador con el que esta dato de alta. 

y entonces el ESP32 debe de guardar ese dato igual en flash con el cual se identificara 

entonces de inciio a los topicos a los cuales de va a suscribir tendran que ser 

selfiq/broker

selfiq/broker/get_id/

creo que sos 2 ocupa, el broken no tendra usuario ni contrasenia, un poco inseguro pero asi lo dejaramos por miemntras. 

















