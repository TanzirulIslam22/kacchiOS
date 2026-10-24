# run-tests.ps1 - boot kacchiOS in QEMU, send shell commands over the
# serial port, and print everything the OS printed.
#
# Run from PowerShell, from any directory:
#
#   .\tests\run-tests.ps1
#
# Or explicitly:
#
#   .\tests\run-tests.ps1 -Elf "D:\...\kacchiOS\src\kernel.elf" `
#                         -Commands "spawn`nps`nmem`nmemtest`nkill 1`nmem"
#
# Prerequisites: QEMU for Windows at
# "C:\Program Files\qemu\qemu-system-i386.exe" (override with -QemuPath)
# and a kernel built with `make` in src/ (WSL is fine for that).
param(
    [string]$Elf = "",
    [string]$Commands = "spawn`nserver`nps`nsend 1 hello`nmem`nmemtest`nkill 1`nmem",
    [int]$BootDelay = 2,
    [int]$SettleSeconds = 3,
    [string]$QemuPath = "C:\Program Files\qemu\qemu-system-i386.exe"
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
if ($Elf -eq "") { $Elf = Join-Path $repoRoot "src\kernel.elf" }

if (-not (Test-Path $QemuPath)) {
    Write-Error "QEMU not found at '$QemuPath'. Install QEMU for Windows or pass -QemuPath."
    exit 1
}
if (-not (Test-Path $Elf)) {
    Write-Error "kernel image not found at '$Elf'. Build it first: wsl -- bash -lc `"cd '.../kacchiOS/src' && make`""
    exit 1
}

# A leftover QEMU instance would hold the serial TCP port.
Get-Process qemu-system-i386 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

$port = 4477
$script:collected = New-Object System.Text.StringBuilder

function Drain-Stream($stream, $ms) {
    $deadline = (Get-Date).AddMilliseconds($ms)
    while ((Get-Date) -lt $deadline) {
        if ($stream.DataAvailable) {
            $b = $stream.ReadByte()
            if ($b -ge 0) { [void]$script:collected.Append([char]$b) }
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
}

$p = Start-Process -FilePath $QemuPath -ArgumentList @(
        '-kernel', "`"$Elf`"", '-m', '64M',
        '-serial', "tcp:127.0.0.1:$port,server,nowait",
        '-display', 'none'
     ) -PassThru -WindowStyle Hidden

try {
    Start-Sleep -Seconds $BootDelay
    $client = $null
    for ($try = 0; $try -lt 20 -and $null -eq $client; $try++) {
        try {
            $c = New-Object System.Net.Sockets.TcpClient
            $c.Connect('127.0.0.1', $port)
            $client = $c
        } catch {
            Start-Sleep -Milliseconds 300
            $c.Dispose()
        }
    }
    if ($null -eq $client) { throw "could not connect to QEMU serial port" }
    $stream = $client.GetStream()

    Drain-Stream $stream 1000          # banner so far

    foreach ($line in ($Commands -split "`n")) {
        if ($line -eq '') { continue }
        $bytes = [Text.Encoding]::ASCII.GetBytes($line + "`r")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        Drain-Stream $stream 800
    }

    Drain-Stream ($stream) ($SettleSeconds * 1000)
    $client.Close()
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
}

Write-Output $script:collected.ToString()
