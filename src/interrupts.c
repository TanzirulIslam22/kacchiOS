/* interrupts.c - IDT, 8259 PIC and the periodic timer (PIT)
 *
 * The starter kacchiOS never enabled interrupts.  A scheduler with a
 * time quantum needs exactly one interrupt: the timer.  This file
 * builds the interrupt table, remaps the two 8259 PIC chips so that
 * hardware interrupts use vectors 32..47, programs the PIT to raise
 * the timer 100 times per second, and finally enables interrupts.
 */
#include "interrupts.h"
#include "serial.h"
#include "io.h"

/* Hardware ports. */
#define PIC_MASTER_CMD    0x20
#define PIC_MASTER_DATA   0x21
#define PIC_SLAVE_CMD     0xA0
#define PIC_SLAVE_DATA    0xA1
#define PIT_COMMAND       0x43
#define PIT_CHANNEL0_DATA 0x40
#define PIT_INPUT_HZ      1193182   /* the PIT crystal frequency      */
#define TIMER_HZ          100       /* our chosen timer interrupt rate */

#define IDT_SIZE       256
#define TIMER_VECTOR   32          /* IRQ0 after remapping the PIC   */

/* Stubs written in isr.s */
void timer_isr(void);
void unexpected_isr(void);

/*
 * One entry of the Interrupt Descriptor Table.  The CPU expects this
 * exact byte layout: a 32-bit handler address split into two halves,
 * the code segment, and some flag bytes.  __attribute__((packed)) is
 * the gcc way of saying "do not insert padding bytes".
 */
struct idt_entry {
    uint16_t offset_lo;   /* bits  0..15 of the handler address */
    uint16_t selector;    /* code segment (same one we run on)  */
    uint8_t  zero;        /* must be 0                          */
    uint8_t  flags;       /* 0x8E = present, ring 0, 32-bit     */
    uint16_t offset_hi;   /* bits 16..31 of the handler address */
} __attribute__((packed));

/* What the lidt instruction loads: where the table is and how long. */
struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_SIZE];

/* The code segment the kernel is actually running on (set by the
   boot loader).  Every IDT entry and every synthetic interrupt frame
   must use this value. */
uint16_t kernel_code_selector(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

/* Point IDT entry n at handler. */
static void set_gate(int n, void (*handler)(void)) {
    uint32_t address = (uint32_t)handler;
    idt[n].offset_lo = (uint16_t)(address & 0xFFFF);
    idt[n].selector  = kernel_code_selector();
    idt[n].zero      = 0;
    idt[n].flags     = 0x8E;          /* present, ring 0, 32-bit interrupt gate */
    idt[n].offset_hi = (uint16_t)((address >> 16) & 0xFFFF);
}

/*
 * The PIC controllers deliver hardware interrupts on vectors 0..15 by
 * default, which collide with CPU exceptions (divide error, ...).
 * This is the standard "remap" sequence from every OS textbook: tell
 * the chips to use vectors 32..47 instead.  After that, IRQ0 (the
 * timer) arrives as interrupt vector 32.
 */
static void pic_remap(void) {
    outb(PIC_MASTER_CMD, 0x11);   /* begin remapping, ICW1 */
    outb(PIC_SLAVE_CMD,  0x11);
    outb(PIC_MASTER_DATA, 0x20);  /* master vector base = 32  */
    outb(PIC_SLAVE_DATA,  0x28);  /* slave  vector base = 40  */
    outb(PIC_MASTER_DATA, 0x04);  /* master has a slave on IRQ2 */
    outb(PIC_SLAVE_DATA,  0x02);  /* slave is cascade 2        */
    outb(PIC_MASTER_DATA, 0x01);  /* 8086 mode, ICW4           */
    outb(PIC_SLAVE_DATA,  0x01);

    /* Mask (disable) every IRQ except IRQ0 = the timer.  That way no
       other device can fire an interrupt we have no handler for. */
    outb(PIC_MASTER_DATA, 0xFE);
    outb(PIC_SLAVE_DATA,  0xFF);
}

/* Program the PIT channel 0 to tick every 1/TIMER_HZ seconds. */
static void pit_init(void) {
    uint16_t divisor = (uint16_t)(PIT_INPUT_HZ / TIMER_HZ);  /* 11932 */
    outb(PIT_COMMAND, 0x36);            /* channel 0, lo/hi bytes, mode 3 */
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}

void interrupts_init(void) {
    struct idt_pointer pointer;
    int i;

    /* Every vector starts out "unexpected"; the timer gets the real
       handler below. */
    for (i = 0; i < IDT_SIZE; i++) {
        set_gate(i, unexpected_isr);
    }
    set_gate(TIMER_VECTOR, timer_isr);

    pointer.limit = (uint16_t)(sizeof(idt) - 1);
    pointer.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(pointer));

    pic_remap();
    pit_init();

    /* Interrupts are on from now on: the timer can preempt the kernel
       at any instruction from here on. */
    __asm__ volatile ("sti");
}

/* Tell the PIC "we finished this interrupt" so the next one can come. */
void timer_acknowledge(void) {
    outb(PIC_MASTER_CMD, 0x20);
}

/* Called from isr.s if any interrupt other than the timer arrives. */
void unexpected_interrupt(void) {
    serial_puts("\nUnexpected interrupt - kernel halted!\n");
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
