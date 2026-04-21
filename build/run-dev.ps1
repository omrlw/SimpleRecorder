param()

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$solution = Join-Path $repoRoot "SimpleRecorder.sln"
$appProject = Join-Path $repoRoot "src\\SimpleRecorder.App\\SimpleRecorder.App.csproj"
$vsWherePath = Join-Path ${Env:ProgramFiles(x86)} "Microsoft Visual Studio\\Installer\\vswhere.exe"
$msbuildPath = $null
$cppToolchainPath = $null

function Invoke-MSBuildCleanEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(Mandatory = $true)]
        [string]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = $Arguments
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    foreach ($entry in Get-ChildItem Env:) {
        if ($entry.Name -ine "PATH" -and $entry.Name -ine "Path") {
            $startInfo.Environment[$entry.Name] = $entry.Value
        }
    }

    $startInfo.Environment["PATH"] = $env:PATH

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    if ($stdout) {
        Write-Host $stdout
    }

    if ($stderr) {
        Write-Error $stderr
    }

    if ($process.ExitCode -ne 0) {
        throw "MSBuild failed with exit code $($process.ExitCode)."
    }
}

& (Join-Path $PSScriptRoot "restore.ps1")

Get-Process -Name "SimpleRecorder.App" -ErrorAction SilentlyContinue |
    ForEach-Object {
        Write-Host "Stopping running instance $($_.Id) before rebuild..."
        Stop-Process -Id $_.Id -Force
    }

if (Test-Path -LiteralPath $vsWherePath) {
    $installationPath = & $vsWherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($installationPath) {
        $candidate = Join-Path $installationPath "MSBuild\\Current\\Bin\\MSBuild.exe"
        if (Test-Path -LiteralPath $candidate) {
            $msbuildPath = $candidate
        }

        $msvcRoot = Join-Path $installationPath "VC\\Tools\\MSVC"
        if (Test-Path -LiteralPath $msvcRoot) {
            $cppToolchainPath = Get-ChildItem $msvcRoot -Directory |
                Sort-Object Name -Descending |
                ForEach-Object {
                    $clCandidate = Join-Path $_.FullName "bin\\Hostx64\\x64\\cl.exe"
                    if (Test-Path -LiteralPath $clCandidate) {
                        $clCandidate
                    }
                } |
                Select-Object -First 1
        }
    }
}

if ($msbuildPath -and $cppToolchainPath) {
    Invoke-MSBuildCleanEnvironment `
        -Executable $msbuildPath `
        -Arguments "$solution /restore /p:Configuration=Debug /p:Platform=x64" `
        -WorkingDirectory $repoRoot
}
else {
    Write-Warning "MSBuild or the Desktop C++ toolchain is missing. Building the managed app project only; runtime will fall back to the managed stub backend."
    dotnet build $appProject -c Debug -p:Platform=x64 --no-restore
}

function Find-AppExecutable {
    param(
        [string]$ProjectRoot
    )

    return Get-ChildItem (Join-Path $ProjectRoot "bin\\x64\\Debug") -Recurse -Filter "SimpleRecorder.App.exe" |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

$appExe = Find-AppExecutable -ProjectRoot (Join-Path $repoRoot "src\\SimpleRecorder.App")

if (-not (Test-Path $appExe)) {
    throw "SimpleRecorder executable was not found at $appExe"
}

Start-Process -FilePath $appExe | Out-Null
