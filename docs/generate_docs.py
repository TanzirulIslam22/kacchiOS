# generate_docs.py - build the three project PDFs with fpdf2
# Run:  python generate_docs.py   (from the docs/ folder)
from fpdf import FPDF
from fpdf.fonts import FontFace
import os

OUT = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT, exist_ok=True)


class Doc(FPDF):
    def h1(self, text):
        self.set_font("Helvetica", "B", 16)
        self.ln(3)
        self.multi_cell(0, 8, text)
        self.ln(1)

    def h2(self, text):
        self.set_font("Helvetica", "B", 12)
        self.ln(2)
        self.multi_cell(0, 6.5, text)
        self.ln(0.5)

    def body(self, text):
        self.set_font("Helvetica", "", 10)
        self.multi_cell(0, 5.2, text)
        self.ln(0.5)

    def bullet(self, text):
        self.set_font("Helvetica", "", 10)
        self.multi_cell(0, 5.2, "- " + text)
        self.ln(0.2)

    def code(self, text):
        self.set_font("Courier", "", 8.5)
        self.set_fill_color(245, 245, 245)
        self.multi_cell(0, 4.4, text, fill=True)
        self.ln(1)
        self.set_font("Helvetica", "", 10)

    def add_table(self, headers, rows, widths):
        self.set_font("Helvetica", "", 8.5)
        with super().table(
            headings_style=FontFace(emphasis="BOLD", size_pt=8.5),
            line_height=4.4,
            col_widths=widths,
            text_align="LEFT",
            v_align="TOP",
        ) as t:
            hr = t.row()
            for h in headers:
                hr.cell(h)
            for row in rows:
                r = t.row()
                for cell in row:
                    r.cell(cell)
        self.ln(2)
        self.set_font("Helvetica", "", 10)


# ------------------------------------------------------------------ #
# 1. Checklist.pdf                                                    #
# ------------------------------------------------------------------ #
pdf = Doc()
pdf.set_auto_page_break(auto=True, margin=18)
pdf.add_page()
pdf.h1("Checklist - kacchiOS Lab Project (CSE 3202)")
pdf.body(
    "Every requirement of the original assignment is listed with its original "
    "category and weight. Status: 'Implemented and tested' means the feature "
    "runs in QEMU and was verified with the test described in this document."
)
pdf.ln(1)

must = [
    ("1", "Memory Manager - Stack, Heap allocation",
     "Implemented and tested",
     "memory.c: allocate_stack(), allocate_heap()",
     "Shell command 'memtest': allocates stacks and heap blocks, checks addresses and sizes. PASS"),
    ("2", "Process Manager - Process table",
     "Implemented and tested",
     "process.c: process_table[16], process_get()",
     "Shell command 'ps' prints the whole table; empty slots are visible. PASS"),
    ("3", "Process Manager - Process creation",
     "Implemented and tested",
     "process.c: process_create()",
     "'spawn' / 'server' create processes; the new pid runs and prints heartbeats. PASS"),
    ("4", "Process Manager - State transition",
     "Implemented and tested",
     "process.c: process_set_state() (single transition point)",
     "'ps' shows READY/CURRENT/BLOCKED/TERMINATED at the right moments; illegal transitions are refused. PASS"),
    ("5", "Process Manager - Process termination",
     "Implemented and tested",
     "process.c: process_exit(), process_terminate(); scheduler.c: cleanup_terminated()",
     "'kill <pid>'; the slot and stack are freed and reused by the next 'spawn'. PASS"),
    ("6", "Scheduler - Clear policy to schedule",
     "Implemented and tested",
     "scheduler.c: pick_next_ready() (round robin + aging, documented in scheduler.h)",
     "Interleaved worker heartbeats between shell prompts; all 15 workers make progress. PASS"),
    ("7", "Scheduler - Context switch",
     "Implemented and tested",
     "isr.S: timer_isr (pusha / ESP swap / popa / iret); scheduler.c: scheduler_tick()",
     "Workers preempt the shell and the shell resumes exactly where it stopped (typing continues "
     "while heartbeats appear). Real ESP swap, verified in QEMU. PASS"),
]

good = [
    ("1", "Memory Manager - Stack deallocation",
     "Implemented and tested",
     "memory.c: free_stack() (with double free / invalid pointer checks)",
     "'kill' frees the stack; 'mem' shows the slot count restored; next 'spawn' reuses it. PASS"),
    ("2", "Memory Manager - Heap deallocation",
     "Implemented and tested",
     "memory.c: free_heap() (coalesces neighbours)",
     "'memtest': free bytes return to the exact initial value after freeing all blocks. PASS"),
    ("3", "Process Manager - Utility functions to get process specific functions",
     "Implemented and tested",
     "process.c: process_current_pid(), process_get_state(), process_get_name(), "
     "process_get_stack_size(), process_count()",
     "'ps' is built entirely from these functions. PASS"),
    ("4", "Scheduler - Configurable time quantum",
     "Implemented and tested",
     "scheduler.c: scheduler_set_quantum() / scheduler_get_quantum(); shell command 'quantum'",
     "'quantum 1', 'quantum 50', 'quantum 100' verified; values are clamped to 1..100. PASS"),
]

