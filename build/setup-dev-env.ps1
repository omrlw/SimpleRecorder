param()

$ErrorActionPreference = "Stop"

function Write-Check {
    param(
        [string]$Label,
        [bool]$Passed,
        [string]$Detail
    )

    $color = if ($Passed) { "Green" } else { "Yellow" }
    $status = if ($Passed) { "[ok]" } else { "[missing]" }
    Write-Host "$status $Label - $Detail" -ForegroundColor $color
}

function Find-MSBuild {
    $vsWherePath = Join-Path ${Env:ProgramFiles(x86)} "Microsoft Visual Studio\\Installer\\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vsWherePath)) {
        return $null
    }

    $installationPath = & $vsWherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $installationPath) {
        return $null
    }

    $msbuildPath = Join-Path $installationPath "MSBuild\\Current\\Bin\\MSBuild.exe"
    if (Test-Path -LiteralPath $msbuildPath) {
        return $msbuildPath
    }

    return $null
}

function Find-CppToolchain {
    $vsWherePath = Join-Path ${Env:ProgramFiles(x86)} "Microsoft Visual Studio\\Installer\\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vsWherePath)) {
        return $null
    }

    $installationPath = & $vsWherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $installationPath) {
        return $null
    }

    $msvcRoot = Join-Path $installationPath "VC\\Tools\\MSVC"
    if (-not (Test-Path -LiteralPath $msvcRoot)) {
        return $null
    }

    return Get-ChildItem $msvcRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object {
            $candidate = Join-Path $_.FullName "bin\\Hostx64\\x64\\cl.exe"
            if (Test-Path -LiteralPath $candidate) {
                $candidate
            }
        } |
        Select-Object -First 1
}

$dotnetVersion = $null
try {
    $dotnetVersion = (dotnet --version).Trim()
}
catch {
    $dotnetVersion = $null
}

$msbuildPath = Find-MSBuild
$cppToolchainPath = Find-CppToolchain
$windowsSdkRoot = $null
try {
    $windowsSdkRoot = (Get-ItemProperty -Path "HKLM:\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots" -ErrorAction Stop).KitsRoot10
}
catch {
    $windowsSdkRoot = $null
}

Write-Host "SimpleRecorder development environment check" -ForegroundColor Cyan
Write-Host ""

Write-Check ".NET SDK" ($null -ne $dotnetVersion) ($(if ($dotnetVersion) { "Detected $dotnetVersion" } else { "Install .NET 8 SDK." }))
Write-Check "MSBuild + Visual Studio" ($null -ne $msbuildPath) ($(if ($msbuildPath) { $msbuildPath } else { "Install Visual Studio with WinUI support." }))
Write-Check "Desktop C++ toolchain" ($null -ne $cppToolchainPath) ($(if ($cppToolchainPath) { $cppToolchainPath } else { "Install the Desktop development with C++ workload." }))
Write-Check "Windows SDK" ($null -ne $windowsSdkRoot) ($(if ($windowsSdkRoot) { $windowsSdkRoot } else { "Install the Windows 10/11 SDK." }))

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "1. Run .\\build\\restore.ps1"
Write-Host "2. Build the solution in Debug|x64"
Write-Host "3. Launch SimpleRecorder.App if the full Windows toolchain is available"
