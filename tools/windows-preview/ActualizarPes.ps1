param([switch]$Gui, [switch]$Check, [switch]$Apply, [string]$TargetTag)

$ErrorActionPreference = 'Stop'
$releaseRepo = 'drackotacoz-spec/PES13Recomp-Lobby'
$assetName = 'PES13-Preview-Windows.zip'
$archiveRoot = 'PES13-Preview-Windows/'

function Read-PesVersion([string]$Tag) {
    if ($Tag -cmatch '^v([0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5})(?:-[a-z0-9-]{1,40})?$') {
        return [version]$Matches[1]
    }
}

function Get-PesRelease($Release, [version]$Current) {
    $tag = [string]$Release.tag_name
    $version = Read-PesVersion $tag
    if ($Release.draft -or !$version -or $version -le $Current) { return }
    $asset = @($Release.assets | Where-Object { $_.name -ceq $assetName -and $_.state -eq 'uploaded' })
    if ($asset.Count -ne 1 -or $asset[0].digest -notmatch '^sha256:[a-f0-9]{64}$' -or
        $asset[0].size -le 0 -or $asset[0].size -gt 150MB) { return }
    $url = "https://github.com/$releaseRepo/releases/download/$tag/$assetName"
    if ($asset[0].browser_download_url -cne $url) { return }
    return [pscustomobject]@{ Tag = $tag; Version = $version; Url = $url
        Hash = $asset[0].digest.Substring(7); Length = [long]$asset[0].size }
}

function Get-PesRelativePath([string]$Name) {
    if ($Name -cmatch '^(PES 2013\.exe|LEEME\.txt|PRUEBA DE RED\.txt)$') { return $Name }
    if ($Name -cnotmatch '^(Soporte|PES13Recomp)/' -or $Name -match '[\\:\x00-\x1f<>|?*"]') { return }
    $parts = $Name.Split('/')
    foreach ($part in $parts) {
        if (!$part -or $part -match '[ .]$|^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)' -or
            $part -in '.', '..', 'game', 'generated', 'userdata', 'build', '.git', 'registros-red', 'Actualizaciones', 'Registros' -or
            $part -in '.preparando.lock', '.actualizando.lock', 'version-compilada.txt') { return }
    }
    return $Name
}

function Get-PesDestination([string]$Base, [string]$Name) {
    if (!(Get-PesRelativePath $Name)) { throw 'El paquete contiene una ruta de actualizacion invalida.' }
    $path = $Base
    foreach ($part in $Name.Split('/')) {
        $path = Join-Path $path $part
        $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'La carpeta contiene un acceso enlazado. Extrae el ZIP en una carpeta normal.'
        }
    }
    return $path
}

function Get-PesUpdateFiles([string]$Zip, [string]$Staging, [string]$Tag) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $files = [Collections.Generic.List[string]]::new()
        $total = 0L
        if ($archive.Entries.Count -gt 15000) { throw 'El paquete tiene demasiados archivos.' }
        foreach ($entry in $archive.Entries) {
            if (!$entry.FullName.StartsWith($archiveRoot, [StringComparison]::Ordinal) -or !$seen.Add($entry.FullName)) {
                throw 'El ZIP no tiene la estructura esperada.'
            }
            $name = $entry.FullName.Substring($archiveRoot.Length)
            if ($name -cin 'PES13Recomp/game/files/.keep', 'PES13Recomp/game/xex/.keep', 'PES13Recomp/generated/.keep') { continue }
            if (!(Get-PesRelativePath $name)) { throw "El ZIP tiene una ruta no permitida: $name" }
            $total += $entry.Length
            if ($total -gt 350MB) { throw 'El paquete descomprimido es demasiado grande.' }
            $destination = Get-PesDestination $Staging $name
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination)
            $files.Add($name)
        }
        $metadata = 'Soporte/version.txt'
        $manifest = 'Soporte/archivos-actualizables.txt'
        if (!$files.Contains($metadata) -or !$files.Contains($manifest) -or
            (Get-Content -LiteralPath (Join-Path $Staging $metadata) -Raw).Trim() -cne $Tag) {
            throw 'El ZIP no corresponde a la version seleccionada.'
        }
        return $files
    } finally { $archive.Dispose() }
}

function Write-PesUpdateStatus([string]$Message) {
    [Console]::Out.WriteLine("PES_STATUS: $Message")
}

