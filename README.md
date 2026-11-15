# ⚽ PES 2013 para Windows

[![Descargar el ZIP para Windows](https://img.shields.io/badge/Descargar%20el%20ZIP-Windows%20x64-168444?style=for-the-badge&logo=github)](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/download/v0.5.8-mandos/PES13-Preview-Windows.zip)

[Descargar PES13-Preview-Windows.zip](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/download/v0.5.8-mandos/PES13-Preview-Windows.zip) · [Ver la versión de prueba](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/tag/v0.5.8-mandos)

## Cómo arrancar

1. **Extraé todo el ZIP** en una carpeta corta, como `C:\PES13`. Dejá unos **10 GB libres** para preparar el juego, más el espacio que pida Microsoft si falta instalar sus herramientas. Usá Windows de 64 bits, una placa de video con soporte para Vulkan y, para compilar, unos 16 GB de RAM.
2. **Abrí `PES 2013.exe`** y elegí tu propia ISO de **PES 2013 para Xbox 360, edición Europa (Es,Pt)**. La copia probada figura como `PES 2013 - Pro Evolution Soccer (Europe) (Es,Pt).iso`. Esa edición trae español.
3. **Tocá Preparar y, cuando termine, tocá Jugar.** El asistente genera el juego en tu PC. Si hacen falta herramientas de Microsoft, el asistente las descarga y Windows puede pedirte permiso. Si pide reiniciar, volvé a abrir **`PES 2013.exe`** después. Las próximas veces, ese mismo archivo abre el juego.

El ZIP trae Python portátil para el lobby. La preparación se hace desde la ventana de **PES 2013**, con el avance en pantalla.

## Buscá actualizaciones desde F1

Si venís de la **v0.4.0**, cerrá el juego y extraé el ZIP nuevo sobre su carpeta. Abrí **PES 2013.exe** y tocá **Actualizar juego** para aprovechar la ISO y la compilación que ya tenés.

Desde la **v0.5.3**, abrí **F1 → Buscar actualizaciones**. Si hay una versión nueva, cerrá la ventana del juego, volvé al lanzador y tocá **Instalar actualización**. El lanzador verifica la descarga de GitHub y solo compila las partes que cambiaron. Si cambia el recompilador o su configuración, puede volver a generar código a partir de tu ISO ya preparada.

Desde la **v0.5.5**, el panel de **F1** muestra la versión instalada junto al título del lobby. Al abrirlo, PES busca actualizaciones en segundo plano. Si encuentra una más nueva, el botón muestra **Actualizar a** y el número de versión. Tocá ese botón para abrir el actualizador.

## Probá un amistoso desde una sala pública

1. Abrí dos ventanas de **PES 2013.exe** en la misma PC. En ambas, entrá a **Partido → Partido amistoso** y quedate donde se elige lado.
2. Apretá **F1** en cada ventana y conectate con apodos distintos. En una elegí **Crear sala pública**. En la otra, elegí la sala de la lista y tocá **Unirme**. PES conecta los mandos automáticamente.
3. Cuando diga **Conectados**, volvé al juego, asigná **Local** y **Visitante** y verificá que las dos copias muestren los mismos equipos. Si se desincronizan, los CSV se guardan solos en `registros-red`.

El lobby automático de esta versión funciona en la misma PC. Para probar entre dos casas, actualicen los dos. Entren a **Partido amistoso** y quédense en el selector de lado. En **F1 → Jugar amistoso por red**, el anfitrión toca **Por Internet**. Cuando aparezca el código, pasáselo entero a tu amigo para que lo pegue y toque **Unirme**. Cuando diga **Conectados**, vuelvan al juego. El anfitrión mueve su mando hacia **Local** y el invitado hacia **Visitante**. [Cloudflare Quick Tunnels](https://developers.cloudflare.com/tunnel/get-started/quick-tunnels/)

También podés usar **En esta red** en la misma red local. **IP directa** usa la IPv4 pública y el puerto TCP 28513 del anfitrión si ya configuró su router. El archivo **PRUEBA DE RED.txt** explica estas opciones. [Servicio de detección de IP](https://www.ipify.org/)

## Probá el lobby y el chat

> Las salas, las invitaciones y el chat se prueban con dos ventanas en la misma PC. Usá **Medir ping** para verificar la latencia. **Jugar amistoso por red** inicia la sesión que comparte los mandos. El rollback sigue en desarrollo.

1. Abrí dos ventanas de `PES 2013.exe`, apretá **F1** en cada una y conectate con apodos distintos. Si ambas usan el mismo apodo guardado, tocá **Desconectarme** en una, cambialo y volvé a conectarte.
2. Para probar una invitación privada de chat, seleccioná al otro jugador, tocá **Invitar** y aceptá desde la otra ventana.
3. Cuando figure el rival, apretá **F2** para escribir y **Enter** para enviar. Probá `/casi`, `/var` o `/payaso` para tirar un emoji que aparece en las dos ventanas. **Esc** te devuelve al juego.

## Si te trabás

- **Falló la preparación.** La ventana muestra el motivo y dónde quedó el registro. Si Microsoft pide reiniciar, hacelo antes de abrir el juego de nuevo.
- **La ISO no pasa la verificación.** Revisá que sea la edición **Europa (Es,Pt) de Xbox 360**. El asistente verifica que coincida con la versión usada para compilar.
- **El lobby de F1 no conecta.** Revisá que hayas extraído el ZIP completo, incluida la carpeta `Soporte\Python`, y volvé a abrir PES.
- **La prueba por código da un error de red.** Anotá el número que muestra. **10049** indica que la IP que escribió el anfitrión no está disponible en su PC. **10061** indica que la conexión fue rechazada. Confirmá que la sala siga abierta y revisá la IP y el puerto. [Códigos de Winsock](https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2).
- **El túnel no llega a Cloudflare.** Prueben que el otro jugador cree la sala. La PC anfitriona necesita llegar a Cloudflare por el puerto saliente 7844. La PC que se une usa HTTPS por el puerto 443. [Puertos de Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/configure-tunnels/tunnel-with-firewall/)

## Código y créditos

Para compilar desde un clon del repositorio, seguí la [guía técnica en español](docs/lobby-preview-es.md). El [estado del desarrollo](docs/online-development.md) explica qué se probó del multijugador.

Basado en [PES13Recomp de xamphy](https://github.com/xamphy/PES13Recomp/tree/70f63ae36a06b630784a25998677953506b55cd7). El código se distribuye bajo [GPL-3.0](LICENSE). Las dependencias conservan sus licencias. Pro Evolution Soccer 2013 pertenece a Konami. Este es un proyecto no oficial.
