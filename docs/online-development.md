# Desarrollo de salas y rollback

El menú F1 está integrado en «Jugar PES 2013.exe». El anfitrión
crea una sala en una dirección IPv4 de su equipo. El invitado pega el código
que contiene dirección, puerto y un token aleatorio. El valor inicial
127.0.0.1 permite probarlo con dos procesos en la misma PC.

Cada jugador elige un apodo de 2 a 15 caracteres en F1. El apodo se
guarda en userdata/online-player.txt y se comparte con el otro jugador
al validarse la invitación. Los dos equipos necesitan el mismo protocolo.

El panel mide el tiempo de ida y vuelta entre los dos extremos de la sala
y lo muestra en milisegundos junto a las barras de señal. El modo de
diagnóstico de entradas usa un protocolo separado del sondeo de latencia.

Se verificó la creación, entrada por código y desconexión de una sala entre
dos instancias reales de PES 2013 en esa misma PC. Con el diagnóstico
activado, ambas intercambiaron 408 cuadros de entradas neutras durante
el arranque. La prueba no llegó al partido.

## Lobby local

F1 abre directamente el lobby. El coordinador en tools/online-lobby/lobby.py
escucha en 127.0.0.1:8767 por defecto. PES lo inicia en segundo plano
mediante Python 3 si hace falta. El apodo crea una presencia temporal.
Crear una sala pública abre un puerto TCP local temporal para el modo
experimental. Al unirse, el invitado recibe la conexión desde el lobby
y empieza el intercambio de mandos sin copiar el código.
Una invitación privada solo es visible para su destinatario y caduca
si no se responde. El emparejamiento confirma al rival.

Después de confirmar al rival, F2 abre el chat de sala desde el juego
o enfoca el cuadro de texto en F1. Enter envía texto o un comando de
reacción. Ambos jugadores ven los emojis sobre su ventana de juego.
El servicio conserva hasta 40 entradas de ese emparejamiento. El cliente
usa el identificador de la pareja al enviar mensajes para descartar envíos
si ya cambió la sala.

Para operar entre redes falta alojar el servicio y definir el transporte
de juego con conexión negociada o retransmisión. El coordinador local
no lanza el partido automáticamente.

## Pruebas de sincronización

- PES_FRAME_TRACE registra la actualización externa del juego en 0x821B0EA8
  junto con las entradas intercambiadas, el reloj y el cuadro de video local.
- PES_PAD_TRACE identifica si cada consulta de mando usó el intercambio de
  la sala o la entrada local. El primer campo es el cuadro de video local.
- PES_MATCH_TRACE registra el reloj de partido, el contador GameStep y el RNG
  alrededor de la inicialización y de las llamadas directas e indirectas
  del motor. La actualización interna 0x8286B718 contiene las etapas
  0x8286C030 y 0x8286C188. La huella excluye direcciones de memoria
  e identifica diferencias en esos campos, sin medir pelota ni jugadores.
- PES_EXPERIMENTAL_SYNC=1 habilita el intercambio de mandos por secuencia
  dentro de la actualización externa. El arranque directo lo deja desactivado.

Dos ejecuciones del mismo ejercicio de regate iniciaron una secuencia con la
misma semilla de RNG. Los campos observados coincidieron en los primeros 60
pasos y el RNG difería en el paso 60, aunque el reloj seguía igual. Los mandos
se accionaron por tiempo real durante la navegación. Hace falta repetir la
prueba con entradas fijadas por paso y una barrera de arranque común.

En una prueba exploratoria de amistoso en la misma PC, dos instancias
se conectaron en la pantalla previa con Canadá y Costa de Marfil ya
seleccionados. Las dos llegaron al saque inicial y leyeron los botones
derecha y B del invitado después de que empezó a correr el reloj.
En la primera secuencia compartida con el reloj en marcha, a ambas les
quedaban 17997 ticks, pero el valor observado del RNG fue 112 y 138.
Por tanto, el intercambio de mandos funciona durante el juego y los
campos observados del partido ya difieren al comienzo. Todavía falta
sincronizar el estado desde la inicialización.

Para comprobar rollback, el siguiente objetivo es guardar el estado que afecta
el partido, restaurarlo y volver a ejecutar varios pasos con las mismas
entradas. La comprobación debe incluir la pelota, los jugadores y el marcador.
