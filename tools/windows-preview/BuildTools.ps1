function Write-PesBuildStatus([string]$Message, [switch]$Gui) {
    if ($Gui) {
        [Console]::Out.WriteLine("PES_STATUS: $Message")
    } else {
        Write-Host $Message
    }
}

function Test-PesBuildPath([string]$Paths, [string]$FileName) {
    foreach ($directory in ($Paths -split ';')) {
        if ([string]::IsNullOrWhiteSpace($directory)) { continue }
        try {
            $path = [IO.Path]::Combine($directory.Trim().Trim('"'), $FileName)
            if (Test-Path -LiteralPath $path -PathType Leaf -ErrorAction SilentlyContinue) { return $true }
        } catch { continue }
    }
    return $false
}

function Get-PesBuildToolProblems {
    foreach ($tool in 'clang-cl.exe', 'lld-link.exe', 'ninja.exe') {
        if (!(Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) { $tool }
    }
    $cmake = Get-Command 'cmake.exe' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $cmakeVersion = $null
    if ($cmake) {
        try {
            $versionLines = @(& $cmake.Source --version)
            if ($LASTEXITCODE -eq 0 -and $versionLines.Count -and
                $versionLines[0] -match '^cmake version ([0-9]+\.[0-9]+(?:\.[0-9]+)?)') {
                $cmakeVersion = [version]$Matches[1]
            }
        } catch {}
    }
    if (!$cmakeVersion -or $cmakeVersion -lt [version]'3.20') { 'CMake 3.20 o posterior' }
    if (!(Test-PesBuildPath $env:INCLUDE 'windows.h') -or !(Test-PesBuildPath $env:LIB 'kernel32.lib')) {
        'Windows SDK x64'
    }
    if (!(Test-PesBuildPath $env:INCLUDE 'cstdio') -or !(Test-PesBuildPath $env:LIB 'libcmt.lib')) {
        'MSVC x64'
    }
}

function Test-PesBuildTools {
    return @(Get-PesBuildToolProblems).Count -eq 0
}

function Find-PesBuildTools {
    $programFiles = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (!$programFiles) { return }
    $vswhere = Join-Path $programFiles 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) { return }
    try {
        $info = New-Object Diagnostics.ProcessStartInfo
        $info.FileName = $vswhere
        $info.Arguments = '-products * -format json -utf8'
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $info.RedirectStandardOutput = $true
        $info.StandardOutputEncoding = [Text.Encoding]::UTF8
        $process = [Diagnostics.Process]::Start($info)
        try {
            $json = $process.StandardOutput.ReadToEnd()
            $process.WaitForExit()
            if ($process.ExitCode -ne 0 -or !$json.TrimStart().StartsWith('[')) { return }
        } finally { $process.Dispose() }
        $instances = ConvertFrom-Json -InputObject $json -ErrorAction Stop
    } catch { return }

    $valid = foreach ($instance in $instances) {
        if ($instance -isnot [PSCustomObject]) { continue }
        $path = $instance.installationPath
        $version = $null
        if ($path -isnot [string] -or $path -notmatch '^[A-Za-z]:[\\/]' -or
            ![version]::TryParse([string]$instance.installationVersion, [ref]$version) -or
            !(Test-Path -LiteralPath $path -PathType Container -ErrorAction SilentlyContinue)) { continue }
        [PSCustomObject]@{
            installationPath = $path
            installationVersion = $version
            productId = $instance.productId
            channelId = $instance.channelId
        }
    }
    $valid | Sort-Object installationVersion -Descending
}

