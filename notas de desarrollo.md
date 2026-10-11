

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
### POSIBLE USO A UART 

no lo eliminare del todo, lo comentare UART solo puede funcionar de manera de debug pero para mi, pero no eslgo principal, si lo ponemos lo pondremos hasta el final. 


--> no lo dejare en el codigo que se cargara al ESP, porque es codigo o comentarios que no aportan nada, solo es estorb visual. 



---
## DEBUG DE LOS PROGRSMAS ESP32 


el problema que estoy vinedo con los esp32, es en el caso de tener fallos, tener que ver que es lo fallo, puede ser un problema dado que no tiene salida por pantalla o algun lado que se pueda comunicar. una razon se me ocurre es por medio de **LEDS** de algunso colores, puede ser varios o 1 solo RGB indicando por medio de colores que puede ser el fallo. 



---

# nos quedamos 

actualiamos el frame cambiando TYPE_FRAME antes que accion, asi podemos descartar posibles acciones y conocer el tipo de frame que viene.