function Install-PesUpdate([string]$Root, [string]$Transaction, [string]$Staging, [string[]]$Files) {
    $gamePath = Join-Path $Root 'Jugar PES 2013.exe'
    if (!(Test-Path -LiteralPath $gamePath -PathType Leaf)) { throw 'Primero prepara PES 2013 con tu ISO.' }
    foreach ($game in Get-Process -Name 'Jugar PES 2013' -ErrorAction SilentlyContinue) {
        if ($game.Path -eq $gamePath) { throw 'Cerra el juego antes de instalar la actualizacion.' }
    }
    $traces = [Collections.Generic.List[object]]::new()
    $backupGame = Join-Path $Transaction 'Juego anterior.exe'
    Copy-Item -LiteralPath $gamePath -Destination $backupGame
    $buildMarker = Join-Path $Root 'Soporte\version-compilada.txt'
    $markerInfo = Get-Item -LiteralPath $buildMarker -Force -ErrorAction SilentlyContinue
    if ($markerInfo -and ($markerInfo.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'El registro de la version compilada es un acceso enlazado.'
    }
    $previousMarker = if ($markerInfo) { [IO.File]::ReadAllText($buildMarker) }
    $manifest = 'Soporte/archivos-actualizables.txt'
    $version = 'Soporte/version.txt'
    $known = Get-Content -LiteralPath (Get-PesDestination $Root $manifest)
    $nextFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $Files) { $null = $nextFiles.Add($file) }

    $remove = @($known | Where-Object { (Get-PesRelativePath $_) -and !$nextFiles.Contains($_) })
    $paths = @($Files | Where-Object { $_ -notin $manifest, $version }) + $remove + @($manifest, $version)
    $buildChanged = $false
    $buildStarted = $false
    $regenerate = $false
    $publishStarted = $false
    try {
        foreach ($name in $paths) {
            if ($name -in $manifest, $version -and !$publishStarted) {
                if ($buildChanged) {
                    $buildStarted = $true
                    Write-PesUpdateStatus 'Compilando solo los cambios del juego...'
                    $compiler = Get-PesDestination $Root 'Soporte/Compilar PES 2013.ps1'
                    $arguments = @{ Gui = $true; Incremental = $true; Regenerate = $regenerate }
                    $global:LASTEXITCODE = 0
                    & $compiler @arguments
                    if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar la actualizacion. Se conservara el juego anterior.' }
                }
                $publishStarted = $true
            }
            $target = Get-PesDestination $Root $name
            $incoming = Get-PesDestination $Staging $name
            $exists = Test-Path -LiteralPath $target -PathType Leaf
            $install = $nextFiles.Contains($name)
            if ($install -and $exists -and
                (Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $incoming).Hash) { continue }
            if (!$install -and !$exists) { continue }
            $backup = Join-Path (Join-Path $Transaction 'anteriores') $name
            if ($exists) {
                $null = New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force
                [IO.File]::Move($target, $backup)
            }
            $traces.Add([pscustomobject]@{ Target = $target; Backup = $backup; Existed = $exists })
            if ($install) {
                $null = New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
                [IO.File]::Copy($incoming, $target)
            }
            if ($name -match '^PES13Recomp/(CMakeLists\.txt|cmake/|runtime/|config/|thirdparty/|tools/(XenonRecomp/|recomp\.ps1|gen-imports\.ps1))') {
                $buildChanged = $true
                if ($name -match '^PES13Recomp/(config/|tools/(XenonRecomp/|recomp\.ps1|gen-imports\.ps1))') { $regenerate = $true }
            }
        }
        [IO.File]::WriteAllText($buildMarker, (Get-Content -LiteralPath (Get-PesDestination $Root $version) -Raw).Trim(),
            [Text.Encoding]::ASCII)
    } catch {
        $original = $_
        for ($i = $traces.Count - 1; $i -ge 0; --$i) {
            $change = $traces[$i]
            if (Test-Path -LiteralPath $change.Target -PathType Leaf) {
                Remove-Item -LiteralPath $change.Target -Force
            }
            if ($change.Existed) { [IO.File]::Move($change.Backup, $change.Target) }
        }
        if ($buildStarted) { [IO.File]::Copy($backupGame, $gamePath, $true) }
        if ($markerInfo) {
            [IO.File]::WriteAllText($buildMarker, $previousMarker)
        } elseif (Test-Path -LiteralPath $buildMarker -PathType Leaf) {
            Remove-Item -LiteralPath $buildMarker -Force
        }
        throw $original
    }
}

