param()

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$solution = Join-Path $repoRoot "SimpleRecorder.sln"
$managedProjects = Get-ChildItem (Join-Path $repoRoot "src") -Recurse -Filter *.csproj | Sort-Object FullName

if (-not $managedProjects) {
    throw "No managed projects were found under $repoRoot\\src."
}

Write-Host "Restoring solution dependencies from $solution..." -ForegroundColor Cyan
dotnet restore $solution

foreach ($project in $managedProjects) {
    Write-Host "Restoring $($project.Name)..." -ForegroundColor Cyan
    dotnet restore $project.FullName
}

$vsWherePath = Join-Path ${Env:ProgramFiles(x86)} "Microsoft Visual Studio\\Installer\\vswhere.exe"
if (Test-Path -LiteralPath $vsWherePath) {
    $installationPath = & $vsWherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($installationPath) {
        $msbuildPath = Join-Path $installationPath "MSBuild\\Current\\Bin\\MSBuild.exe"
        $clPath = $null

        $msvcRoot = Join-Path $installationPath "VC\\Tools\\MSVC"
        if (Test-Path -LiteralPath $msvcRoot) {
            $clPath = Get-ChildItem $msvcRoot -Directory |
                Sort-Object Name -Descending |
                ForEach-Object {
                    $candidate = Join-Path $_.FullName "bin\\Hostx64\\x64\\cl.exe"
                    if (Test-Path -LiteralPath $candidate) {
                        $candidate
                    }
                } |
                Select-Object -First 1
        }

        if ((Test-Path -LiteralPath $msbuildPath) -and $clPath) {
            Write-Host "MSBuild detected at $msbuildPath" -ForegroundColor Green
            Write-Host "Desktop C++ toolchain detected at $clPath" -ForegroundColor Green
            Write-Host "Use MSBuild to build the full solution, including the native DLL project." -ForegroundColor Green
            return
        }
    }
}

Write-Warning "Managed restore from the repo root succeeded, but the full solution still needs Visual Studio MSBuild plus the Desktop C++ toolchain before the native DLL project can be built."