function Use-PesBuildTools([ref]$Problems) {
    $best = @(Get-PesBuildToolProblems)
    if ($best.Count -eq 0) { return $true }
    foreach ($instance in Find-PesBuildTools) {
        $installation = $instance.installationPath
        $devCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
        if (!(Test-Path -LiteralPath $devCmd -PathType Leaf)) { continue }

        $info = New-Object Diagnostics.ProcessStartInfo
        $info.FileName = $env:ComSpec
        $info.Arguments = '/d /s /u /c ""' + $devCmd + '" -arch=x64 -host_arch=x64 >nul && set"'
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $info.RedirectStandardOutput = $true
        $info.StandardOutputEncoding = [Text.Encoding]::Unicode
        try {
            $process = [Diagnostics.Process]::Start($info)
            try {
                $output = $process.StandardOutput.ReadToEnd()
                $process.WaitForExit()
                if ($process.ExitCode -ne 0) { continue }
            } finally { $process.Dispose() }
        } catch { continue }

        $previous = @{}
        foreach ($line in $output -split '\r?\n') {
            if ($line -match '^([^=]+)=(.*)$') {
                $name = $Matches[1]
                if (!$previous.ContainsKey($name)) {
                    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
                }
                [Environment]::SetEnvironmentVariable($name, $Matches[2], 'Process')
            }
        }
        $bundled = @(
            'VC\Tools\Llvm\x64\bin',
            'VC\Tools\Llvm\bin',
            'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin',
            'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
        ) | ForEach-Object { Join-Path $installation $_ } | Where-Object {
            Test-Path -LiteralPath $_ -PathType Container
        }
        if (!$previous.ContainsKey('Path')) { $previous['Path'] = $env:Path }
        $env:Path = (@($bundled) + $env:Path) -join ';'
        $missing = @(Get-PesBuildToolProblems)
        if ($missing.Count -eq 0) { return $true }
        if ($missing.Count -lt $best.Count) { $best = $missing }
        foreach ($name in $previous.Keys) {
            [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
        }
    }
    if ($Problems) { $Problems.Value = $best }
    return $false
}

function Install-PesBuildTools([switch]$Gui) {
    $url = 'https://aka.ms/vs/17/release/vs_BuildTools.exe'
    $setup = Join-Path ([IO.Path]::GetTempPath()) ("pes13-buildtools-{0}.exe" -f [guid]::NewGuid())
    try {
        Write-PesBuildStatus 'Descargando el instalador oficial de Microsoft...' -Gui:$Gui
        try {
            Invoke-WebRequest -Uri $url -OutFile $setup -UseBasicParsing -TimeoutSec 90
        } catch {
            throw "No se pudo descargar Microsoft Build Tools. Revisa tu conexion a Internet o abrilo desde $url"
        }
        $signature = Get-AuthenticodeSignature -LiteralPath $setup
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)O=Microsoft Corporation(,|$)') {
            throw 'Windows no pudo verificar la firma de Microsoft del instalador. Volve a probar con el enlace oficial de Build Tools.'
        }
        $argsForSetup = @('--passive', '--wait', '--norestart', '--nocache')
        $existing = Find-PesBuildTools | Where-Object {
            $_.productId -eq 'Microsoft.VisualStudio.Product.BuildTools' -and $_.channelId -eq 'VisualStudio.17.Release'
        } | Select-Object -First 1
        if ($existing) {
            $argsForSetup = @('modify', '--channelId', $existing.channelId,
                '--installPath', ('"' + $existing.installationPath + '"')) + $argsForSetup
        }
        foreach ($component in 'Microsoft.VisualStudio.Workload.VCTools',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
            'Microsoft.VisualStudio.Component.VC.Llvm.Clang',
            'Microsoft.VisualStudio.Component.VC.CMake.Project',
            'Microsoft.VisualStudio.Component.Windows11SDK.26100') {
            $argsForSetup += @('--add', $component)
        }
        Write-PesBuildStatus 'Instalando herramientas de Microsoft. Si Windows pide permiso, toca Si.' -Gui:$Gui
        try {
            $setupProcess = Start-Process -FilePath $setup -ArgumentList $argsForSetup -Verb RunAs -Wait -PassThru
        } catch {
            throw 'No se pudo abrir el instalador de Microsoft. Si cancelaste el permiso de Windows, volve a intentar.'
        }
        switch ($setupProcess.ExitCode) {
            0 { return }
            3010 { throw 'Las herramientas se instalaron. Reinicia Windows y volve a abrir PES 2013.' }
            1641 { throw 'Microsoft inicio un reinicio. Despues, volve a abrir PES 2013.' }
            1602 { throw 'Se cancelo la instalacion de herramientas. Volve a abrir PES 2013 para reintentar.' }
            default { throw "El instalador de Microsoft devolvio el codigo $($setupProcess.ExitCode). Abri Visual Studio Installer y revisa el detalle." }
        }
    } finally {
        if (Test-Path -LiteralPath $setup -PathType Leaf) {
            Remove-Item -LiteralPath $setup -ErrorAction SilentlyContinue
        }
    }
}

function Initialize-PesBuildTools([switch]$Gui) {
    Write-PesBuildStatus 'Buscando las herramientas de compilacion...' -Gui:$Gui
    if (Use-PesBuildTools) { return }
    if (!$Gui) {
        Write-Host 'Faltan herramientas de C++ para generar el juego desde tu ISO.'
        Write-Host 'El asistente puede agregar Clang, CMake/Ninja y el SDK con Microsoft Build Tools.'
        $answer = Read-Host 'Queres instalarlas ahora? [S/N]'
        if ($answer.Trim() -notmatch '^(s|si|s\u00ed)$') {
            throw 'Instalacion omitida. Volve a abrir PES 2013 para continuar.'
        }
    }
    Install-PesBuildTools -Gui:$Gui
    Write-PesBuildStatus 'Comprobando las herramientas instaladas...' -Gui:$Gui
    $problems = @()
    if (!(Use-PesBuildTools -Problems ([ref]$problems))) {
        throw ("Faltan componentes de Microsoft Build Tools: {0}. Revisalos en Visual Studio Installer y reintenta." -f ($problems -join ', '))
    }
}
