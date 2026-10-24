# kacchiOS — Memory Manager, Process Manager and Scheduler

A beginner-friendly **reference implementation** of the CSE 3202 (Operating
Systems Sessional) lab project, built on the provided
[kacchiOS](https://github.com/farhan-shakib/kacchiOS) bare-metal starter.

The starter OS boots and runs a Null Process (an interactive shell over the
serial port). This project adds the three required components:

1. **Memory manager** — allocates and frees process stacks and heap blocks,
   with reuse after free.
2. **Process manager** — process table, creation, state transitions,
   termination, utility functions and simple message passing (IPC).
3. **Scheduler** — round-robin with aging, configurable time quantum and a
   **real context switch** driven by the hardware timer.

Everything is written in plain C (plus ~40 lines of assembly for the
interrupt stubs) and is intended to be readable by a student who knows basic
C but little about operating systems. This is an **educational** project,
not a production operating system.

## A. Quick start

```powershell
# build (WSL)
wsl -- bash -lc "cd '/mnt/d/CSE-RUET/3-2/Self/CSE 3202/Final Lab Project/kacchiOS/src' && make"
# run (Windows QEMU)
& "C:\Program Files\qemu\qemu-system-i386.exe" -kernel "D:\CSE-RUET\3-2\Self\CSE 3202\Final Lab Project\kacchiOS\src\kernel.elf" -m 64M -serial stdio -display none
# automated test
.\tests\run-tests.ps1
```

Sections D–G explain every step in detail.

---

## B. Features

**Implemented and verified in QEMU (see [I. Testing and troubleshooting](#i-testing-and-troubleshooting))**

| Area | Feature |
|---|---|
| Memory | stack allocation (`allocate_stack`) and deallocation (`free_stack`) |
| Memory | heap allocation (`allocate_heap`) and deallocation (`free_heap`) |
| Memory | reuse of freed stacks and heap blocks |
| Memory | double-free and invalid-free detection (rejected with a message) |
| Memory | out-of-memory handling (returns `NULL`, no crash) |
| Memory | optimization: first fit + block splitting + coalescing |
| Process | 16-entry process table with integer pids (index-based) |
| Process | process creation with an initial stack frame and execution context |
| Process | explicit state transitions through one function (`process_set_state`) |
| Process | termination, deferred stack cleanup, slot and stack reuse |
| Process | utility functions (`process_current_pid`, `process_get_state`, …) |
| Process | invalid pid and table-exhaustion handling |
| Process | extra states: `BLOCKED` (bonus) |
| Process | IPC: one-slot mailbox, `send` / `inbox`, blocking receive (bonus) |
| Scheduler | real context switch (ESP swap in the timer interrupt handler) |
| Scheduler | round-robin policy with configurable time quantum |
| Scheduler | aging so low-priority processes cannot starve (bonus) |
| Scheduler | Null Process always runnable, never terminable |
| Platform | 100 Hz PIT timer, IDT, 8259 PIC remap, `sti` |

**Not implemented (documented limitations)**

* User mode, paging, memory protection — every process is a kernel thread
  (the same model as Xinu's processes).
* Multiple CPUs.
* Individual CPU-exception decoding (unexpected interrupts halt the kernel
  with a message).
* Keyboard driver — input comes from the serial port only.
* `video/Demo_Video.mp4` — the demo video must still be recorded; only the
  reproducible script (`video/Demo_Script.md`) exists.

## C. Project structure

```text
kacchiOS/
├── LICENSE                 MIT license (see K)
├── README.md               this file
├── src/
│   ├── boot.S              Multiboot header, first stack, calls kmain()
│   ├── kernel.c            boot sequence + Null Process (shell) + demo processes
│   ├── memory.c/.h         memory manager: stack pool + heap
│   ├── process.c/.h        process manager: table, lifecycle, IPC
│   ├── scheduler.c/.h      scheduler: policy, quantum, aging
│   ├── interrupts.c/.h     IDT, 8259 PIC remap, PIT timer (100 Hz)
│   ├── isr.S               assembly stubs for the timer interrupt
│   ├── serial.c/.h         serial (COM1) driver         — from the starter
│   ├── string.c/.h         strlen/strcmp/strcpy          — from the starter
│   ├── types.h, io.h       basic types, inb/outb         — from the starter
│   ├── link.ld             linker script (kernel at 1 MB) — from the starter
│   └── Makefile            builds kernel.elf (works under WSL)
├── docs/
│   ├── Checklist.pdf           requirement-by-requirement status
│   ├── Project_Report.pdf      design document
│   ├── Individual_Contribution.pdf
│   └── generate_docs.py        regenerates the three PDFs (fpdf2)
├── tests/
│   └── run-tests.ps1       automated boot + command test script (Windows)
├── video/
│   ├── Demo_Script.md      reproducible demo (record this for Demo_Video.mp4)
│   └── README.md           notes about the demo video
└── .gitignore              ignores *.o and src/kernel.elf
```

## D. Prerequisites (Windows users)

| Tool | Used for | Check it |
|---|---|---|
| Git for Windows | cloning/history | `git --version` |
| WSL + Ubuntu | building the kernel | `wsl --status`, `wsl -l -v` |
| GCC (32-bit) + Make, inside WSL | compiling | see below |
| QEMU for Windows | running the OS | PowerShell command below |
| VS Code (optional) | editing | — |

```powershell
# PowerShell
wsl --status
wsl -l -v
git --version
Test-Path "C:\Program Files\qemu\qemu-system-i386.exe"     # should print True
```

If `Test-Path` prints `False`, QEMU is installed elsewhere — adjust the path
in the commands below (or reinstall QEMU for Windows).

Inside WSL:

```bash
gcc --version        # needs 32-bit support (gcc-multilib on Ubuntu)
make --version
```

If `gcc` is missing or cannot build 32-bit code:

```bash
sudo apt-get update
sudo apt-get install build-essential gcc-multilib
```

> **Note:** every path in this project contains spaces, so always quote the
> paths (single quotes inside `bash -lc "..."`, double quotes in PowerShell).

## E. Opening the project in Windows and WSL

The Windows drive `D:` appears in WSL as `/mnt/d/`:

| Windows | WSL |
|---|---|
| `D:\CSE-RUET\3-2\Self\CSE 3202\Final Lab Project\kacchiOS` | `/mnt/d/CSE-RUET/3-2/Self/CSE 3202/Final Lab Project/kacchiOS` |
| `...\kacchiOS\src` | `.../kacchiOS/src` |

```bash
cd "/mnt/d/CSE-RUET/3-2/Self/CSE 3202/Final Lab Project/kacchiOS/src"
```

Build in WSL (the Makefile and `gcc` live there), run in Windows (QEMU for
Windows lives there). Both operate on the same files.

## F. Build the project (WSL)

```powershell
wsl -- bash -lc "cd '/mnt/d/CSE-RUET/3-2/Self/CSE 3202/Final Lab Project/kacchiOS/src' && make"
```

What this does:

1. starts WSL,
2. changes to the Linux path of the Windows project directory (the `&&`
   stops the chain if the directory does not exist),
3. runs GNU Make, which compiles every `.c`/`.S` file and links
   **`src/kernel.elf`** — the bootable kernel image.

Useful messages:

* `make: Nothing to be done for 'all'.` — Make considers the existing files
  up to date. This only means "no rebuild needed"; it does **not** prove the
  kernel boots or works. To force a fresh build:

```powershell
wsl -- bash -lc "cd '/mnt/d/CSE-RUET/3-2/Self/CSE 3202/Final Lab Project/kacchiOS/src' && make clean && make"
```

  (`make clean` is defined in the Makefile; it only removes `*.o` and
  `kernel.elf`.)

* If `gcc` or `make` is not found, re-read section D.

The built image is always: `D:\...\kacchiOS\src\kernel.elf`.

## G. Run the OS in QEMU (Windows)

```powershell
& "C:\Program Files\qemu\qemu-system-i386.exe" -kernel "D:\CSE-RUET\3-2\Self\CSE 3202\Final Lab Project\kacchiOS\src\kernel.elf" -m 64M -serial stdio -display none
```

* `-kernel` — loads the Multiboot kernel image directly (no bootloader needed).
* `-m 64M` — guest RAM; the kernel's 8 MB heap assumes this.
* `-serial stdio` — the OS talks to this terminal (its only console).
* `-display none` — no VGA window; everything happens in the terminal.

You should see:

```text
========================================
    kacchiOS - Minimal Baremetal OS
========================================
Hello from kacchiOS!
type 'help' for commands

Starting the scheduler...
kacchiOS>
```

Type a command and press **Enter**. Quit QEMU with **Ctrl+A** then **X**
(that is QEMU's escape sequence for `-serial stdio`).

Troubleshooting input/output:

* Blank terminal → you probably dropped `-serial stdio`; the OS has no
  other console.
* Typing does nothing → type in the same window QEMU is running in and
  press Enter (the shell accepts both CR and LF as end-of-line).
* `make run` fails inside WSL with `qemu-system-i386: command not found` —
  the Makefile's `run` target expects QEMU on the PATH. On Windows, use the
  full-path command above instead.
* The Makefile also has `run-vga` (QEMU window with serial in the console)
  and `debug` (waits for GDB on port 1234) for Linux-style setups.

## H. Shell commands

The Null Process (pid 0) is an interactive shell. `help` prints:

```text
commands:
  help            this list
  ps              show the process table
  spawn           create a worker process
  server          create a server (blocks until messaged)
  kill <pid>      terminate a process
  quantum <ticks> set the time quantum (100 ticks = 1s)
  send <pid> <msg> send a message to a process
  inbox           read the shell's mailbox
  busy <ticks>    burn CPU (demonstrates aging)
  mem             memory manager statistics
  memtest         run the memory manager self test
```

| Command | Purpose | Example and expected result |
|---|---|---|
| `help` | list the commands | `help` → the block above |
| `spawn` | create a worker (prints a heartbeat) | `spawn` → `created worker with pid 1`; later `[worker 1] alive at tick 583` |
| `server` | create a server that sleeps until messaged | `server` → `created server with pid 2 (try: send 2 hello)`; in `ps` it shows `BLOCKED` |
| `ps` | show the process table | see format below |
| `kill <pid>` | terminate a process (pid 0 is protected) | `kill 1` → `process terminated`; its stack slot is freed |
| `quantum <ticks>` | change the time quantum (clamped to 1..100) | `quantum 50` → `quantum set to 50 ticks (100 ticks = 1 second)` |
| `send <pid> <msg>` | put a message into a process's mailbox | `send 1 hello` → `message sent`; the worker prints `[worker 1] got message: hello` |
| `inbox` | read the shell's own mailbox | `send 0 ping` then `inbox` → `inbox: ping` |
| `busy <ticks>` | burn CPU for N ticks (aging demo) | `spawn` then `busy 300` → worker heartbeats still appear during the loop |
| `mem` | memory statistics | `mem` → `heap free bytes = 8388592`, `free stack slots = 14` |
| `memtest` | memory-manager self test | every check prints `OK` (full output in section I) |

`ps` output format (pid, state, priority, waiting ticks, name):

```text
 pid  state        prio  wait  name
 ---  -----------  ----  ----  ----------------
  0  CURRENT         2     0  null
  1  READY           1     0  worker
  2  BLOCKED         1     0  server
  3  (free slot)
...
processes alive: 3, ticks since boot: 549, quantum: 5 ticks
```

**Process ids** are simply table indices `0..15`. Pid 0 is the Null Process
(shell) and can never be killed. The first `spawn` gets pid 1, the next pid
2, and so on; freed pids are reused by later creations.

**States** shown by `ps`: `CURRENT` (running now), `READY` (waiting for the
CPU), `BLOCKED` (sleeping in a blocking receive), `(free slot)` (unused, or
terminated and already cleaned up). The internal `TERMINATED` state exists
only briefly between `process_exit()` and the stack cleanup at the next
scheduling point.

## I. Testing and troubleshooting

All tests below were executed in QEMU 11.0 against `src/kernel.elf` built by
`make`. A test counts as passed only if it was actually run.

### 1. Memory (one command covers most of it)

```text
kacchiOS> memtest
--- memory test start ---
heap free bytes = 8388592
free stack slots = 13
stack1 = 0x0010dde0  stack2 = 0x0010fde0
stack reuse: OK
next line should report a double free:
free_heap: double free ignored
double free was ignored: OK
heap reuse: OK
coalesce after free: OK
out of memory returns NULL: OK
next line should report an invalid pointer:
free_heap: pointer is not a heap block
invalid free was ignored: OK
stack slots allocated until failure = 11
free stack slots after test = 13
--- memory test end ---
```

Every `OK` must appear, and `mem` afterwards must show the same free bytes
and stack-slot count as before (`heap free bytes = 8388592`).

### 2. Processes and state transitions

```text
spawn        -> created worker with pid 1
server       -> created server with pid 2 (try: send 2 hello)
ps           -> pid 0 CURRENT, pid 1 READY, pid 2 BLOCKED, rest free
send 1 hello -> message sent, then [worker 1] got message: hello
send 0 ping  -> message sent
inbox        -> inbox: ping
kill 1       -> process terminated
mem          -> free stack slots increased again (stack reused later)
spawn        -> reuses the freed pid/slot
kill 99      -> kill failed (invalid pid?)
kill 0       -> kill: the null process cannot be terminated
spawn x16    -> process_create: process table is full / spawn failed
```

### 3. Scheduling and context switching

* `spawn` twice, then just watch: the `[worker ...] alive at tick ...`
  lines appear **between** shell prompts — the timer preempted the shell
  and later resumed it exactly where it stopped.
* `quantum 1` makes switches frequent, `quantum 100` rare; both are
  accepted, values outside 1..100 are clamped.
* Aging: `spawn` then `busy 300` — the shell has *higher* priority than the
  workers, yet their heartbeats still print during the busy loop.

### 4. Automated end-to-end script

From the repository root in PowerShell:

```powershell
.\tests\run-tests.ps1
```

It boots `src\kernel.elf` in QEMU (serial over TCP, headless), sends a
default command session (`spawn, server, ps, send, mem, memtest, kill, mem`)
and prints the full OS output. Options:

```powershell
.\tests\run-tests.ps1 -Commands "spawn`nps`nkill 1`nmem" -SettleSeconds 5
.\tests\run-tests.ps1 -QemuPath "E:\qemu\qemu-system-i386.exe"
```

If script execution is blocked: `powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1`.
The script requires Windows QEMU (default `C:\Program Files\qemu\...`);
override with `-QemuPath` if your install differs.

### 5. Common problems

| Symptom | Diagnosis | Fix |
|---|---|---|
| `make: Nothing to be done for 'all'.` | files up to date (not a correctness proof) | `make clean && make` |
| `gcc: command not found` / `make: No such file` | WSL tools missing | `sudo apt-get install build-essential gcc-multilib` |
| `error: cannot find -lgcc` or 32-bit link errors | no multilib support | install `gcc-multilib` |
| `kernel.elf: No such file` in QEMU | not built yet | run the build command in F |
| QEMU: `'qemu-system-i386' is not recognized` | not on PATH | use the full path (section G) |
| QEMU exits immediately / no banner | wrong image or flags | rebuild, then check `-kernel ...kernel.elf -m 64M -serial stdio` |
| Terminal is blank | serial not connected to the terminal | add `-serial stdio` |
| Typing does nothing | wrong window, or Enter not pressed | type in QEMU's window, press Enter; quit with Ctrl+A, X |
| `command not found` from the shell | typo, or missing argument | type `help`; `kill`/`quantum`/`send` need numbers |
| Test script: `could not connect to QEMU serial port` | a leftover QEMU holds port 4477 | `Get-Process qemu-system-i386 \| Stop-Process -Force` |
| Test script: `kernel image not found` | built in the wrong place | build first; image must be `src\kernel.elf` |
| Worker/server never appears in `ps` | process already terminated | `spawn` again and check quickly |

## J. Development and contribution notes

| Module | Role | Change something here when… |
|---|---|---|
| `src/memory.c` | stack pool + heap allocator | allocation/free/reuse misbehaves |
| `src/process.c` | process table, lifecycle, IPC | creation, states or messages are wrong |
| `src/scheduler.c` | policy, quantum, aging, cleanup | the wrong process runs, or nobody runs |
| `src/isr.S` | saves registers, swaps ESP | context switch corrupts registers |
| `src/interrupts.c` | IDT/PIC/PIT setup | no timer ticks, or interrupts never fire |
| `src/kernel.c` | boot order, shell, demo processes | a shell command misbehaves |

Workflow after any change:

```powershell
# 1. rebuild
wsl -- bash -lc "cd '/mnt/d/CSE-RUET/3-2/Self/CSE 3202/Final Lab Project/kacchiOS/src' && make clean && make"
# 2. test
.\tests\run-tests.ps1
# 3. interactive check if needed (section G)
```

Where to look first when something fails:

* **No output at all** → `src/serial.c`, boot flags in `src/boot.S`.
* **Boots but no prompt** → `kmain()` in `src/kernel.c`.
* **Wrong process runs / crash after a while** → `src/isr.S` +
  `scheduler_tick()` in `src/scheduler.c`.
* **Command not listed** → `shell_run_command()` in `src/kernel.c`.
* **Memory stats look wrong** → `src/memory.c`.

## K. License and author

* **Author:** Tanzirul Islam
* **Repository:** https://github.com/TanzirulIslam22/kacchiOS
* **License:** MIT — see [`LICENSE`](LICENSE).

The original kacchiOS starter was created by **Md Farhan Shakib**
(https://github.com/farhan-shakib/kacchiOS) and is itself MIT-licensed;
its copyright notice is preserved in `LICENSE` alongside the notice for the
present work. Not every file was originally authored by the current
author — the starter files (`boot.S`, `serial.*`, `string.*`, `types.h`,
`io.h`, `link.ld`, the original Makefile) come from the upstream project.
See `docs/Project_Report.pdf` for the full design document.

---

## Design details: how the pieces fit together

#### Boot flow

```text
QEMU (multiboot) -> boot.S: set ESP, clear .bss, call kmain()
                 -> kmain():
                      serial_init()
                      process_table_init()     // empty table
                      memory_init()            // stack pool + 8 MB heap
                      scheduler_init()
                      interrupts_init()        // IDT + PIC + 100 Hz PIT + sti
                      process_create("null", ...)   // pid 0
                      scheduler_start()        // int $0x20 -> jumps into pid 0
```

After `scheduler_start()` the kernel never returns to `kmain()`; the timer
interrupt decides who runs from then on.

#### Where memory comes from

```text
1 MB   +---------------------+
       | kernel .text/.data  |
       | kernel .bss         |  <- stack_pool[16][8 KB] lives here
__kernel_end
       | heap (8 MB)         |  <- allocate_heap()/free_heap()
       | unused RAM          |
64 MB  +---------------------+   (QEMU is started with -m 64M)
```

* **Stacks** come from 16 fixed 8 KB slots — one per process. Fixed slots
  make allocation, free, reuse and double-free detection trivial.
* **The heap** is one address-ordered list of blocks. `allocate_heap()`
  uses first fit and splits large blocks; `free_heap()` merges the block
  with free neighbours (coalescing) so freed memory always becomes one
  large block again.

#### Context switching (the important part)

A process *is* its stack: all of its registers live on that stack while it
is not running. On every timer tick:

```text
timer_isr (isr.S)                    scheduler_tick (scheduler.c)
-----------------                    ----------------------------
pusha                ; save regs
mov %esp,%ecx
push %ecx
call scheduler_tick  ---------------->  save old ESP in the PCB
                                        quantum_left--
                                        pick next READY process
                     <----------------  return its saved ESP
mov %eax,%esp        ; load new ESP
popa                 ; restore regs
iret                 ; continue the new process (or start a new one)
```

A brand-new process gets a fake stack frame built by `process_create()`
(see `build_initial_stack()` in `process.c`), so its first `iret`
"returns" into `process_trampoline()`, which simply calls the process's
entry function. `yield()` is one line: `int $0x20` — the same interrupt
path as a real timer tick.

#### Scheduling policy

* Round robin with a configurable quantum (default 5 ticks = 50 ms).
* Every READY process gains `wait_ticks++` each tick.
* Next process = highest `priority + wait_ticks / 10`; ties are broken in
  round-robin order.
* With equal priorities this is plain round robin. A low-priority process
  accumulates waiting time until it wins anyway — that is the **aging**
  that prevents starvation.

#### Process states

```text
create:  (free slot) -> READY
pick:    READY -> CURRENT
rotate:  CURRENT -> READY
block:   CURRENT -> BLOCKED   (inside ipc_receive_wait)
wake:    BLOCKED -> READY     (inside ipc_send)
finish:  READY/CURRENT/BLOCKED -> TERMINATED -> (stack freed, slot reused)
```

All transitions go through the single function `process_set_state()`,
which rejects illegal transitions.

### File-by-file reading order

1. **src/boot.S** — how the machine reaches C.
2. **src/kernel.c** — `kmain()` then the Null Process shell.
3. **src/memory.h / memory.c** — two pools, one function at a time.
4. **src/process.h / process.c** — the process table and lifecycle.
5. **src/scheduler.c** — `scheduler_tick()` is the heart of the OS.
6. **src/isr.S** — the assembly that does the actual ESP swap.
7. **src/interrupts.c** — how the timer interrupt is wired up.

### Known limitations

* No user mode, no paging, no memory protection: every process is a kernel
  thread (same model as Xinu's processes).
* Single-CPU only; the context switch assumes interrupts are off
  (guaranteed by the interrupt gate).
* The mailbox holds one message; a second `send` before the first is read
  fails (reported to the sender).
* The heap is a fixed 8 MB region after the kernel image, sized for the
  `-m 64M` QEMU configuration.
* CPU exceptions print "Unexpected interrupt" and halt the kernel; they are
  not decoded individually.
* The scheduler only makes switching decisions at quantum boundaries or
  when a process terminates/blocks — not at every tick.
