# ⚽ PES 2013 para Windows

[![Descargar el ZIP para Windows](https://img.shields.io/badge/Descargar%20el%20ZIP-Windows%20x64-168444?style=for-the-badge&logo=github)](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/download/v0.1.0-lobby/PES13-Preview-Windows.zip)

[Descargar PES13-Preview-Windows.zip](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/download/v0.1.0-lobby/PES13-Preview-Windows.zip) · [Ver la versión de prueba](https://github.com/drackotacoz-spec/PES13Recomp-Lobby/releases/tag/v0.1.0-lobby)

## Cómo arrancar

1. **Descargá el ZIP de arriba y extraelo** en una carpeta corta, como `C:\PES13`. Dejá unos **10 GB libres** para preparar el juego. Usá Windows de 64 bits y una placa de video con soporte para Vulkan. Para compilar, se recomiendan 16 GB de RAM.
2. **Instalá las herramientas una sola vez.** En Build Tools 2022 marcá **Desarrollo para el escritorio con C++** y asegurate de incluir **Clang para Windows**, **CMake/Ninja** y el **SDK de Windows**. Para el lobby instalá **Python 3.10 o posterior** y agregalo al PATH. [Build Tools 2022](https://aka.ms/vs/17/release/vs_BuildTools.exe) · [Python para Windows](https://www.python.org/downloads/windows/).
3. **Abrí `Preparar y compilar.cmd`** y, en el selector, elegí **tu propia ISO de PES 2013 para Xbox 360, edición Europa (Es,Pt)**. Esa edición trae español. La copia probada figura como `PES 2013 - Pro Evolution Soccer (Europe) (Es,Pt).iso`.
4. **Esperá a que termine** y abrí **`Jugar PES 2013.exe`** con doble clic. La primera compilación tarda. El ZIP trae el código, las dependencias y el asistente. Cada persona genera su ejecutable con su propia ISO.

## Probá el lobby y el chat

> **Estado de esta versión.** Las salas, las invitaciones y el chat se prueban con dos ventanas en la misma PC. El amistoso entre dos PCs por Internet y el rollback siguen en desarrollo.

1. Abrí dos ventanas de `Jugar PES 2013.exe`, apretá **F1** en cada una y conectate con apodos distintos. Si ambas usan el mismo apodo guardado, tocá **Desconectarme** en una, cambialo y volvé a conectarte.
2. En una ventana elegí **Crear sala pública** y en la otra elegí la sala y tocá **Unirme**. Para probar una invitación privada, seleccioná al otro jugador, tocá **Invitar** y aceptá desde la otra ventana.
3. Cuando figure el rival, apretá **F2** para escribir y **Enter** para enviar. Probá `/casi`, `/var` o `/payaso` para tirar un emoji que aparece en las dos ventanas. **Esc** te devuelve al juego.

## Si te trabás

- **El asistente pide herramientas.** Abrí Visual Studio Installer, buscá Build Tools y usá **Modificar** para agregar los componentes del paso 2.
- **La ISO no pasa la verificación.** Revisá que sea la edición **Europa (Es,Pt) de Xbox 360**. El asistente verifica que coincida con la versión usada para compilar.
- **F1 no conecta.** Abrí una terminal nueva y probá `python --version`. Si Windows abre la tienda o no reconoce el comando, instalá Python desde el enlace del paso 2 y agregalo al PATH.

## Código y créditos

Para compilar desde un clon del repositorio, seguí la [guía técnica en español](docs/lobby-preview-es.md). El [estado del desarrollo](docs/online-development.md) explica qué se probó del multijugador.

Basado en [PES13Recomp de xamphy](https://github.com/xamphy/PES13Recomp/tree/70f63ae36a06b630784a25998677953506b55cd7). El código se distribuye bajo [GPL-3.0](LICENSE). Las dependencias conservan sus licencias. Pro Evolution Soccer 2013 pertenece a Konami. Este es un proyecto no oficial.
