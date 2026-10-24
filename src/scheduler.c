/* scheduler.c - Round-robin scheduler with aging and real context switch
 *
 * The whole scheduler fits in one idea:
 *
 *   "A process is its stack.  To switch processes, we save the current
 *    stack pointer in the process table and load the saved stack
 *    pointer of the next process.  popa + iret then continue that
 *    process exactly where it left off."
 *
 * scheduler_tick() is called from timer_isr (see isr.s) 100 times per
 * second.  It decides who runs next and returns that process's saved
 * stack pointer; the assembly stub does the actual ESP swap.
 */
#include "scheduler.h"
#include "process.h"
#include "memory.h"
#include "interrupts.h"
#include "serial.h"

static int      current_pid  = PROCESS_NONE; /* process owning the CPU */
static int      quantum_ticks = SCHED_DEFAULT_QUANTUM;
static int      quantum_left  = SCHED_DEFAULT_QUANTUM;
static int      started       = 0;           /* 1 once scheduler_start() ran */
static uint32_t ticks_total   = 0;

void scheduler_init(void) {
    current_pid  = PROCESS_NONE;
    quantum_ticks = SCHED_DEFAULT_QUANTUM;
    quantum_left  = SCHED_DEFAULT_QUANTUM;
    ticks_total   = 0;
    started        = 0;
}

int scheduler_current_pid(void) {
    return current_pid;
}

uint32_t scheduler_ticks(void) {
    return ticks_total;
}

void scheduler_set_quantum(int ticks) {
    if (ticks < SCHED_MIN_QUANTUM) {
        ticks = SCHED_MIN_QUANTUM;
    }
    if (ticks > SCHED_MAX_QUANTUM) {
        ticks = SCHED_MAX_QUANTUM;
    }
    quantum_ticks = ticks;
    quantum_left  = ticks;
}

int scheduler_get_quantum(void) {
    return quantum_ticks;
}

/* ------------------------------------------------------------------ */
/* Choosing the next process                                           */
/* ------------------------------------------------------------------ */

/*
 * pick_next_ready - return the pid of the READY process that should
 * run next, or PROCESS_NONE if no process is READY.
 *
 * Effective score = priority + wait_ticks / SCHED_AGING_STEP.
 * Scanning starts after the current pid, so processes with the same
 * score are chosen in round-robin order.
 */
static int pick_next_ready(void) {
    int best = PROCESS_NONE;
    int best_score = -1;
    int start = (current_pid == PROCESS_NONE) ? 0 : current_pid + 1;
    int k;

    for (k = 0; k < PROCESS_MAX; k++) {
        int i = (start + k) % PROCESS_MAX;
        struct process* p = process_get(i);
        int score;

        if (p == NULL || p->state != PROCESS_READY) {
            continue;
        }
        score = p->priority + p->wait_ticks / SCHED_AGING_STEP;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

/* Every READY process has waited one tick longer (aging). */
static void age_ready_processes(void) {
    int i;
    for (i = 0; i < PROCESS_MAX; i++) {
        struct process* p = process_get(i);
        if (p != NULL && p->state == PROCESS_READY) {
            p->wait_ticks++;
        }
    }
}

/*
 * cleanup_terminated - release the stack and the table entry of every
 * TERMINATED process, EXCEPT keep_pid.
 *
 * keep_pid is the process whose stack we are standing on right now
 * (its interrupt frame is on that stack!).  Its stack is freed by the
 * next call, once another process owns the CPU.
 */
static void cleanup_terminated(int keep_pid) {
    int i;
    for (i = 0; i < PROCESS_MAX; i++) {
        struct process* p = process_get(i);
        if (p != NULL && p->state == PROCESS_TERMINATED && i != keep_pid) {
            free_stack(p->stack_base);
            p->stack_base  = NULL;
            p->entry       = 0;
            p->has_message = 0;
            p->state       = PROCESS_TERMINATED;
            p->in_use      = 0;      /* slot can be reused */
        }
    }
}

/* ------------------------------------------------------------------ */
/* The scheduling decision itself                                      */
/* ------------------------------------------------------------------ */

/*
 * scheduler_tick - called once per timer tick from timer_isr.
 *
 * frame      address of the saved register frame of the process that
 *            was just interrupted (on THAT process's stack)
 * returns    stack pointer the CPU shall continue with
 *
 * The caller loads it into ESP and executes popa/iret, so returning
 * "frame" means "keep running the same process".
 */
uint32_t scheduler_tick(uint32_t* frame) {
    int old;
    int runnable = 0;
    int next;

    timer_acknowledge();
    ticks_total++;

    /* While kmain is still booting we only count ticks. */
    if (started == 0) {
        return (uint32_t)frame;
    }

    old = current_pid;

    /*
     * Case 1: a real process was interrupted.  Its complete state is
     * the frame on its stack, so remember the stack pointer as long
     * as the process can still run some day (anything but
     * TERMINATED - even a BLOCKED process must be able to resume
     * after it was woken up).
     */
    if (old != PROCESS_NONE) {
        struct process* p = process_get(old);
        if (p != NULL && p->state != PROCESS_TERMINATED) {
            p->saved_esp = (uint32_t)frame;
        }
        if (p != NULL && p->state == PROCESS_CURRENT) {
            runnable = 1;

            quantum_left--;
            if (quantum_left > 0) {
                /* Quantum not over yet: same process continues. */
                age_ready_processes();
                cleanup_terminated(old);
                return (uint32_t)frame;
            }
        }
    }

    /*
     * Scheduling point: either the quantum just expired, or the
     * current process terminated, or this is the very first switch
     * out of kmain.
     */
    age_ready_processes();
    cleanup_terminated(old);

    next = pick_next_ready();

    if (next == PROCESS_NONE) {
        if (runnable) {
            /* Nobody else wants the CPU: keep the current process. */
            quantum_left = quantum_ticks;
            return (uint32_t)frame;
        }
        if (old == PROCESS_NONE) {
            /* Boot stack, no process chosen yet: keep kmain alive. */
            return (uint32_t)frame;
        }
        /* The current process is dead and nothing is READY.
           With a live null process this cannot happen. */
        serial_puts("scheduler: no runnable process - halting\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    /* Switch: old process goes back to READY (if it is still alive),
       the chosen one becomes CURRENT. */
    if (runnable) {
        process_set_state(old, PROCESS_READY);
    }
    process_set_state(next, PROCESS_CURRENT);
    current_pid = next;
    process_get(next)->wait_ticks = 0;
    quantum_left = quantum_ticks;

    return process_get(next)->saved_esp;
}

/* ------------------------------------------------------------------ */
/* Starting and yielding                                               */
/* ------------------------------------------------------------------ */

/*
 * scheduler_start - hand the CPU to the first created process.
 *
 * We trigger the exact same interrupt path a real timer tick uses
 * (vector 32) instead of writing a second switching routine.  kmain's
 * stack is simply left behind; the scheduler will never come back
 * here because pid 0 (the null process) is never terminated.
 */
void scheduler_start(void) {
    started = 1;
    __asm__ volatile ("int $0x20");

    /* Only reached if there was no process to run. */
    serial_puts("scheduler_start: no process to run\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/*
 * scheduler_yield - voluntarily give the CPU back before the quantum
 * ends.  Same mechanism: software interrupt 32 runs scheduler_tick,
 * which either switches away or (if no other process is READY)
 * returns right here.
 */
void scheduler_yield(void) {
    __asm__ volatile ("int $0x20");
}
