$ErrorActionPreference = 'Stop'

$RootDir = Split-Path -Parent $PSScriptRoot
$GoDir = Join-Path $RootDir 'GO'
$CxxDir = Join-Path $RootDir 'CXX'
$CxxBuildDir = if ($env:CXX_BUILD_DIR) { $env:CXX_BUILD_DIR } else { Join-Path $RootDir 'CXX\build' }
$ConfigPath = Join-Path $RootDir 'docs\configs\serve-web-ollama-3b.json'

$Model = if ($env:HARU_MODEL) { $env:HARU_MODEL } else { 'qwen2.5-coder:3b' }
$DbPath = if ($env:HARU_DB_PATH) { $env:HARU_DB_PATH } else { Join-Path $env:TEMP 'haruhidb-web.db' }
$Listen = if ($env:HARU_LISTEN) { $env:HARU_LISTEN } else { ':8080' }
$Timeout = if ($env:HARU_TIMEOUT) { $env:HARU_TIMEOUT } else { '60s' }
$OpenBrowser = if ($env:HARU_OPEN_BROWSER) { $env:HARU_OPEN_BROWSER } else { 'true' }

function Require-Command([string]$Name) {
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    throw "$Name is required but not found in PATH"
  }
}

function Is-Enabled([string]$Value) {
  $v = $Value.Trim().ToLowerInvariant()
  return -not ($v -eq '0' -or $v -eq 'false' -or $v -eq 'no' -or $v -eq 'off')
}

function Resolve-UiUrl([string]$ListenValue) {
  if ($env:HARU_UI_URL) {
    return $env:HARU_UI_URL
  }
  if ($ListenValue -match '^:(\d+)$') {
    return "http://127.0.0.1:$($Matches[1])/ui"
  }
  return 'http://127.0.0.1:8080/ui'
}

function Ensure-OllamaService {
  try {
    Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:11434/api/tags' -TimeoutSec 2 | Out-Null
    Write-Host '[1/5] Ollama service is running'
  }
  catch {
    Write-Host '[1/5] Starting Ollama service in background'
    Start-Process -FilePath 'ollama' -ArgumentList 'serve' -WindowStyle Hidden | Out-Null
    Start-Sleep -Seconds 2
  }
}

function Ensure-CapiLibrary {
  $capiOutDir = Join-Path $RootDir 'CXX\build\src\capi'
  $dllPath = Join-Path $capiOutDir 'libharuhidb_capi.dll'
  $importLibPath = Join-Path $capiOutDir 'libharuhidb_capi.dll.a'

  if ((Test-Path $dllPath) -and (Test-Path $importLibPath)) {
    Write-Host '[3/5] C API library already exists'
    return
  }

  Require-Command 'cmake'
  Require-Command 'mingw32-make'

  $mingwBuildDir = Join-Path $RootDir 'CXX\build-mingw'
  Write-Host '[3/5] Building C API library (MinGW)'
  & cmake -S $CxxDir -B $mingwBuildDir -G 'MinGW Makefiles' -DTEST=OFF -DEXAMPLE=OFF
  & cmake --build $mingwBuildDir --target haruhidb_capi -j 6

  $builtDll = Join-Path $mingwBuildDir 'src\capi\libharuhidb_capi.dll'
  $builtImport = Join-Path $mingwBuildDir 'src\capi\libharuhidb_capi.dll.a'

  if (-not (Test-Path $builtDll) -or -not (Test-Path $builtImport)) {
    throw 'C API build finished but expected output files are missing'
  }

  New-Item -ItemType Directory -Force -Path $capiOutDir | Out-Null
  Copy-Item -Force $builtDll $dllPath
  Copy-Item -Force $builtImport $importLibPath
}

Require-Command 'go'
Require-Command 'ollama'
Require-Command 'cmake'

$uiUrl = Resolve-UiUrl -ListenValue $Listen

Ensure-OllamaService

Write-Host "[2/5] Pull model: $Model"
& ollama pull $Model

Ensure-CapiLibrary

Write-Host '[4/5] Prepare runtime library path'
$capiRuntimeDir = Join-Path $RootDir 'CXX\build\src\capi'
$env:PATH = "$capiRuntimeDir;$env:PATH"

Write-Host '[5/5] Start HaruhiDB Web'
Write-Host "        UI: $uiUrl"
Write-Host "        db_path=$DbPath"
Write-Host "        listen=$Listen"
Write-Host "        timeout=$Timeout"
Write-Host "        model=$Model"

if (Is-Enabled $OpenBrowser) {
  Start-Process $uiUrl | Out-Null
}

Set-Location $GoDir
& go run ./cmd/haruhidb serve --config $ConfigPath --model $Model --db-path $DbPath --listen $Listen --timeout $Timeout
