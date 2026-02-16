param(
    [ValidateSet("start", "stop", "status", "smoke")]
    [string]$Action = "status",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$ApiExeDebug = Join-Path $RootDir "BuildCheck/API/build/Debug/api_server.exe"
$ApiExeRelease = Join-Path $RootDir "BuildCheck/API/build/Release/api_server.exe"
$ApiExeUnix = Join-Path $RootDir "BuildCheck/API/build/api_server"
$EngineDir = Join-Path $RootDir "BuildCheck/Engine"
$PidDir = Join-Path $RootDir ".run"
$PidFile = Join-Path $PidDir "local_stack.json"
$ApiBase = "http://127.0.0.1:8080"
$EngineBase = "http://127.0.0.1:9090"

function Get-PortPids {
    param([int[]]$Ports)
    $pids = @()
    $lines = netstat -ano -p tcp | Select-String "LISTENING"
    foreach ($line in $lines) {
        $parts = ($line.ToString() -replace "\s+", " ").Trim().Split(" ")
        if ($parts.Length -lt 5) { continue }
        $local = $parts[1]
        $procId = $parts[-1]
        if ($local -notmatch ":(\d+)$") { continue }
        $port = [int]$Matches[1]
        if ($Ports -contains $port -and $procId -match "^\d+$") {
            $pids += [int]$procId
        }
    }
    return ($pids | Select-Object -Unique)
}

function Stop-Pids {
    param([int[]]$Pids)
    foreach ($id in ($Pids | Select-Object -Unique)) {
        if ($id -le 0) { continue }
        try {
            Stop-Process -Id $id -Force -ErrorAction Stop
            Write-Host "[local] stopped pid $id"
        } catch {
        }
    }
}

function Wait-Healthy {
    param(
        [string]$Url,
        [int]$TimeoutSec = 30
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $resp = Invoke-RestMethod -Method Get -Uri $Url -TimeoutSec 4
            if ($null -ne $resp) { return $true }
        } catch {
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Ensure-DevEnv {
    $isProd = $false
    if ($env:BUILDCHECK_ENV) {
        $isProd = ($env:BUILDCHECK_ENV.Trim().ToLower() -eq "production")
    }
    if (-not $env:ENGINE_API_KEY -or $env:ENGINE_API_KEY.Length -lt 32) {
        if ($isProd) {
            throw "[local] ENGINE_API_KEY missing/weak in production mode."
        }
        $env:ENGINE_API_KEY = "0123456789abcdef0123456789abcdef"
        Write-Host "[local] ENGINE_API_KEY missing/weak; using local strong dev key."
    }
    if (-not $env:BUILDCHECK_ADMIN_USERNAME) {
        if ($isProd) {
            throw "[local] BUILDCHECK_ADMIN_USERNAME missing in production mode."
        }
        $env:BUILDCHECK_ADMIN_USERNAME = "admin"
    }
    if (-not $env:BUILDCHECK_ADMIN_PASSWORD) {
        if ($isProd) {
            throw "[local] BUILDCHECK_ADMIN_PASSWORD missing in production mode."
        }
        $env:BUILDCHECK_ADMIN_PASSWORD = "admin123!"
    }
    if (-not $env:BUILDCHECK_ADMIN_ALLOWED_ORIGINS) {
        $env:BUILDCHECK_ADMIN_ALLOWED_ORIGINS = "http://127.0.0.1:8081,http://localhost:8081"
    }
}

function Resolve-ApiExecutable {
    if (Test-Path $ApiExeDebug) { return $ApiExeDebug }
    if (Test-Path $ApiExeRelease) { return $ApiExeRelease }
    if (Test-Path $ApiExeUnix) { return $ApiExeUnix }
    return $null
}

function Read-PidState {
    if (-not (Test-Path $PidFile)) { return $null }
    try {
        return (Get-Content $PidFile -Raw | ConvertFrom-Json)
    } catch {
        return $null
    }
}

function Write-PidState {
    param($EnginePid, $ApiPid)
    if (-not (Test-Path $PidDir)) {
        New-Item -ItemType Directory -Path $PidDir | Out-Null
    }
    @{
        engine_pid = $EnginePid
        api_pid = $ApiPid
        started_at_utc = [DateTime]::UtcNow.ToString("o")
    } | ConvertTo-Json | Set-Content -Path $PidFile
}

function Get-Json {
    param([string]$Url)
    return Invoke-RestMethod -Method Get -Uri $Url -TimeoutSec 4
}

function Test-Health {
    param([string]$Url)
    try {
        $null = Get-Json -Url $Url
        return $true
    } catch {
        return $false
    }
}

function Show-Status {
    $state = Read-PidState
    $portPids = Get-PortPids -Ports @(8080, 9090)
    if ($state -and $portPids.Count -eq 0) {
        # Stale pid file from a previous crashed/stopped session.
        try { Remove-Item $PidFile -Force -ErrorAction SilentlyContinue } catch {}
        $state = $null
    }
    Write-Host "[local] listeners pids (8080/9090): $($portPids -join ', ')"
    if ($state) {
        Write-Host "[local] pid file: engine=$($state.engine_pid), api=$($state.api_pid), started=$($state.started_at_utc)"
    } else {
        Write-Host "[local] pid file not found"
    }

    try {
        $apiHealth = Get-Json -Url "$ApiBase/health"
        Write-Host "[local] api health: $($apiHealth | ConvertTo-Json -Compress)"
    } catch {
        Write-Host "[local] api health: down"
    }

    try {
        $engineHealth = Get-Json -Url "$EngineBase/engine/health"
        Write-Host "[local] engine health: $($engineHealth | ConvertTo-Json -Compress)"
    } catch {
        Write-Host "[local] engine health: down"
    }
}

switch ($Action) {
    "start" {
        $apiExe = Resolve-ApiExecutable
        if (-not $apiExe) {
            throw "[local] API binary not found. Build API first (Debug or Release)."
        }

        python -c "import fastapi,uvicorn,ultralytics,requests" 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "[local] missing python deps. run: python -m pip install -r BuildCheck/BuildCheck/Engine/requirements.txt"
        }

        Ensure-DevEnv
        $existing = Get-PortPids -Ports @(8080, 9090)
        if ($existing.Count -gt 0) {
            if (-not $Force) {
                throw "[local] ports 8080/9090 are already in use ($($existing -join ', ')). run stop first or use -Force."
            }
            Stop-Pids -Pids $existing
            Start-Sleep -Seconds 1
        }

        $engineProc = $null
        $apiProc = $null
        try {
            $engineProc = Start-Process -FilePath python -ArgumentList "-m", "uvicorn", "engine_service:app", "--host", "0.0.0.0", "--port", "9090" -WorkingDirectory $EngineDir -PassThru
            $apiProc = Start-Process -FilePath $apiExe -PassThru
            Write-PidState -EnginePid $engineProc.Id -ApiPid $apiProc.Id

            if (-not (Wait-Healthy -Url "$EngineBase/engine/health" -TimeoutSec 35)) {
                throw "[local] engine did not become healthy on :9090"
            }
            if (-not (Wait-Healthy -Url "$ApiBase/health" -TimeoutSec 35)) {
                throw "[local] api did not become healthy on :8080"
            }

            $engineHealth = Get-Json -Url "$EngineBase/engine/health"
            if ($engineHealth.PSObject.Properties.Name -notcontains "model_loaded") {
                throw "[local] wrong engine runtime detected on :9090 (expected python engine_service.py with model_loaded field)."
            }

            Write-Host "[local] started successfully"
            Show-Status
        } catch {
            if ($apiProc) { Stop-Pids -Pids @([int]$apiProc.Id) }
            if ($engineProc) { Stop-Pids -Pids @([int]$engineProc.Id) }
            if (Test-Path $PidFile) { Remove-Item $PidFile -Force -ErrorAction SilentlyContinue }
            throw
        }
    }
    "stop" {
        $state = Read-PidState
        if ($state) {
            Stop-Pids -Pids @([int]$state.engine_pid, [int]$state.api_pid)
        }
        if ($Force) {
            Stop-Pids -Pids (Get-PortPids -Ports @(8080, 9090))
        }
        if (Test-Path $PidFile) {
            Remove-Item $PidFile -Force -ErrorAction SilentlyContinue
        }
        if (-not $Force) {
            $leftovers = Get-PortPids -Ports @(8080, 9090)
            if ($leftovers.Count -gt 0) {
                throw "[local] stop completed but listeners still exist ($($leftovers -join ', ')). run stop -Force."
            }
        }
        Write-Host "[local] stopped"
        Show-Status
    }
    "status" {
        Show-Status
    }
    "smoke" {
        $env:RUN_LIVE_E2E = "1"
        if (-not (Test-Health -Url "$ApiBase/health") -or -not (Test-Health -Url "$EngineBase/engine/health")) {
            throw "[local] services are down. run: powershell -ExecutionPolicy Bypass -File scripts/local_stack.ps1 -Action start"
        }
        python -m pytest -q tests/integration/test_live_e2e.py
        if ($LASTEXITCODE -ne 0) { throw "[local] live_e2e smoke failed" }
        python -m pytest -q tests/integration/test_contact_admin_integration.py
        if ($LASTEXITCODE -ne 0) { throw "[local] contact_admin integration failed" }
    }
}
