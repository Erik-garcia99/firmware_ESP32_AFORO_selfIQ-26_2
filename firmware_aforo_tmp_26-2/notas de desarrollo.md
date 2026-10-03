

--- creo que necesitare reescribir UART, porque no me acuerdo que cosas puse, aparte que ya no quiero tener harcodeado mis credenciales 

--> lo que tengo que volver  ver que onda sera el parse, para saber que es lo que se ingreso por UART, esto 

pero es que la verdad, me estoy matando por UART y realmente no lo utilicare para realizar setup al esp, porque usaremos un HTTP server, porque ni modo que a loq eu le vamos a vender esta porqueeria les vamos a poner uart 

--> entonces pues creo que lo debemos de quitar UART, o tiene que ser mucho mas simple, UART estaba para el proyecto anterior que necesitaba rasberry. 



# cambio de planes 


a traves de la rasberry es por donde ahora nos vamos a conectar, a que me refiero 

--> rasberry pi levanta una red local en AP 

--> lo que hacia antes la ESP ahora lo hara la rasberry (porque no encuentro una forma de enlazar todo en un conjunto, a fuersa tiene que ir separados)


--> entonces UART, creo que lo vamos a sacar, porque degub por mientras lo haremos con ESP_LOG, no por UART


--> entonces la rasberry levanta una web sencilla para ingresar datos, 

--> despues levanta 



---> aqui el problema va ser como establecer la redes de las esp32, 




