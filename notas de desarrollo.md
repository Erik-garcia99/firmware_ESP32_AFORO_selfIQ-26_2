 

--- creo que necesitare reescribir UART, porque no me acuerdo que cosas puse, aparte que ya no quiero tener harcodeado mis credenciales 

--> lo que tengo que volver  ver que onda sera el parse, para saber que es lo que se ingreso por UART, esto 

pero es que la verdad, me estoy matando por UART y realmente no lo utilicare para realizar setup al esp, porque usaremos un HTTP server, porque ni modo que a loq eu le vamos a vender esta porqueeria les vamos a poner uart 

--> entonces pues creo que lo debemos de quitar UART, o tiene que ser mucho mas simple, UART estaba para el proyecto anterior que necesitaba rasberry. 



### que hace en este momento el codigo actual-pasado 

actualente lo que planeamos hacer hora con la rasberry, en este momento lo realiza la ESP, la ESP lanza la web con acces point, por lo que tenemos que qut=itarlo, porque ahora la credenciales lo obtendra la rasberry.




# cambio de planes 


a traves de la rasberry es por donde ahora nos vamos a conectar, a que me refiero 

--> rasberry pi levanta una red local en AP 

--> lo que hacia antes la ESP ahora lo hara la rasberry (porque no encuentro una forma de enlazar todo en un conjunto, a fuersa tiene que ir separados)


--> entonces UART, creo que lo vamos a sacar, porque degub por mientras lo haremos con ESP_LOG, no por UART


--> entonces la rasberry levanta una web sencilla para ingresar datos, 

--> despues levanta una res AP, en la cual tendra credenciales por defecto, con la cual los esp32 se podran conectar (**pero en cuesion de seguridad, creo que no es muy factible que tenga una credenicial por defecto**)

--> tal vez si, porque es la red interna, que no tiene salida a la red, **al menos que pr medio de la app, halla una opcion en la cual indique que queremos configurar un nuevo dispositivo, saldra una animacion o alguna mamada asi, es cunado la rasberry enciente la AP, realiza la conexion, pero esto puede esperar a x dispositvos nuevos, puede ser que epere un x tiempo en donde no reciba solciutudes entoncs dara por hecho que ya no hay dispositovs, puede ser, algo que solo lo puede activar el usuro admi, y las credeniclaes en codigo tienes que estar cifradas.**


---> escogemos TCP porque tiene mecanismos que verifican la entrega que UDP no tiene y asi nos aseguramos que las credenciales lleguen con exito :

---> **que necesitamos para l setup de credenicales WIFI.**
-> ocupo conocer primero las credenicales del AP de la rasberry pi. 

--> **credeniclaes por mientreas**
--> **SSID: SelfIQ-Admi**
--> **PSWD: $elfIQ-4dmi-1**

pero estas deben de estar cifradas, las vamos a hardcodear, entnces estaran cifradas para que posibles atacantes no seran que onda. 

**Formato de la trama**

--> necesitaria algo con lo cual me deberia de identificar. 
--> lo de accion podemos mantenerlos, porque podemos enviar que queremoes solciitrar crecedenciales puede funcionar para otras cosas mantener esta estrucutra de tener un espacio para indicar lo que se requiere hacer, aunque por ahora solo sera login, despes podremos meter algo mas. 


--> esto sera cunado enviemos  o si la RPI solicita algo a la ESP32, por socket. 
 | HEADER | LEN | TYPE_FRAME |ACTION  | DATA |
      2            1                                1                      98   = 102 bytes, 
dejaremos el socket un poco general, lo que quiere decir que, vamos a dejar la posibilidad de poder relizar mas que solo la conexion, creo que es lo mas adecuado 


**respuesta desde la RPI -> ESP32 o  ESP32->RPI**

*necesitaremos *

|ACK| LEN | ACTION |TYPE_FRAME| DATA |

|NACK| LEN | ACTION |TYPE_FRAME| DATA |
|NACK| 0xFF | F |F| 0 | -> esto es lo que consta NACK 



entonces para :

ACTION : indicaremos que se realizara, una peticion a la RPI o alguna otra operacion que requeramos de la RPI, 

TYPE_FRAME_T : indicara que tipo de frame es 
			
		 0 : no tipo, porque estamos solicitando ESP32 -> rpi
		 1 : para credeniclaes WIFI normales RPI -> ESP32
		 2:  para credeniciales WIFI de empresa RPI -> ESP32 (esto por ahora )

*formato en como llegan las credenciales*
frame->data = |id_ssid| |len |SSID| |id_pswd|| len |data|   -> para redes normales provadas

rame->data = |id_ssid |len |SSID | id_pswd | len | data |id_usr | len |data -> este para redes de empresas. sera el mas largo, y el que creo no usar tanto, quiero probar en UABC.  




