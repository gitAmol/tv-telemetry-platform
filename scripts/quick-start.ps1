# TV Telemetry Analytics - Quick Start Script
# Run from project root: .\scripts\quick-start.ps1

param(
    [int]$TvCount = 1,
    [switch]$Stop
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

if ($Stop) {
    Write-Host "Stopping all containers..." -ForegroundColor Yellow
    
    # Stop TV simulators
    1..$TvCount | ForEach-Object {
        docker stop "tv-sim-$_" 2>$null
        docker rm "tv-sim-$_" 2>$null
    }
    docker stop tv-agent 2>$null
    docker rm tv-agent 2>$null
    
    # Stop infrastructure
    Push-Location "$ProjectRoot\infra"
    docker-compose down
    Pop-Location
    
    Write-Host "All containers stopped." -ForegroundColor Green
    exit 0
}

Write-Host "=== TV Telemetry Analytics Quick Start ===" -ForegroundColor Cyan

# Step 1: Start infrastructure
Write-Host "`n[1/5] Starting infrastructure stack..." -ForegroundColor Yellow
Push-Location "$ProjectRoot\infra"
docker-compose up -d
Pop-Location

# Step 2: Wait for Kafka
Write-Host "[2/5] Waiting for Kafka to be ready..." -ForegroundColor Yellow
Start-Sleep -Seconds 10

# Step 3: Create checkpoint directory
Write-Host "[3/5] Setting up Flink checkpoints..." -ForegroundColor Yellow
docker exec flink-jm bash -c "mkdir -p /tmp/checkpoints && chmod 777 /tmp/checkpoints"

# Step 4: Submit Flink job
Write-Host "[4/5] Building and submitting Flink job..." -ForegroundColor Yellow
Push-Location "$ProjectRoot\flink-job"
mvn clean package -DskipTests -q
$jarUpload = curl.exe -s -X POST http://localhost:8081/jars/upload -H "Expect:" -F "jarfile=@target/bt-analytics-1.0.0.jar" | ConvertFrom-Json
$jarId = ($jarUpload.filename -split "/")[-1]
curl.exe -s -X POST "http://localhost:8081/jars/$jarId/run" | Out-Null
Pop-Location

# Step 5: Start TV agents
Write-Host "[5/5] Starting $TvCount TV agent(s)..." -ForegroundColor Yellow
if ($TvCount -eq 1) {
    docker run -d --name tv-agent --network infra_telemetry `
        -v "tv-agent-data:/data" `
        tv-bt-agent:latest TV-00001 kafka:29092 /data/buffer.db
} else {
    1..$TvCount | ForEach-Object {
        docker run -d --name "tv-sim-$_" --network infra_telemetry `
            -v "tv-sim-$($_)-data:/data" `
            tv-bt-agent:latest "TV-SIM-$('{0:D3}' -f $_)" kafka:29092 /data/buffer.db
    }
}

Write-Host "`n=== Stack is running! ===" -ForegroundColor Green
Write-Host "Flink UI:    http://localhost:8081"
Write-Host "Grafana:     http://localhost:3000 (admin/admin)"
Write-Host "`nVerify data flow:"
Write-Host "  docker exec kafka kafka-console-consumer --bootstrap-server localhost:29092 --topic bt-events --max-messages 5"
Write-Host "`nCheck TimescaleDB (wait 5+ min for first window):"
Write-Host "  docker exec timescaledb psql -U telemetry -d telemetry -c 'SELECT * FROM bt_window_stats;'"
Write-Host "`nTo stop: .\scripts\quick-start.ps1 -Stop -TvCount $TvCount"