if ($MyInvocation.InvocationName -eq '.') { return }
if ($Gui) {
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    $OutputEncoding = [Console]::OutputEncoding
}

try {
    if ($Check -eq $Apply) { throw 'Elegi buscar o instalar una actualizacion.' }
    $root = (Get-Item -LiteralPath (Split-Path -Parent $PSScriptRoot)).FullName
    $tag = (Get-Content -LiteralPath (Get-PesDestination $root 'Soporte/version.txt') -Raw).Trim()
    $current = Read-PesVersion $tag
    if (!$current) { throw 'No se pudo identificar la version instalada.' }
    $api = "https://api.github.com/repos/$releaseRepo/releases"
    $headers = @{ Accept = 'application/vnd.github+json'; 'User-Agent' = 'PES13Recomp-Launcher' }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    if ($Check) {
        $releases = Invoke-RestMethod -Uri "$($api)?per_page=30" -Headers $headers -TimeoutSec 15 -UseBasicParsing
        $available = $releases | ForEach-Object { Get-PesRelease $_ $current } | Sort-Object Version -Descending | Select-Object -First 1
        if ($available) { [Console]::Out.WriteLine("PES_UPDATE:$($available.Tag)") }
        else { [Console]::Out.WriteLine('PES_UPDATE:CURRENT') }
        exit 0
    }

    if (!(Read-PesVersion $TargetTag)) { throw 'La version solicitada no es valida.' }
    $release = Get-PesRelease (Invoke-RestMethod -Uri "$api/tags/$TargetTag" -Headers $headers -TimeoutSec 15 -UseBasicParsing) $current
    if (!$release -or $release.Tag -cne $TargetTag) { throw 'Esa actualizacion ya no esta disponible.' }
    $lockPath = Join-Path $PSScriptRoot '.preparando.lock'
    $existingLock = Get-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
    if ($existingLock -and ($existingLock.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'La preparacion contiene un acceso enlazado.'
    }
    try {
        $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None, 4096, [IO.FileOptions]::DeleteOnClose)
    } catch [IO.IOException] { throw 'Ya se esta preparando o actualizando el juego en esta carpeta.' }
    try {
        $current = Read-PesVersion (Get-Content -LiteralPath (Get-PesDestination $root 'Soporte/version.txt') -Raw).Trim()
        if (!$current -or $release.Version -le $current) {
            [Console]::Out.WriteLine('PES_UPDATE:CURRENT')
            exit 0
        }
        $updates = Join-Path $PSScriptRoot 'Actualizaciones'
        $updateDir = Get-Item -LiteralPath $updates -Force -ErrorAction SilentlyContinue
        if ($updateDir -and ($updateDir.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'La carpeta de actualizaciones es un acceso enlazado.'
        }
        $transaction = Join-Path $updates ([guid]::NewGuid().ToString('N'))
        $staging = Join-Path $transaction 'paquete'
        $null = New-Item -ItemType Directory -Path $staging -Force
        $zip = Join-Path $transaction $assetName
        Write-PesUpdateStatus 'Descargando la actualizacion desde GitHub...'
        Invoke-WebRequest -Uri $release.Url -OutFile $zip -TimeoutSec 120 -UseBasicParsing
        if ((Get-Item -LiteralPath $zip).Length -ne $release.Length -or
            (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $release.Hash) {
            throw 'La descarga no paso la verificacion. Intenta de nuevo.'
        }
        $files = @(Get-PesUpdateFiles $zip $staging $release.Tag)
        Install-PesUpdate $root $transaction $staging $files
        $absoluteTransaction = [IO.Path]::GetFullPath($transaction)
        $updateRoot = [IO.Path]::GetFullPath($updates).TrimEnd('\') + '\'
        if ($absoluteTransaction.StartsWith($updateRoot, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $absoluteTransaction -Recurse -Force -ErrorAction SilentlyContinue
        }
        [Console]::Out.WriteLine('PES_UPDATE:CURRENT')
    } finally { $lock.Dispose() }
} catch {
    [Console]::Error.WriteLine('PES_ERROR: ' + ($_.Exception.Message -replace '[\x00-\x1f]+', ' ').Trim())
    exit 1
}
