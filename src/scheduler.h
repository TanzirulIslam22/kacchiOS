/* scheduler.h - Round-robin scheduler with aging */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"

/*
 * Scheduling policy (also described in README.md):
 *
 *   - The CPU runs one process at a time for "quantum" timer ticks,
 *     then looks for another process to run.
 *   - Every READY process gets wait_ticks++ on every timer tick.
 *   - The next process is the READY process with the highest
 *         priority + wait_ticks / SCHED_AGING_STEP
 *     Ties are broken in round-robin order (start scanning after the
 *     process that ran last).
 *   - With equal priorities this is plain round-robin.  A process with
 *     a low priority eventually accumulates enough wait_ticks to win
 *     anyway - that is the aging that prevents starvation.
 */

#define SCHED_DEFAULT_QUANTUM 5   /* ticks; 5 ticks = 50 ms at 100 Hz */
#define SCHED_AGING_STEP      10  /* ticks of waiting = +1 priority    */
#define SCHED_MIN_QUANTUM     1
#define SCHED_MAX_QUANTUM     100

/* One time setup at boot (before interrupts_init()). */
void scheduler_init(void);

/* Switch to the first created process.  Never returns. */
void scheduler_start(void);

/* Give the CPU away voluntarily before the quantum ends. */
void scheduler_yield(void);

/*
 * Called from timer_isr (isr.s) on every timer tick.
 *   frame = stack pointer of the interrupted process
 *   return value = stack pointer the CPU should continue with
 * The caller loads that value into ESP, pops the registers and irets.
 */
uint32_t scheduler_tick(uint32_t* frame);

/* pid of the process that currently owns the CPU (PROCESS_NONE if the
   kernel itself is running, i.e. before the first switch). */
int scheduler_current_pid(void);

/* Configurable time quantum (in timer ticks). */
void scheduler_set_quantum(int ticks);
int  scheduler_get_quantum(void);

/* Number of timer ticks since boot. */
uint32_t scheduler_ticks(void);

#endif
