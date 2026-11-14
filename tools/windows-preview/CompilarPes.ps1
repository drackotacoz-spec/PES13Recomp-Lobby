param([string]$Iso, [switch]$CheckTools, [switch]$Gui, [switch]$Incremental, [switch]$Regenerate)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$repo = Join-Path $root 'PES13Recomp'

if ($Gui) {
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    $OutputEncoding = [Console]::OutputEncoding
}

trap {
    $message = ($_.Exception.Message -replace '[\x00-\x1f]+', ' ').Trim()
    if (!$message) { $message = 'Ocurrio un error al preparar PES 2013.' }
    if ($message.Length -gt 280) { $message = $message.Substring(0, 277) + '...' }
    if ($Gui) {
        [Console]::Error.WriteLine("PES_ERROR: $message")
    } else {
        Write-Error -Message $message -ErrorAction Continue
    }
    exit 1
}

. (Join-Path $repo 'tools\windows-preview\BuildTools.ps1')

function Invoke-Checked([string]$Program, [string[]]$Arguments, [string]$Failure) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Failure Consulta el registro para ver el detalle."
    }
}

if ($CheckTools) {
    $problems = @()
    if (!(Use-PesBuildTools -Problems ([ref]$problems))) {
        throw "Faltan herramientas de C++: $($problems -join ', ')."
    }
    Write-PesBuildStatus 'Herramientas de C++ detectadas.' -Gui:$Gui
    return
}

if ($Gui -and !$Incremental -and (!$Iso -or !(Test-Path -LiteralPath $Iso -PathType Leaf))) {
    throw 'Elegi una ISO de PES 2013 Xbox 360 Europa (Es,Pt) que exista en tu equipo.'
}
if (!$Incremental) {
    Write-PesBuildStatus 'Preparando tu ISO de PES 2013...' -Gui:$Gui
    if ($Iso) {
        & (Join-Path $PSScriptRoot 'Preparar ISO.ps1') -Iso $Iso
    } else {
        & (Join-Path $PSScriptRoot 'Preparar ISO.ps1')
    }
}

$xex = Join-Path $root 'game\xex\default.xex'
if ($Incremental -and !(Test-Path -LiteralPath $xex -PathType Leaf)) {
    $xex = Join-Path $repo 'game\xex\default.xex'
}
if (-not (Test-Path -LiteralPath $xex -PathType Leaf)) {
    if ($Incremental) { throw 'Primero prepara el juego con tu propia ISO desde el lanzador.' }
    if ($Gui) { throw 'No se pudo preparar la ISO. Elegi tu copia de PES 2013 e intenta de nuevo.' }
    Write-Host 'Compilacion cancelada. Volve a abrir PES 2013 para elegir la ISO.'
    exit 0
}
Initialize-PesBuildTools -Gui:$Gui

$recompilerInput = Join-Path $repo 'game\xex\default.xex'
if ($xex -ne $recompilerInput -and (!$Incremental -or !(Test-Path -LiteralPath $recompilerInput -PathType Leaf))) {
    Copy-Item -LiteralPath $xex -Destination $recompilerInput -Force
}

$links = Join-Path $repo 'thirdparty\xenia_third_party\third_party'
$null = New-Item -Path $links -ItemType Directory -Force
foreach ($name in 'fmt', 'glslang', 'xxhash') {
    $link = Join-Path $links $name
    if (Test-Path -LiteralPath $link -PathType Container) { continue }
    $target = if ($name -eq 'xxhash') {
        Join-Path $repo 'tools\XenonRecomp\thirdparty\xxHash'
    } else {
        Join-Path $repo "thirdparty\$name"
    }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) {
        throw "Faltan archivos del ZIP: $target."
    }
    $null = New-Item -Path $link -ItemType Junction -Target $target
}

$commonCMake = @(
    '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
    '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl', '-DCMAKE_LINKER=lld-link',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
    '-DCMAKE_EXE_LINKER_FLAGS_INIT=/MANIFEST:NO', '-DCMAKE_SHARED_LINKER_FLAGS_INIT=/MANIFEST:NO'
)
$parallel = @('--parallel', '6')

if (!$Incremental -or $Regenerate -or !(Test-Path -LiteralPath (Join-Path $repo 'generated\ppc_func_mapping.cpp'))) {
    Write-PesBuildStatus 'Paso 1 de 3. Compilando el recompilador...' -Gui:$Gui
    $recompilerSource = Join-Path $repo 'tools\XenonRecomp'
    $recompilerBuild = Join-Path $recompilerSource 'build'
    Invoke-Checked 'cmake.exe' (@('-S', $recompilerSource, '-B', $recompilerBuild) + $commonCMake) 'No se pudo configurar el recompilador.'
    Invoke-Checked 'cmake.exe' (@('--build', $recompilerBuild, '--target', 'XenonRecomp') + $parallel) 'No se pudo compilar el recompilador.'

    Write-PesBuildStatus 'Paso 2 de 3. Recompilando el contenido de tu disco...' -Gui:$Gui
    Push-Location -LiteralPath $repo
    try {
        & (Join-Path $repo 'tools\recomp.ps1')
    } finally {
        Pop-Location
    }
}

Write-PesBuildStatus 'Compilando los archivos de PES 2013 que cambiaron...' -Gui:$Gui
$build = Join-Path $repo 'build\Release'
Invoke-Checked 'cmake.exe' (@('-S', $repo, '-B', $build) + $commonCMake) 'No se pudo configurar PES 2013.'
Invoke-Checked 'cmake.exe' (@('--build', $build, '--target', 'pes13') + $parallel) 'No se pudo compilar PES 2013.'
$destination = Join-Path $root 'Jugar PES 2013.exe'
$temporary = Join-Path $root ('pes13-build-' + [guid]::NewGuid().ToString('N') + '.tmp')
try {
    Copy-Item -LiteralPath (Join-Path $build 'bin\pes13.exe') -Destination $temporary
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        [IO.File]::Replace($temporary, $destination, [System.Management.Automation.Language.NullString]::Value)
    } else {
        [IO.File]::Move($temporary, $destination)
    }
} catch {
    throw 'No se pudo guardar el juego compilado. Cerra otras ventanas de PES 2013 y volve a intentar.'
} finally {
    if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue }
}
Write-PesBuildStatus 'Listo. Podes abrir PES 2013. F1 abre el lobby.' -Gui:$Gui
$versionFile = Join-Path $PSScriptRoot 'version.txt'
if (Test-Path -LiteralPath $versionFile -PathType Leaf) {
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'version-compilada.txt'),
        (Get-Content -LiteralPath $versionFile -Raw).Trim(), [Text.Encoding]::ASCII)
}
