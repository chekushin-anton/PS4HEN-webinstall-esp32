# ============================================================
#  Update data/ from ps4freeforall main.zip
#  Deletes old data, downloads archive, extracts into data/
#  ASCII-only for PowerShell 5.1 compatibility
# ============================================================

$ErrorActionPreference = "Stop"

# Force TLS 1.2 for older Windows 10
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# ---------- CONFIG ----------
$DataPath = "C:\Users\user\Documents\GitHub\PS4HEN-webinstall-esp32\PS4HEN-webinstall-esp32\data"
$ZipUrl   = "https://github.com/Akela-x/ps4freeforall/archive/refs/heads/main.zip"
$TempZip  = Join-Path $env:TEMP "ps4freeforall.zip"
$TempDir  = Join-Path $env:TEMP "ps4freeforall_extract"
$MaxSize  = 0xBE0000   # 12451840 bytes = SPIFFS partition

Write-Host ""
Write-Host "=== Update data folder ===" -ForegroundColor Cyan
Write-Host "Target: $DataPath"
Write-Host "Source: $ZipUrl"
Write-Host ""

# ---------- CONFIRM ----------
Write-Host "This will DELETE the current data folder and replace it." -ForegroundColor Yellow
Write-Host "Any local changes inside data/ will be lost." -ForegroundColor Yellow
$confirm = Read-Host "Continue? (y/N)"
if ($confirm -notmatch "^[yY]$") {
    Write-Host "Cancelled."
    exit 0
}

# ---------- 1. DOWNLOAD ----------
Write-Host ""
Write-Host "[1/4] Downloading archive..." -ForegroundColor Cyan
if (Test-Path $TempZip) { Remove-Item $TempZip -Force }

try {
    Invoke-WebRequest -Uri $ZipUrl -OutFile $TempZip -UseBasicParsing
    $sizeMB = [math]::Round((Get-Item $TempZip).Length / 1MB, 2)
    Write-Host "      Downloaded: $sizeMB MB"
} catch {
    Write-Host "[ERROR] Download failed:" -ForegroundColor Red
    Write-Host "        $_" -ForegroundColor Red
    Write-Host "        Check your internet connection and the URL:" -ForegroundColor Red
    Write-Host "        $ZipUrl" -ForegroundColor Red
    exit 1
}

# ---------- 2. EXTRACT ----------
Write-Host "[2/4] Extracting to temp..." -ForegroundColor Cyan
if (Test-Path $TempDir) { Remove-Item $TempDir -Recurse -Force }

try {
    Expand-Archive -Path $TempZip -DestinationPath $TempDir -Force
} catch {
    Write-Host "[ERROR] Extraction failed:" -ForegroundColor Red
    Write-Host "        $_" -ForegroundColor Red
    exit 1
}

# Determine source folder
$topItems = Get-ChildItem $TempDir -Force
if ($topItems.Count -eq 1 -and $topItems[0].PSIsContainer) {
    $source = $topItems[0].FullName
    Write-Host "      Archive root: $($topItems[0].Name)"
} else {
    $source = $TempDir
    Write-Host "      Archive has multiple top-level items, using all"
}

# ---------- 3. DELETE OLD DATA ----------
Write-Host "[3/4] Removing old data folder..." -ForegroundColor Cyan
if (Test-Path $DataPath) {
    try {
        Remove-Item $DataPath -Recurse -Force
        Write-Host "      Deleted: $DataPath"
    } catch {
        Write-Host "[ERROR] Cannot delete old data folder:" -ForegroundColor Red
        Write-Host "        $_" -ForegroundColor Red
        Write-Host "        Possible reasons:" -ForegroundColor Red
        Write-Host "        - A program (Arduino IDE, VS Code) has files open inside data/" -ForegroundColor Red
        Write-Host "        - File Explorer is browsing that folder" -ForegroundColor Red
        Write-Host "        Close them and try again." -ForegroundColor Red
        exit 1
    }
}
New-Item -ItemType Directory -Path $DataPath -Force | Out-Null

# ---------- 4. COPY NEW CONTENT ----------
Write-Host "[4/4] Copying files into data/..." -ForegroundColor Cyan
$items = Get-ChildItem $source -Force
$count = 0
foreach ($item in $items) {
    Copy-Item $item.FullName -Destination $DataPath -Recurse -Force
    $count++
}
Write-Host "      Copied: $count top-level items"

# ---------- CLEANUP ----------
Remove-Item $TempZip -Force -ErrorAction SilentlyContinue
Remove-Item $TempDir -Recurse -Force -ErrorAction SilentlyContinue

# ---------- SUMMARY ----------
Write-Host ""
Write-Host "=== DONE ===" -ForegroundColor Green
Write-Host ""

Write-Host "Top-level content of data/:" -ForegroundColor Cyan
Get-ChildItem $DataPath | Select-Object @{N="Type";E={if($_.PSIsContainer){"DIR "}else{"FILE"}}}, Name | Format-Table -AutoSize

$totalBytes = (Get-ChildItem $DataPath -Recurse -File | Measure-Object -Property Length -Sum).Sum
$totalMB    = [math]::Round($totalBytes / 1MB, 2)
$maxMB      = [math]::Round($MaxSize / 1MB, 2)

Write-Host ("Total size:  $totalMB MB")
Write-Host ("Max allowed: $maxMB MB (SPIFFS partition)")
Write-Host ""

if ($totalBytes -gt $MaxSize) {
    Write-Host "[WARNING] data/ is LARGER than the SPIFFS partition!" -ForegroundColor Yellow
    Write-Host "          Build will fail or produce a truncated image." -ForegroundColor Yellow
    Write-Host "          Remove some files from data/ or switch to per-FW builds." -ForegroundColor Yellow
    Write-Host ""
} else {
    Write-Host "[OK] data/ fits into the SPIFFS partition." -ForegroundColor Green
}

# ---------- NEXT STEPS ----------
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Check data/ structure (index.html, ran/ etc. must be present)"
Write-Host "  2. Open GitHub Desktop, commit and push"
Write-Host "  3. Wait for GitHub Actions to rebuild spiffs.bin (2-5 min)"
Write-Host "  4. Flash ESP32 via web installer with 'Erase device' checked"
Write-Host ""