param()

$ErrorActionPreference = "Stop"

$solution = Join-Path $PSScriptRoot "..\SimpleRecorder.sln"
$appExe = Join-Path $PSScriptRoot "..\src\SimpleRecorder.App\bin\x64\Debug\net8.0-windows10.0.19041.0\SimpleRecorder.App.exe"

dotnet restore $solution
dotnet build $solution -c Debug -p:Platform=x64 -m:1 --no-restore

if (-not (Test-Path $appExe)) {
    throw "SimpleRecorder executable was not found at $appExe"
}

Start-Process -FilePath $appExe | Out-Null
