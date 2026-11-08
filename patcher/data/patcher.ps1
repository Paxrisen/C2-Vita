# C2-Vita Patcher 1.0 - erzeugt aus der eigenen civ2.exe die native C2-Vita VPK
# C2-Vita Patcher 1.0 - builds the native C2-Vita VPK from your own civ2.exe
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Data  = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root  = Split-Path -Parent $Data
$Cache = Join-Path $env:LOCALAPPDATA 'C2-Vita-Patcher'
$Work  = Join-Path $Cache 'work'
$KnownHash = 'B4D05D8755AB4E8813BBC00157B2B8A4604ABE8B44955C3C4F325ACFFDCD3763'   # civ2.exe MGE 5.4.0f (deutsch)
$PyUrl = 'https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip'
$SdkFallback = 'https://github.com/vitasdk/autobuilds/releases/download/master-win-v1498/vitasdk-x86_64-w64-mingw32.tar.bz2'

function Say($de, $en) { Write-Host ""; Write-Host "== $de" -ForegroundColor Cyan; Write-Host "   $en" -ForegroundColor DarkGray }
function Fail($de, $en) { Write-Host ""; Write-Host "FEHLER: $de" -ForegroundColor Red; Write-Host "ERROR: $en" -ForegroundColor Red; Read-Host "Enter druecken / press Enter"; exit 1 }
function Run($exe, $argList) {
    try { $p = Start-Process -FilePath $exe -ArgumentList $argList -NoNewWindow -Wait -PassThru }
    catch { Fail "$([IO.Path]::GetFileName($exe)) wurde von Windows blockiert. Intelligente App-Steuerung (Smart App Control) ausschalten, siehe LIESMICH_README.txt." "$([IO.Path]::GetFileName($exe)) was blocked by Windows. Turn off Smart App Control (see README)." }
    if ($p.ExitCode -ne 0) { Fail "$([IO.Path]::GetFileName($exe)) ist fehlgeschlagen (Code $($p.ExitCode))" "$([IO.Path]::GetFileName($exe)) failed (code $($p.ExitCode))" }
}
function Q($s) { return '"' + $s + '"' }

Write-Host "C2-Vita Patcher 1.0" -ForegroundColor Yellow
Write-Host "Dieses Programm uebersetzt DEINE civ2.exe in nativen PS-Vita-Code und baut daraus eine VPK."
Write-Host "This tool translates YOUR civ2.exe into native PS Vita code and builds a VPK from it."

# ---------------------------------------------------------------- 0. Intelligente App-Steuerung pruefen
try {
    $sac = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -Name 'VerifiedAndReputablePolicyState' -ErrorAction Stop).VerifiedAndReputablePolicyState
} catch { $sac = 0 }
if ($sac -eq 1) {
    Write-Host ""
    Write-Host "ACHTUNG: Die Intelligente App-Steuerung (Smart App Control) ist eingeschaltet." -ForegroundColor Yellow
    Write-Host "Sie blockiert die VitaSDK-Werkzeuge, der Patcher bricht dann beim Bauen der VPK ab." -ForegroundColor Yellow
    Write-Host "Bitte vorher ausschalten: Windows-Sicherheit > App- & Browsersteuerung > Intelligente App-Steuerung > Aus." -ForegroundColor Yellow
    Write-Host "Wieder einschalten (ohne Neuinstallation) siehe LIESMICH_README.txt." -ForegroundColor Yellow
    Write-Host "WARNING: Smart App Control is ON and will block the VitaSDK tools. Turn it off first (see README)." -ForegroundColor Yellow
    $a = Read-Host "Trotzdem versuchen? / Try anyway? (j/y = ja)"
    if ($a -notmatch '^[jJyY]') { exit 1 }
}

# ---------------------------------------------------------------- 1. civ2.exe waehlen
Say "civ2.exe auswaehlen" "Select civ2.exe"
$exe = $args | Select-Object -First 1
if (-not $exe) {
    $cand = @((Join-Path $Root 'civ2.exe'), (Join-Path (Split-Path -Parent $Root) 'civ2.exe')) | Where-Object { Test-Path $_ } | Select-Object -First 1
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = 'civ2.exe (Civilization II Multiplayer Gold Edition) auswaehlen / select'
    $dlg.Filter = 'civ2.exe|civ2.exe|EXE|*.exe'
    if ($cand) { $dlg.InitialDirectory = Split-Path -Parent $cand; $dlg.FileName = 'civ2.exe' }
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Fail "Keine Datei gewaehlt." "No file selected." }
    $exe = $dlg.FileName
}
if (-not (Test-Path $exe)) { Fail "Datei nicht gefunden: $exe" "File not found: $exe" }
$hash = (Get-FileHash -Algorithm SHA256 $exe).Hash
if ($hash -ne $KnownHash) {
    Write-Host "Achtung: Diese civ2.exe ist nicht die getestete Version (MGE 5.4.0f deutsch)." -ForegroundColor Yellow
    Write-Host "Warning: this civ2.exe is not the tested version (MGE 5.4.0f German)." -ForegroundColor Yellow
    $a = Read-Host "Trotzdem fortfahren? / Continue anyway? (j/y = ja)"
    if ($a -notmatch '^[jJyY]') { exit 1 }
} else { Write-Host "   civ2.exe erkannt (MGE 5.4.0f) / recognized" -ForegroundColor Green }

