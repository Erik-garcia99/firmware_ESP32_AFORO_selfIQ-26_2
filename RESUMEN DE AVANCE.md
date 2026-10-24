


okay, con mi proyecto, ocupo que me ayudes a saber en que etapa del desarrollo estamos, 


por el momento por la parte de hardware, llevamos que tanto la RPI y la ESP32 tiene un mecanimso para poder conectarse aun falta la parte de detaercaar el posible fallo, donde lanza el offline, em modo offline donde se guardara en memoria de la RPI con SQLite, es lo que falta

poer ahora la ESP32 igual puede solcitar credenciales WIFI, busca el broker, se conecta lanza una peticion de registro en el sistema, se registra, en la base de datos y la ESP32 obtiene su identificador en el sistmea. 

por ahora no se suscribe a nada ni nada, porque esoty con un firmware base, un firmware que los demas esp32 compartiran casi igual, pero los topcios en donde se vana suscribir algunso seran diferenes otros seran iguales porque seran como este caso, solicitar un dientificador, otras cosas aparte del funcionamiento propio de cada micro, uno es camara, el otro es con led, el otro controla aforo, etc.. me explico, por ahora solo en lo basico que todos los esp32 comparitran 



entonces por ahora creo que me haria falta avancar en forntend, por ahora en la parte de administradores que es el que tiene como la base, el usuario pues le quitamos cosas y para ventas es muy parecido solo que tiene el modulo de venta agregado.


entonces que me sugieres seguir? ya cuento con todo lo necesairo, tengo el rad hlk-2450, tengo la esp32'cam, solo no tengo esp32-supermini, pero pues tengo esp32 solo tengo que cuidar el firmware que no sea muy exigente, 

y avanzar igual con el backedn y eso, pero tu que me aoncsejar a revisando mis requeireintos y como va el proyecot 

algo en donde creo que me puedo atorar pero podremos hacerlo simple o mas o menos es en la edicion del mapa, aun no tenga una idea, de esto, por ahora dame tus ideas, en que crees que demamos seguir, yo tenia una idea de ir avanzo en firmware - backedn - forntend asi en ese orden . 


---

# OFFLINE

okay, muy bien, ahora ya se conecta y todo, pero ahora falta algo importante, algo que igual se comparte entre todas las eso32 es una funcionalidad en comun lo cual es la parte de offline, la cosa aqui es que en el caso de falla de conexion y que sea que fallo porque no hay internet o se cambiron las credenciales de WIFI, entonces lo que pasara es que perdera WIFI, no es asi entonces no podra mandar datos a la base de datos que captura telemetria influxDB ni a postgres SQL, (aun no tiene integracion con influx DB. entonces aun falta esta parte), entonces como son 2 tipos de bases de datos SQL y noSQL, tengo entendidos que influxDB es noSQL, entonces no se que onda como quedaria eso, por lo que la RPI tendra que comprobar que en efecto no hay conexion a internet para eso en los ESP32 cree checksum para corroborar credeniclaes de este tipo, no se si lo agregaste a la RPI, si no, seria necesario agregalro, entonces si pierde conexion, al igual que la RPI, intenta conectarts desde el modulo de conexiones o no se que odna que es network.py intena conectart al menos unas 5 a 10 veces, por unas 4 llamas osea entre 20 veces no?, no lo logra --> indicame que no se logro relaizar la conexion, por terminal por ahora, un pirnt, un ESP_LOG, verdad, porque despues requerie de esto: si pasa un error en los esp32 o en la RPI a ciencia cierta si no esta conectada a WIFI y en la app no podremos saber que tipo de error esta ocurrioned, se me ocurrio unos led, un LED RGB que con algun color indique el tipo de grupo de problema, no podemos poner un color para el probelam, puede ser rojo, WIFI algo paso con la conectividad, verde MQTT, azul otra cosa y los colores que pueda dar el RGB, sobre grupo, conceptos no tanto comportainetos en especificos, si no ese problema en donde estaria engrupado, es algo que involucra WIFI, o el servidor TCP etc.. me explico? --> pero aun no, porque aun no tengo del todo una manera de simular esto, se que puedo deconectar wl WIFI, entonces la ESP32 se reincia pero eso pasa solamente si no tiene credenicales de WIFI o MQTT registradas, no?, pero si se puede conectar por TCP al AP de la RPI, etnonces esta todo bien, no requiere el reinicio, el reinicio es por si tiene datos o algo que esta impidinedo la conection TCP con la RPI, si al final indica, no hay conexion WIFI, la RPI, lo que debe de hacer es incialzar la DB interna guardado el flash, y tiene que volver a levantar la red AP, entonces ahora la IP del broker ha caminado o usa el mismo?, si usa la misma IP que la del servidor TCP, entonces riemro que intenta conectar no?, si no lo logra, entonces solicita por TCP un infrome del estado, la RPI infromara un codigo de error o algo que le idnique a la ESP que accion tomar, si ya de plano lo logra nada se tendra que reinciar, aun no se en donde pueda entrar el led pero en el camino antes del renicio, 

si se pudo conectar entonces la comunicacion sigue de manera normal nomas uqe por el momento se guarda de manera local 

peor cada x tiempo intentan a conectarse de vuelta cada minutos o 5 minutos intentan a conectarse de nuevo si no lo logran pues a seguirle hasta ahora hasta pero no dejemos que se llene la flash de datos, mi microsd en est momento es de 16 gb, de la RPI, pero me gustaria que fuera un porcentaje, por ejemplo si le emten uno de 128 o 256 caben mas datos no?, me imagniaba un 60 o 70% de capacidad de la memoria dejajdo ese otro 30%para lo que ocupe la placa el SO, etc, que igual creo que es mucho porcentaje, es porcentaje que puede abarcar la base de datos en flash. 

ahora si viene lo chido, el mecanismo para cunado ya se volvio a conectar a la red etnonces estos datos tiene que volverlos a neviar, poruqe va a etar recibiedo, lo que se me ocurria es que ahora la RPI envie u mensaje a todas las RPI dinciando como silencio, no evnien datos, las ESP32 no evniaran datos, la RPI empezara a subir al backend y el backedn a la DB, y cunado termine, la RPI borra la base de datos interna que guardo en modo offline, y ahora manda otro mensajes para idnicar que esta lista y entonces los ESP32 empeizan a volver a mandar mensajes en MQTT

es una idea muy vaga aun, pero si tu tienes un mecanismo mucho mejor y mas fiable, sientete con la libertad de propoiner




