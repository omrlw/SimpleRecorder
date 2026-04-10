param()

$ErrorActionPreference = "Stop"

Write-Host "SimpleRecorder prerequisites:" -ForegroundColor Cyan
Write-Host "1. Install Visual Studio with Windows App SDK + Desktop C++ workloads."
Write-Host "2. Install Windows 11 SDK."
Write-Host "3. Ensure dotnet can target net8.0-windows."
Write-Host "4. Then run .\build\restore.ps1 and build SimpleRecorder.sln in Debug x64."