bonus = [
    ("1", "Memory Manager - Optimized memory allocation",
     "Implemented and tested",
     "memory.c: first fit + block splitting on allocate, coalescing on free",
     "'memtest': a 50-byte request reuses a freed 200-byte block; after freeing everything "
     "the heap is one single free block again. Limitation: no best-fit/buddy system. PASS"),
    ("2", "Process Manager - Add more states",
     "Implemented and tested",
     "process.h/.c: PROCESS_BLOCKED + blocked->READY wake-up in ipc_send()",
     "'server' shows BLOCKED in 'ps'; a message wakes it; killing a BLOCKED process works. PASS"),
    ("3", "Process Manager - Inter-process communication (IPC)",
     "Implemented and tested",
     "process.c: ipc_send(), ipc_receive(), ipc_receive_wait() (one-slot mailbox)",
     "'send 1 hello' -> worker prints it; 'send 0 x' + 'inbox' round trip; "
     "full mailbox and bad pid are rejected. PASS"),
    ("4", "Scheduler - Implement Aging",
     "Implemented and tested",
     "scheduler.c: wait_ticks / SCHED_AGING_STEP inside pick_next_ready()",
     "'busy 300': the higher-priority shell spins, yet lower-priority workers still get the "
     "CPU (their heartbeats appear during the busy loop). PASS"),
]

pdf.h2("Must Include (7 x 10 = 70%)")
pdf.add_table(("#", "Requirement", "Status", "Where in the code", "Test and result"),
          must, (4, 22, 14, 27, 33))

pdf.h2("Good to have (4 x 5 = 20%)")
pdf.add_table(("#", "Requirement", "Status", "Where in the code", "Test and result"),
          good, (4, 22, 14, 27, 33))

pdf.h2("Bonus (4 x 2.5 = 10%)")
pdf.add_table(("#", "Requirement", "Status", "Where in the code", "Test and result"),
          bonus, (4, 22, 14, 27, 33))

pdf.h2("Test environment and honest status notes")
pdf.bullet("All tests were executed in QEMU 11.0 (qemu-system-i386, -m 64M) with the kernel "
           "built by 'make' (gcc -m32, freestanding).")
pdf.bullet("Every 'PASS' above comes from actually running the shown shell commands and "
           "observing the printed output, not from code inspection alone.")
pdf.bullet("A 30 second soak test (repeated create/terminate cycles, quantum changes, "
           "memtest under load) completed without crashes or memory corruption.")
pdf.bullet("Not implemented by design: user mode, paging, multiple CPUs, per-exception "
           "handlers (see Project_Report.pdf, Limitations).")

pdf.output(os.path.join(OUT, "Checklist.pdf"))

# ------------------------------------------------------------------ #
# 2. Project_Report.pdf                                               #
# ------------------------------------------------------------------ #
pdf = Doc()
pdf.set_auto_page_break(auto=True, margin=18)
pdf.add_page()
pdf.h1("kacchiOS Project Report")
pdf.body("CSE 3202 - Operating Systems Sessional. Reference implementation of the "
         "Memory Manager, Process Manager and Scheduler on the kacchiOS bare-metal starter.")

pdf.h2("1. Introduction")
pdf.body(
    "kacchiOS is a tiny 32-bit x86 operating system that boots through a Multiboot "
    "loader and talks to the outside world over the serial port. The starter already "
    "contained the boot code, a serial driver and a Null Process that reads one line "
    "of input and echoes it back. This project adds the three components of the "
    "assignment: a memory manager, a process manager and a scheduler with a real "
    "context switch."
)

pdf.h2("2. Objectives")
pdf.bullet("Allocate and free memory for processes (stacks and heap), with reuse after free.")
pdf.bullet("Manage a process table with creation, state transitions and termination.")
pdf.bullet("Switch between processes with the hardware timer and a documented policy.")
pdf.bullet("Keep the code simple enough to explain in one evening at a viva.")

