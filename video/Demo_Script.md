# kacchiOS Demo Script

A reproducible step-by-step demo of every required feature.  Use this
instead of (or before) recording `Demo_Video.mp4`.

## 0. Build

```bash
cd src
make clean && make
```

## 1. Boot and the Null Process

```bash
make run          # or: qemu-system-i386 -kernel kernel.elf -m 64M -serial stdio -display none
```

Expected:

```
========================================
    kacchiOS - Minimal Baremetal OS
========================================
Hello from kacchiOS!
type 'help' for commands

Starting the scheduler...
kacchiOS>
```

Type `hello` and Enter -> `You typed: hello` (the original Null Process
behaviour still works; the shell is itself just process 0).

## 2. Process table and context switching

Type:

```
spawn
spawn
```

Expected: `created worker with pid 1` / `created worker with pid 2`.
Then watch the screen without typing anything: every 2 seconds a line

```
[worker 1] alive at tick ...
[worker 2] alive at tick ...
```

appears **between shell prompts**.  The shell is blocked waiting for a
key, the workers are preempted and resumed by the timer — only a real
context switch produces this interleaving.

Now type `ps` slowly.  While you type, worker lines appear between the
echoed characters, and the command still works afterwards: proof that
the interrupted shell really resumes exactly where it stopped.

## 3. Process states and IPC

```
server
ps
```

Expected: the server shows state `BLOCKED`.  Then:

```
send 1 hello
```

Expected: `[server] received: hello` and `ps` shows it `BLOCKED` again.

## 4. Termination, cleanup, memory reuse

```
kill 1
ps
mem
spawn
```

Expected: pid 1 becomes free (shown as free slot after the next tick),
`free stack slots` increases, and the new `spawn` gets **pid 1 again**
with a fresh heartbeat — the stack slot and table entry were reused.

## 5. Allocation failure / exhaustion

Press `spawn` fifteen times (pids 1..15), then once more:

```
process_create: process table is full
spawn failed
```

`mem` now shows `free stack slots = 0`.  `memtest` runs the full
memory-manager self test (stack/heap alloc, reuse, double free
detection, invalid free detection, out-of-memory NULL, coalescing).

## 6. Configurable quantum

```
quantum 1
```

Expected: `quantum set to 1 ticks (100 ticks = 1 second)`;
`ps` shows the new value.  Heartbeats get closer together.

## 7. Aging (bonus)

```
busy 300
```

The shell (higher priority) spins for 300 ticks without blocking.
Expected: worker heartbeats still appear during the loop — their
waiting time grows until they outrank the busy shell.

## 8. Automated run (optional)

The following PowerShell snippet boots the OS, feeds the commands over
QEMU's TCP serial port and prints everything the OS sends back
(requires QEMU on the PATH or adjust the path):

```powershell
$qemu = "C:\Program Files\qemu\qemu-system-i386.exe"
$elf  = ".\src\kernel.elf"
$p = Start-Process $qemu -ArgumentList @('-kernel', $elf, '-m', '64M',
        '-serial', 'tcp:127.0.0.1:4477,server,nowait', '-display', 'none') -PassThru
Start-Sleep 2
$c = New-Object System.Net.Sockets.TcpClient; $c.Connect('127.0.0.1', 4477)
$s = $c.GetStream()
foreach ($cmd in @('spawn','spawn','ps','send 1 hi','busy 200','kill 1','mem')) {
    $b = [Text.Encoding]::ASCII.GetBytes($cmd + "`r")
    $s.Write($b, 0, $b.Length); $s.Flush(); Start-Sleep -Milliseconds 800
    while ($s.DataAvailable) { [Console]::Write([char]$s.ReadByte()) }
}
Start-Sleep 3
while ($s.DataAvailable) { [Console]::Write([char]$s.ReadByte()) }
$c.Close(); Stop-Process $p.Id
```
