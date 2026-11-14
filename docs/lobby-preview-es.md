# Vista previa del lobby de PES 2013 para Windows

La vista previa permite probar el lobby de F1, crear una sala pública, invitar a otro
jugador y usar el chat de F2 entre dos ventanas de la misma PC. La conexión directa
por código permite probar el ping cuando dos equipos se pueden alcanzar por IPv4.
El amistoso por Internet y el rollback siguen en desarrollo.

## Requisitos

- Windows de 64 bits con un controlador gráfico compatible con Vulkan.
- Visual Studio 2022 con **Desarrollo para el escritorio con C++**, un Windows SDK y
  **Herramientas de C++ Clang para Windows** (`clang-cl` y `lld-link`).
- Git, CMake 3.31, Ninja y PowerShell. Los scripts de compilación cargan el entorno de
  Visual Studio. CMake y Ninja deben estar disponibles en ese entorno.
- Python 3.10 o posterior, disponible como `python.exe` al abrir el juego. El lobby
  usa la biblioteca estándar de Python.
- Aproximadamente 16 GB de RAM, espacio para extraer el disco y espacio adicional
  para compilar.
- Tu propia ISO de PES 2013 para Xbox 360, Europa (Es,Pt), que incluye español y
  portugués. La edición probada tiene Media ID `183C6A2F` y XEX `0.0.0.8`.

## Compilar

Cloná la rama de la vista previa desde GitHub y, en PowerShell, entrá a la raíz del
clon. Estas instrucciones descargan las dependencias fijadas en el repositorio.

```powershell
git submodule update --init tools/XenonRecomp thirdparty/fmt thirdparty/glslang thirdparty/volk thirdparty/Vulkan-Headers thirdparty/xenia
git -C tools/XenonRecomp submodule update --init --recursive
```

Cada persona usa su propia ISO de la edición que incluye español. Cambiá la
carpeta del ejemplo por la que contiene tu copia.

```powershell
.\tools\extract-xiso\artifacts\extract-xiso.exe -d game\files 'C:\Juegos\PES 2013 - Pro Evolution Soccer (Europe) (Es,Pt).iso'
Copy-Item -LiteralPath game\files\default.xex -Destination game\xex\default.xex
Get-FileHash -LiteralPath game\xex\default.xex -Algorithm SHA256
```

El SHA-256 esperado de `default.xex` para la versión ES/PT probada es
`F1389BCF5FF3F771C9AEBCB9AE7D4F45D2FE54127268BA5DED3BF1E5A129A52E`.

Prepará los enlaces que usa el traductor de shaders de Xenia. En un disco NTFS,
los enlaces de unión funcionan desde una sesión de PowerShell normal.

```powershell
$dependencies = Join-Path $PWD 'thirdparty\xenia_third_party\third_party'
New-Item -ItemType Directory -Path $dependencies -Force | Out-Null
$targets = @{
    fmt = 'thirdparty\fmt'
    glslang = 'thirdparty\glslang'
    xxhash = 'tools\XenonRecomp\thirdparty\xxHash'
}
foreach ($name in $targets.Keys) {
    $destination = Join-Path $dependencies $name
    if (!(Test-Path -LiteralPath $destination)) {
        New-Item -ItemType Junction -Path $destination -Target (Resolve-Path -LiteralPath $targets[$name]).Path | Out-Null
    }
}
```

Generá el código a partir de tu copia y compilá el ejecutable. Continuá al paso
siguiente cuando el anterior haya terminado correctamente.

```powershell
.\tools\build-xenonrecomp.bat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\recomp.ps1
.\tools\build-runtime.bat
Copy-Item -LiteralPath .\build\Release\bin\pes13.exe -Destination '.\Jugar PES 2013.exe'
```

Abrí **Jugar PES 2013.exe** con doble clic desde la carpeta del clon. Ahí encuentra
tu ISO extraída y el servicio local. La primera ejecución puede tardar mientras
el juego prepara los shaders. El registro de compilación queda en
`build/Release/build.log`.

## Probar el lobby

1. Abrí dos ventanas de **Jugar PES 2013.exe** y presioná **F1** en cada una.
2. Conectate con dos apodos distintos. En una ventana creá una sala pública y
   entrá desde la otra. También podés elegir al otro jugador e invitarlo.
3. Cuando se confirme el rival, presioná **F2**, escribí un mensaje y enviá con
   **Enter**. Probá `/jaja`, `/uy`, `/payaso`, `/frio`, `/casi` y `/var`.
4. Salí de la sala y verificá que ambas ventanas vuelvan a mostrar los jugadores
   disponibles.

Al conectarte, F1 inicia en segundo plano el servicio en `127.0.0.1:8766` si
hace falta. Ambas ventanas comparten ese servicio. Para probar el coordinador
de forma independiente, ejecutá:

```powershell
Set-Location .\tools\online-lobby
python -B -m unittest -v test_lobby
```

[PES13Recomp de xamphy](https://github.com/xamphy/PES13Recomp) es el proyecto
base, revisión `70f63ae36a06b630784a25998677953506b55cd7`. La
[licencia GPL-3.0](../LICENSE) y los créditos originales se incluyen en el
repositorio. Cada persona genera su propio ejecutable a partir de su disco.
