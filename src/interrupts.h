/* interrupts.h - Interrupt control and timer interrupt interface */
#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "types.h"

/*
 * A "critical section" is a short piece of code that must not be cut in
 * half by a timer interrupt.  interrupt_save() turns interrupts off and
 * remembers whether they were on; interrupt_restore() puts them back
 * exactly as they were.  We save the old flags instead of simply calling
 * sti, because these helpers are also used inside the interrupt handler
 * itself, where re-enabling interrupts would be dangerous.
 */
static inline uint32_t interrupt_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void interrupt_restore(uint32_t flags) {
    __asm__ volatile ("push %0; popf" : : "r"(flags) : "memory");
}

/* Set up IDT, 8259 PIC and the periodic timer.  Implemented in interrupts.c */
void interrupts_init(void);

/* Tell the PIC that the timer interrupt has been handled (send EOI). */
void timer_acknowledge(void);

/* Code segment the kernel runs on (set by the boot loader).  Needed
   whenever we build an interrupt frame by hand, e.g. for a new
   process. */
uint16_t kernel_code_selector(void);

#endif