---

# comunicaciones 


--> tenemos 3 escenarios los cuales son, 
	*1- no hay credenicales la ESP32 tiene la flash de datos borrada, mas que el propio firmware*
	*2- hay credenicales pero estan corruptas o se modificaron las credenucales por lo que la rasberry enviara un socket indicando el cambio de credenucales*
			*en el caso que esten corruptos, entonces el keep_alive no sera enivado por lo que en la APP se mencionara que incie comunicacion para poner en liena el nodo X, y es cunado se tiene que volver a hacer el proceso de solcitar crecniales- actualziar - conectar y verificar la conexion*
	*3- el internet esta caido, las credenicales estan OK pero ocurrio un error de conexion, no hay WIFI puede ser, y lo mas ideal es cunado se entra en modo OFFLINE.*


--> okay muy bien, dentro del proceo de conexion en donde se comprueba si hay conexion es en donde estara entrando cuando se pierde la conexion WIFI. 

	--> en este punto es cunado 


una froma de validar que las credenciales esten correctas puede ser aplicando un **checksum** con esto podemos averifgar con mas certeza en que caso estamos lideando, si el **cehcksum** de las credenciales de WIFI, MQTT o TCP estan correctas, entonces paso otro caso que no son las crednciales a coenctarnos 

**en donde entrara el checksum?**
antes de actualizar credenicaes o levantar de nuevo el servicio debemos de verificar que en verdad, para el BIT WIFI_FAIL_BIT queindica que si hay crednialces pero no se pudo conectar en este va el checksum 


comunicacion constante con la rasberry, si cierro TPC SERVER en la rasberry se cerrara en automatico en la ESP32. 

entonces una forma de validar que todo esta correct es el KEEP_ALIVE en MQTT envinado y recibidno, ESP32->RPI --- RPI->ESP32

cuando no se reicban confirmacion de ambos sentidos entre 5 a 10 minutos entonces indicara un probrema en la comnuicaiocn por lo que se levantara los servicios de TPC entre ambos para averigar que es lo que paso, que esta fallando. 

---
## formato de comunicacion topicos en MQTT 

--> creo que debo de relizar el modelo de base de datos. 

en este punto la esp32 ya tiene red. 

entonces ahora por MQTT va a solcitar registrarse en el sistema del programa para indentificrse y llevar el control. 




---



redes
INFINITUMF4AF
nFukH34MPW

usuario sudo de app de prueba. 
- **Correo:** `sudo@selfiq.local`
- **Contraseña:** `Sudo123!`

ip rasberry: http://192.168.50.1


tokens de prueba 

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIwNmQ2MjI2NC1hZTA0LTQ3NDAtOTJhMy0wNzliOGRlYjc5MjQiLCJpYXQiOjE3OTIwMTI5ODh9.fiB2P_G1li1Oh_TdLXj6ElVW_Vs0918PiWi9ytTjUoI",
  "token_type": "bearer",
  "user": {
    "id": "06d62264-ae04-4740-92a3-079b8deb7924",
    "email": "sudo@selfiq.local",
    "first_name": "Erik",
    "last_name": "Administrador",
    "is_superuser": true,
    "account_status": "ACTIVE",
    "role": "sudo",
    "organization_id": "f19209ad-7f92-4244-b3a1-97c2f5e923c2",
    "branch_id": null
  }
}
```

id: tienda de prueba: d07018c7-99c1-490f-95b9-dd54589cf60b

```json
{
  "branch_id": "d07018c7-99c1-490f-95b9-dd54589cf60b",
  "branch_token": "E8Znyoj36ORwwJeTJ9dDE22nVQewJPltGagpI8nxps8"
}
```









construye y levnata dokcer: ```
docker compose up --build -d backend

----

docker compose up --> levante el servicio y deja los lsogs abiertos, pero si se modifica el docker pues necesitamos volver a construir

acceder a la base de datos:

docker compose exec db psql -U selfiq -d selfiq






# Configuración del desarrollador/instalador. No se solicita en la página.

SELFIQ_BACKEND_WS_URL=ws://192.168.1.66:8000/ws/gateways


----
# cargar a la esp32


idf.py -p /dev/ttyUSB0 flash monitor


dar permisos

sudo usermod -aG uucp "$USER"



---
# cambios en proyecto 


### eliminamos la conexion de empresa 
esto porque me esta dando problemas al momento de lanzar la rasbeerry, no conecta y hay algo tal vez un bug o es algo que la red de UABC no me deje coenctar mi rasberry, entonces lo que haremos es quitar esta opcion, estara en un branch, pero mejor lo quitamos. 

	
