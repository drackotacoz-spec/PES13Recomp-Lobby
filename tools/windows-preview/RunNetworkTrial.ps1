$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$root = Split-Path -Parent $repo
$game = Join-Path $root 'Jugar PES 2013.exe'
if (!(Test-Path -LiteralPath $game -PathType Leaf)) {
    throw 'Primero termina Preparar y compilar.cmd con tu ISO de PES 2013 para Xbox 360 Europa (Es,Pt).'
}

$xex = 'game\xex\default.xex'
if (!(Test-Path -LiteralPath (Join-Path $root $xex) -PathType Leaf)) {
    $xex = 'PES13Recomp\game\xex\default.xex'
}
if (!(Test-Path -LiteralPath (Join-Path $root $xex) -PathType Leaf)) {
    throw 'No se encuentran los archivos del juego. Ejecuta Preparar y compilar.cmd.'
}

$id = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), [Guid]::NewGuid().ToString('N').Substring(0, 8)
$session = Join-Path $root "registros-red\$id"
$null = New-Item -Path $session -ItemType Directory -Force
$env:PES_EXPERIMENTAL_SYNC = '1'
$env:PES_FRAME_TRACE = 'cuadros.csv'
$env:PES_PAD_TRACE = 'mandos.csv'
$env:PES_MATCH_TRACE = 'partido.csv'
Remove-Item Env:PES_TEST_SECOND_PAD -ErrorAction SilentlyContinue
Remove-Item Env:PES_CONTROL -ErrorAction SilentlyContinue

Start-Process -FilePath $game -WorkingDirectory $session -ArgumentList '--run', "..\..\$xex"
Write-Host "Registros de la prueba: $session"