New-Item -ItemType Directory -Force -Path $Cache | Out-Null
if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
New-Item -ItemType Directory -Force -Path $Work, (Join-Path $Work 'gen'), (Join-Path $Work 'obj') | Out-Null

# ---------------------------------------------------------------- 2. Werkzeuge laden (einmalig)
$py = Join-Path $Cache 'python\python.exe'
if (-not (Test-Path $py)) {
    Say "Lade Python (einmalig, ca. 11 MB) ..." "Downloading Python (once, ~11 MB) ..."
    $zip = Join-Path $Cache 'python.zip'
    try { Invoke-WebRequest -Uri $PyUrl -OutFile $zip -UseBasicParsing } catch { Fail "Download von Python fehlgeschlagen. Internetverbindung pruefen." "Python download failed. Check your internet connection." }
    Expand-Archive -Path $zip -DestinationPath (Join-Path $Cache 'python') -Force
    Remove-Item $zip
}
$sdk = Join-Path $Cache 'vitasdk'
$gcc = Join-Path $sdk 'bin\arm-vita-eabi-gcc.exe'
if (-not (Test-Path $gcc)) {
    Say "Lade VitaSDK (einmalig, ca. 150 MB) ..." "Downloading VitaSDK (once, ~150 MB) ..."
    $url = $null
    try {
        $rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/vitasdk/autobuilds/releases?per_page=60' -UseBasicParsing -Headers @{ 'User-Agent' = 'C2-Vita-Patcher' }
        foreach ($r in $rel) {
            if ($r.tag_name -notlike 'master-win-*') { continue }
            $as = $r.assets | Where-Object { $_.name -like '*w64-mingw32*.tar.bz2' } | Select-Object -First 1
            if ($as) { $url = $as.browser_download_url; break }
        }
    } catch { }
    if (-not $url) { $url = $SdkFallback }
    Write-Host "   $url"
    $tb = Join-Path $Cache 'vitasdk.tar.bz2'
    try { Invoke-WebRequest -Uri $url -OutFile $tb -UseBasicParsing } catch { Fail "Download des VitaSDK fehlgeschlagen." "VitaSDK download failed." }
    Say "Entpacke VitaSDK ..." "Extracting VitaSDK ..."
    $tmp = Join-Path $Cache 'sdktmp'
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    Run "$env:SystemRoot\System32\tar.exe" @('-xjf', (Q $tb), '-C', (Q $tmp))
    $found = Get-ChildItem -Path $tmp -Recurse -Filter 'arm-vita-eabi-gcc.exe' | Select-Object -First 1
    if (-not $found) { Fail "VitaSDK unvollstaendig." "VitaSDK incomplete." }
    if (Test-Path $sdk) { Remove-Item -Recurse -Force $sdk }
    Move-Item -Path (Split-Path -Parent (Split-Path -Parent $found.FullName)) -Destination $sdk
    Remove-Item -Recurse -Force $tmp; Remove-Item $tb
}
$env:VITASDK = $sdk
$env:PATH = (Join-Path $sdk 'bin') + ';' + $env:PATH

# ---------------------------------------------------------------- 3. uebersetzen
Say "Uebersetze civ2.exe nach C (ca. 1 Minute) ..." "Translating civ2.exe to C (~1 minute) ..."
Run $py @((Q (Join-Path $Data 'recomp.py')), '-', (Q $exe), (Q (Join-Path $Work 'gen')))

