# ⚽ PES 2013 para Windows

[![Descargar el ZIP para Windows](https://img.shields.io/badge/Descargar%20el%20ZIP-Windows%20x64-168444?style=for-the-badge&logo=github)](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/download/v0.3.1-netplay-test/PES13-Preview-Windows.zip)

[Descargar PES13-Preview-Windows.zip](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/download/v0.3.1-netplay-test/PES13-Preview-Windows.zip) · [Ver la versión de prueba](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/tag/v0.3.1-netplay-test)

## Cómo arrancar

1. **Descargá el ZIP de arriba y extraelo** en una carpeta corta, como `C:\PES13`. Dejá unos **10 GB libres** para preparar el juego. Usá Windows de 64 bits y una placa de video con soporte para Vulkan. Para compilar, se recomiendan 16 GB de RAM.
2. **Abrí `Preparar y compilar.cmd`.** Si faltan herramientas, escribí **S**, apretá **Enter** y aceptá el permiso de Windows. El asistente descarga **Microsoft Build Tools** con los componentes que hacen falta. Si pide reiniciar, reiniciá y volvé a abrir el mismo archivo.
3. **Elegí tu propia ISO de PES 2013 para Xbox 360, edición Europa (Es,Pt)**. Esa edición trae español. La copia probada figura como `PES 2013 - Pro Evolution Soccer (Europe) (Es,Pt).iso`.
4. **Esperá a que termine** y abrí **`Jugar PES 2013.exe`**. La primera compilación tarda. El asistente genera el ejecutable en tu PC a partir de tu disco.

## Probá un amistoso por red

1. Los dos abran **Jugar PES 2013.exe**. Entren a **Partido → Partido amistoso** y quédense donde se elige lado.
2. En los dos juegos, apretá **F1 → Probar amistoso por red**. El anfitrión crea la sala y le pasa el código al invitado.
3. Vuelvan al juego, elijan **Local** y **Visitante** y comprueben que los equipos coincidan. Si el partido se desincroniza, anoten en qué jugada pasó. Los CSV se guardan solos en `registros-red`.

La prueba es experimental. Desde casas distintas, el anfitrión tiene que ser alcanzable por **TCP 28513**. El archivo **PRUEBA DE RED.txt** del ZIP explica cómo compartir el código.

## Probá el lobby y el chat

> Las salas, las invitaciones y el chat se prueban con dos ventanas en la misma PC. **Conexión directa** sirve para medir el ping. El rollback sigue en desarrollo.

1. Abrí dos ventanas de `Jugar PES 2013.exe`, apretá **F1** en cada una y conectate con apodos distintos. Si ambas usan el mismo apodo guardado, tocá **Desconectarme** en una, cambialo y volvé a conectarte.
2. En una ventana elegí **Crear sala pública** y en la otra elegí la sala y tocá **Unirme**. Para probar una invitación privada, seleccioná al otro jugador, tocá **Invitar** y aceptá desde la otra ventana.
3. Cuando figure el rival, apretá **F2** para escribir y **Enter** para enviar. Probá `/casi`, `/var` o `/payaso` para tirar un emoji que aparece en las dos ventanas. **Esc** te devuelve al juego.

## Si te trabás

- **El asistente sigue pidiendo herramientas.** En Visual Studio Installer, elegí **Modificar** en Build Tools. Agregá **Desarrollo para el escritorio con C++**, **MSVC x64/x86**, **Clang**, **CMake/Ninja** y **Windows SDK**. Después volvé a abrir el asistente. [Instalador oficial de Build Tools](https://aka.ms/vs/17/release/vs_BuildTools.exe).
- **La ISO no pasa la verificación.** Revisá que sea la edición **Europa (Es,Pt) de Xbox 360**. El asistente verifica que coincida con la versión usada para compilar.
- **El lobby de F1 no conecta.** Instalá **Python 3.10 o posterior** con la opción para agregarlo al PATH y volvé a abrir PES. Podés comprobarlo en una terminal nueva con `python --version`. [Python para Windows](https://www.python.org/downloads/windows/).

## Código y créditos

Para compilar desde un clon del repositorio, seguí la [guía técnica en español](docs/lobby-preview-es.md). El [estado del desarrollo](docs/online-development.md) explica qué se probó del multijugador.

Basado en [PES13Recomp de xamphy](https://github.com/xamphy/PES13Recomp/tree/70f63ae36a06b630784a25998677953506b55cd7). El código se distribuye bajo [GPL-3.0](LICENSE). Las dependencias conservan sus licencias. Pro Evolution Soccer 2013 pertenece a Konami. Este es un proyecto no oficial.
