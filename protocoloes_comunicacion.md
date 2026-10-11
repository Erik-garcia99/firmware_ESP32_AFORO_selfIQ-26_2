# PROTOCOLOS DE COMUNICACION DE DEL SISTEMA 

## PROTOCOLO DE COMUNICACION PARA SETUP DEL SISTEMA 

cuando el producto de compra y se prende por primera vez, el sistem viene vacio, sin credenciales de ningun tipo, mas las por defecto para realizar esta setup. 

el sistema viene incorporado con una suit de procotolo de comunicacion de TCP, con el cual la rasberry pi como las ESP32 se comunicaran. 

el cerebro prinicipal, sera la rasberry pi, por lo que todo lo que pasa entre los diferentes controladores pasara por ella, y esta se encarga de enviar los datos a donde pertenecen, si un proceso local, a backend, base de datos, etc.. 

**como funciona el setup:**

1. cuando se enciente la rasberry pi y no tenga credenciales o las credenciales para WIFI no sean correctas por cualquer razon la rasberry lanzara una red *acces point* en la cual nos podremos conectar. 
2. esta red tiene las sigueintes credenciales por defecto: **SSID: SelfIQ-Admi,** **PSWD: $elfIQ-4dmi-1** 
3. una vez concetados a la red, se tendra que acceder a la siguiente IP desde cualquier navegador: **192.168.50.1**, en la cual se mostrara una paguina en donde se tendra 2 opciones de tipos de red, red wifi normal y red wifi empresarial en la cual comunmente se piede un usuario y contrasenia personal del usuario, aparte del nombre de la red.
![[Pasted image 20261010212356.png|433]]
4. despues de esto si en 5 minutos no se recibe solicutdes de peticion por parte de las esp32, el servidor TCP se cerrara por parte de la rasberry, pero se puede volver a levantar desde la aplicacion, por si se agrega un nuevo controlador, este pueda obtener las credenciales actuales. 