pdf.h2("3. Existing system architecture")
pdf.body("Boot flow after QEMU loads the kernel at 1 MB:")
pdf.code(
    "boot.S   set ESP to a 16 KB boot stack, clear .bss, call kmain()\n"
    "kmain()  serial_init(); create the null process; scheduler_start()\n"
    "shell    the null process (pid 0) prints 'kacchiOS>' and reads a line"
)
pdf.body(
    "Existing files kept unchanged in behaviour: serial.c/h (COM1 driver), "
    "string.c/h, types.h, io.h (inb/outb), link.ld (kernel layout, exports "
    "__kernel_end). The sources were moved into src/ to match the required "
    "submission structure."
)

pdf.h2("4. Memory manager")
pdf.body("Memory layout (QEMU runs with -m 64M):")
pdf.code(
    "  1 MB          kernel image (.text, .data, .bss)\n"
    "  __kernel_end  heap starts here (8 MB)\n"
    "  64 MB         end of RAM"
)
pdf.body(
    "Stacks: the stack pool is 16 fixed slots of 8 KB inside the kernel .bss "
    "(stack_pool[][]). One slot belongs to each process. allocate_stack() scans "
    "for a free slot; free_stack() validates the pointer (inside the pool? exactly "
    "at a slot base? slot actually in use?) and marks it free again, so double free "
    "and invalid free are detected and ignored with a message."
)
pdf.body(
    "Heap: the region after the kernel is one address-ordered list of blocks with a "
    "16-byte header (size, in_use, next, prev). allocate_heap() uses first fit and "
    "splits a block when the remainder is still useful; free_heap() marks the block "
    "free and merges it with free neighbours, so the heap returns to a single large "
    "block after everything is freed. Out of memory returns NULL."
)

pdf.h2("5. Process manager")
pdf.body(
    "The process table is an array of 16 entries (like Xinu's proctab[NPROC]); the "
    "pid is simply the index. Each entry (PCB) holds: in_use, pid, state, name, "
    "entry function, stack_base, stack_size, saved_esp, priority, wait_ticks and a "
    "one-slot mailbox."
)
pdf.body(
    "process_create() finds a free entry, asks the memory manager for a stack, "
    "builds the initial stack frame and only then makes the entry visible "
    "(in_use = 1), so a timer interrupt in the middle of creation cannot see a "
    "half-built process. process_set_state() is the only function that changes a "
    "state and rejects illegal transitions. When a process finishes, process_exit() "
    "marks it TERMINATED and yields; the scheduler frees its stack at the next "
    "scheduling point, but never from the stack that is still in use."
)

pdf.h2("6. Process states and transitions")
pdf.code(
    "   create            scheduler picks it\n"
    "(free) --> READY --------------------------> CURRENT\n"
    "             ^         ^                       |\n"
    "             |         | ipc_send wakes        | scheduler rotates\n"
    "             |         |                       v\n"
    "             +---------+                    READY\n"
    "           BLOCKED <---- CURRENT\n"
    "       (waits for message, only via ipc_receive_wait)\n"
    "                                     finish: --> TERMINATED\n"
    "                                              --> stack freed, slot reused"
)
pdf.body("All transitions go through process_set_state(); e.g. TERMINATED -> READY "
         "is refused, so a finished process can never run again.")

pdf.h2("7. Scheduler")
pdf.body(
    "Policy: round robin with a configurable quantum (default 5 ticks = 50 ms at "
    "100 Hz). Every READY process gets wait_ticks++ each tick. The next process is "
    "the READY process with the highest priority + wait_ticks/10; ties are broken "
    "in round-robin order (the scan starts after the process that ran last)."
)
pdf.body(
    "When no other process is READY, the current one simply keeps the CPU. The "
    "null process (pid 0) never terminates, so there is always at least one "
    "runnable process."
)

pdf.h2("8. Context switching")
pdf.body("A process is its stack: everything that must be preserved lives on that "
         "stack. On every timer tick:")
pdf.code(
    "timer_isr (isr.S)                  scheduler_tick (scheduler.c)\n"
    "-------------------                ----------------------------\n"
    "pusha                 ; save 8 regs\n"
    "mov %esp,%ecx\n"
    "push %ecx\n"
    "call scheduler_tick  ------------> save old ESP in the PCB\n"
    "                                    pick next READY process\n"
    "                   <-------------- return its saved ESP\n"
    "mov %eax,%esp         ; switch stacks\n"
    "popa                  ; restore registers\n"
    "iret                  ; continue the new process"
)
pdf.body(
    "A new process starts with a fake frame built by build_initial_stack(): "
    "EFLAGS (interrupts on), CS, EIP = process_trampoline, plus dummy values for "
    "popa. The first iret therefore 'returns' into process_trampoline(), which "
    "calls the process entry function. yield() is just 'int $0x20' - the same "
    "interrupt path as a real timer tick."
)

