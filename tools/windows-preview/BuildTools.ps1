function Test-PesBuildTools {
    if (!$env:INCLUDE -or !$env:LIB) { return $false }
    foreach ($tool in 'clang-cl.exe', 'lld-link.exe', 'cmake.exe', 'ninja.exe') {
        if (!(Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) { return $false }
    }
    return $true
}

function Find-PesBuildTools {
    $programFiles = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (!$programFiles) { return }
    $vswhere = Join-Path $programFiles 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) { return }
    $json = & $vswhere -products '*' -format json -utf8 | Out-String
    if ($LASTEXITCODE -eq 0 -and $json.Trim()) {
        ConvertFrom-Json $json | Sort-Object { [version]$_.installationVersion } -Descending
    }
}

function Use-PesBuildTools {
    if (Test-PesBuildTools) { return $true }
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
        $process = [Diagnostics.Process]::Start($info)
        try {
            $output = $process.StandardOutput.ReadToEnd()
            $process.WaitForExit()
            if ($process.ExitCode -ne 0) { continue }
        } finally {
            $process.Dispose()
        }

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
        if (Test-PesBuildTools) { return $true }
        foreach ($name in $previous.Keys) {
            [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
        }
    }
    return $false
}

function Install-PesBuildTools {
    $url = 'https://aka.ms/vs/17/release/vs_BuildTools.exe'
    $setup = Join-Path ([IO.Path]::GetTempPath()) ("pes13-buildtools-{0}.exe" -f [guid]::NewGuid())
    try {
        Write-Host 'Descargando el instalador oficial de Microsoft...'
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
        Write-Host 'Si Windows pide permiso, toca Si. Espera a que Microsoft termine la instalacion.'
        try {
            $setupProcess = Start-Process -FilePath $setup -ArgumentList $argsForSetup -Verb RunAs -Wait -PassThru
        } catch {
            throw "No se pudo abrir el instalador de Microsoft. Si cancelaste el permiso de Windows, volve a intentar. $($_.Exception.Message)"
        }
        switch ($setupProcess.ExitCode) {
            0 { return }
            3010 { throw 'Las herramientas se instalaron. Reinicia Windows y volve a abrir Preparar y compilar.cmd.' }
            1641 { throw 'Microsoft inicio un reinicio. Despues, volve a abrir Preparar y compilar.cmd.' }
            1602 { throw 'Se cancelo la instalacion de herramientas. Volve a abrir Preparar y compilar.cmd para reintentar.' }
            default { throw "El instalador de Microsoft devolvio el codigo $($setupProcess.ExitCode). Abri Visual Studio Installer y revisa el detalle." }
        }
    } finally {
        if (Test-Path -LiteralPath $setup -PathType Leaf) {
            Remove-Item -LiteralPath $setup -ErrorAction SilentlyContinue
        }
    }
}

function Initialize-PesBuildTools {
    if (Use-PesBuildTools) { return }
    Write-Host 'Faltan herramientas de C++ para generar el juego desde tu ISO.'
    Write-Host 'El asistente puede agregar Clang, CMake/Ninja y el SDK con Microsoft Build Tools.'
    $answer = Read-Host 'Queres instalarlas ahora? [S/N]'
    if ($answer.Trim() -notmatch '^(s|si|s\u00ed)$') {
        throw 'Instalacion omitida. Volve a abrir Preparar y compilar.cmd y elegi S para continuar.'
    }
    Install-PesBuildTools
    if (!(Use-PesBuildTools)) {
        throw 'No se encontraron todas las herramientas. En Visual Studio Installer revisa Clang, CMake/Ninja, MSVC x64 y Windows SDK. Luego volve a intentar.'
    }
}