# ---------------------------------------------------------------- 4. kompilieren (parallel)
$srcs = Get-ChildItem -Path (Join-Path $Work 'gen') -Filter 'native_*.c' | Sort-Object Length -Descending
$ramGB = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$jobs = [int][math]::Max(1, [math]::Min([math]::Min([int]$env:NUMBER_OF_PROCESSORS, 4), [math]::Floor(($ramGB - 2) / 2.5)))
Say "Kompiliere $($srcs.Count) Dateien mit $jobs Prozessen (10-30 Minuten) ..." "Compiling $($srcs.Count) files with $jobs processes (10-30 minutes) ..."
$flags = @('-O2', '-marm', '-march=armv7-a+simd', '-mfpu=neon', '-mfloat-abi=hard', '-w', ('-I' + (Q $Data)), '-c')
$logdir = Join-Path $Work 'log'; New-Item -ItemType Directory -Force -Path $logdir | Out-Null
$running = New-Object System.Collections.ArrayList; $done = 0; $objs = @()
function Finish($e) {
    $e.P.WaitForExit()
    $code = $e.P.ExitCode
    if ($code -ne 0 -or -not (Test-Path $e.O)) {
        Write-Host ""; Write-Host "Compiler-Ausgabe / compiler output ($($e.N)):" -ForegroundColor Yellow
        if (Test-Path $e.L) { Get-Content $e.L -Tail 25 | ForEach-Object { Write-Host "   $_" } }
        Fail "Kompilieren von $($e.N) fehlgeschlagen (Code $code). Bitte Bildschirmfoto schicken." "Compiling $($e.N) failed (code $code)."
    }
    $script:done++; Write-Host "   $($script:done) / $($srcs.Count)  ($($e.N))"
}
foreach ($s in $srcs) {
    while ($running.Count -ge $jobs) {
        Start-Sleep -Milliseconds 500
        foreach ($e in @($running)) { if ($e.P.HasExited) { Finish $e; $running.Remove($e) } }
    }
    $o = Join-Path (Join-Path $Work 'obj') ($s.BaseName + '.o'); $objs += $o
    $lg = Join-Path $logdir ($s.BaseName + '.txt')
    $p = Start-Process -FilePath $gcc -ArgumentList ($flags + @((Q $s.FullName), '-o', (Q $o))) -NoNewWindow -PassThru -RedirectStandardError $lg
    $null = $p.Handle   # noetig, sonst liefert PowerShell spaeter keinen ExitCode
    [void]$running.Add([pscustomobject]@{ P = $p; O = $o; L = $lg; N = $s.Name })
}
foreach ($e in @($running)) { Finish $e }

# ---------------------------------------------------------------- 5. linken + VPK bauen
Say "Baue die VPK ..." "Building the VPK ..."
$ob = Join-Path $Data 'obj'
$elf = Join-Path $Work 'c2vita.elf'; $velf = Join-Path $Work 'c2vita.velf'; $eboot = Join-Path $Work 'eboot.bin'; $sfo = Join-Path $Work 'param.sfo'
$link = @('-marm', '-nostdlib', '-nostartfiles', '-Wl,-q', (Q (Join-Path $ob 'c2_pre.o')), (Q (Join-Path $ob 'c2_main.o')))
$link += ($objs | ForEach-Object { Q $_ })
$link += @((Q (Join-Path $ob 'c2_post.o')), '-o', (Q $elf))
Run $gcc $link
Run (Join-Path $sdk 'bin\vita-elf-create.exe') @((Q $elf), (Q $velf))
Run (Join-Path $sdk 'bin\vita-make-fself.exe') @('-s', (Q $velf), (Q $eboot))
Run (Join-Path $sdk 'bin\vita-mksfoex.exe') @('-s', 'TITLE_ID=CIV200002', '-s', 'APP_VER=01.00', '-d', 'ATTRIBUTE2=12', 'C2-Vita', (Q $sfo))
$pk = Join-Path $Data 'pkg'
$out = Join-Path $Root 'C2-Vita_1.0_nativ.vpk'
if (Test-Path $out) { Remove-Item $out }
$pa = @('-s', (Q $sfo), '-b', (Q $eboot))
foreach ($f in @('icon0.png', 'livearea\contents\bg.png', 'livearea\contents\startup.png', 'livearea\contents\template.xml')) {
    $pa += @('-a', ((Q (Join-Path $pk ('sce_sys\' + $f))) + '=sce_sys/' + ($f -replace '\\', '/')))
}
foreach ($d in @('fonts', 'licenses')) {
    Get-ChildItem -Path (Join-Path $pk $d) -File | ForEach-Object { $pa += @('-a', ((Q $_.FullName) + "=$d/" + $_.Name)) }
}
$pa += (Q $out)
Run (Join-Path $sdk 'bin\vita-pack-vpk.exe') $pa

Remove-Item -Recurse -Force $Work
Write-Host ""
Write-Host "FERTIG! Deine VPK: $out" -ForegroundColor Green
Write-Host "DONE! Your VPK: $out" -ForegroundColor Green
Write-Host "Die VPK enthaelt Code aus deiner civ2.exe - nur fuer dich, nicht weitergeben."
Write-Host "The VPK contains code from your civ2.exe - for your own use only, do not share it."
Read-Host "Enter druecken / press Enter"