pdf.h2("9. Time quantum")
pdf.body(
    "The quantum is counted in timer ticks (100 ticks = 1 second). While the "
    "current process is CURRENT, each tick decrements the counter; at zero, the "
    "scheduler picks another READY process. The shell command 'quantum <ticks>' "
    "changes it at runtime; values are clamped to 1..100."
)

pdf.h2("10. Aging")
pdf.body(
    "Aging needs no extra code path: the pick score already contains "
    "wait_ticks/AGING_STEP (10). A low-priority process that waits long enough "
    "outranks a high-priority process that keeps running, so nobody can starve. "
    "This is visible with the 'busy' command: the shell (priority 2) spins for N "
    "ticks while the workers (priority 1) still print their heartbeats."
)

pdf.h2("11. Testing")
pdf.body("All tests were run in QEMU 11.0 with the built kernel. Summary:")
pdf.add_table(
    ("Test", "How", "Result"),
    [
        ("Boot + shell", "make run, type text", "PASS"),
        ("Context switch", "spawn twice, watch heartbeats between prompts", "PASS"),
        ("Resume after preemption", "type slowly while workers print", "PASS"),
        ("States", "ps before/after spawn, send, kill", "PASS"),
        ("BLOCKED + IPC", "server; ps; send 1 hello", "PASS"),
        ("Termination + cleanup", "kill 1; mem; spawn (pid reused)", "PASS"),
        ("Table exhaustion", "spawn 16 times; last fails cleanly", "PASS"),
        ("Stack exhaustion", "memtest: allocates until NULL", "PASS"),
        ("Double free / invalid free", "memtest: both reported and ignored", "PASS"),
        ("Out of memory", "memtest: huge request returns NULL", "PASS"),
        ("Coalescing", "memtest: free bytes restored exactly", "PASS"),
        ("Quantum config", "quantum 1 / 50 / 100 / 0 / 99999 (clamped)", "PASS"),
        ("Aging", "busy 300: low priority workers still run", "PASS"),
        ("Soak test", "30 s of create/terminate/quantum churn", "PASS"),
    ],
    (22, 48, 12),
)

pdf.h2("12. Limitations")
pdf.bullet("No user mode or paging: every process is a kernel thread (Xinu model).")
pdf.bullet("Single CPU; switching relies on interrupts being off inside the ISR.")
pdf.bullet("One-slot mailbox: a second send before the first is read fails.")
pdf.bullet("Fixed 8 MB heap, sized for -m 64M in the Makefile.")
pdf.bullet("Unexpected interrupts (including CPU exceptions) halt the kernel with a "
           "message instead of being decoded.")
pdf.bullet("The scheduler decides only at quantum boundaries and at block/terminate "
           "events, not on every tick.")

pdf.h2("13. Conclusion")
pdf.body(
    "The three required components work together on real hardware emulation: the "
    "memory manager gives every process a stack and reuses everything that is "
    "freed, the process manager keeps a clean lifecycle with explicit state "
    "transitions, and the scheduler performs genuine context switches through the "
    "timer interrupt, with a configurable quantum and aging against starvation. "
    "The code stays small on purpose so that each function can be explained "
    "during the viva."
)

pdf.output(os.path.join(OUT, "Project_Report.pdf"))

# ------------------------------------------------------------------ #
# 3. Individual_Contribution.pdf                                      #
# ------------------------------------------------------------------ #
pdf = Doc()
pdf.set_auto_page_break(auto=True, margin=18)
pdf.add_page()
pdf.h1("Individual Contribution Sheet")
pdf.body("Team: Series __ Section _ Group __     (fill in before submission)")
pdf.body(
    "Each team member must record their own concrete contribution: which files or "
    "functions they wrote, which tests they ran, which parts of the report they "
    "wrote. Viva marks are awarded individually, so be specific and honest. "
    "Do not invent contributions; this sheet is a template to be filled in by "
    "the team."
)
pdf.h2("Contributions")
rows = [(str(i), "", "", "") for i in range(1, 7)]
pdf.add_table(("#", "Name", "Roll No.", "Contribution (files / functions / tests)"),
          rows, (5, 25, 15, 55))
pdf.h2("Notes")
pdf.bullet("Attach extra sheets if a contribution does not fit in one row.")
pdf.bullet("All members must sign their rows before uploading the project.")
pdf.ln(4)
pdf.body("Signatures:")
pdf.ln(6)
pdf.body("________________________________   ________________________________")
pdf.body("Member 1                                        Member 2")
pdf.ln(4)
pdf.body("________________________________   ________________________________")
pdf.body("Member 3                                        Member 4")
pdf.ln(4)
pdf.body("________________________________   ________________________________")
pdf.body("Member 5                                        Member 6")

pdf.output(os.path.join(OUT, "Individual_Contribution.pdf"))
print("PDFs written to", OUT)


