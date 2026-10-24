/* isr.s - Assembly stubs for the interrupt handlers
 *
 * How a context switch works, step by step:
 *
 *   1. The timer chip raises IRQ0.  The CPU (after remapping through the
 *      PIC, vector 32) saves EFLAGS, CS and EIP on the stack of the
 *      process that was just interrupted and jumps to timer_isr below.
 *   2. pusha saves the eight general purpose registers on the SAME
 *      stack.  From now on the complete machine state of this process
 *      lives on its own stack; we only need to remember the stack
 *      pointer.
 *   3. scheduler_tick() (C code) stores that stack pointer in the
 *      process table, picks the next READY process and returns the
 *      stack pointer of THAT process.
 *   4. We load the new stack pointer, popa restores the new process's
 *      registers and iret pops EIP/CS/EFLAGS, continuing exactly where
 *      that process was interrupted (or, for a brand new process, at
 *      its entry point).
 *
 * So "switching processes" really just means: change ESP to the saved
 * stack pointer of another process.  Everything else is already on
 * that process's stack.
 */

.section .text

/* ------------------------------------------------------------------ */
/* Timer interrupt (vector 32)                                         */
/* ------------------------------------------------------------------ */

.global timer_isr
.extern scheduler_tick

timer_isr:
    pusha                       /* save all general purpose registers */
    mov  %esp, %ecx             /* ecx = address of the saved frame    */
    push %ecx                   /* argument of scheduler_tick(frame)   */
    call scheduler_tick         /* returns stack pointer of next process */
    mov  %eax, %esp             /* switch onto the next process stack  */
    popa                        /* restore that process's registers    */
    iret                        /* resume (or start) that process      */

/* ------------------------------------------------------------------ */
/* Every other interrupt vector lands here                             */
/* ------------------------------------------------------------------ */

/*
 * We only enable the timer interrupt, so nothing else should ever
 * arrive.  If it does (for example a CPU exception caused by a bug),
 * printing a message and stopping is much safer than pretending
 * nothing happened.  We never return from here, so it does not matter
 * that CPU exceptions push an extra error code: no iret is executed.
 */
.global unexpected_isr
.extern unexpected_interrupt

unexpected_isr:
    call unexpected_interrupt   /* prints a message, does not return  */
1:  hlt
    jmp 1b
