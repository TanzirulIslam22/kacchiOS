/* process.c - Process manager (create, state transitions, terminate, IPC)
 *
 * Everything about a process lives in one entry of process_table[].
 * The pid of a process is simply the index of its entry, exactly like
 * Xinu's proctab[] array from the lecture slides.
 */
#include "process.h"
#include "memory.h"
#include "scheduler.h"
#include "interrupts.h"
#include "serial.h"
#include "string.h"

static struct process process_table[PROCESS_MAX];

/* ------------------------------------------------------------------ */
/* Setup                                                               */
/* ------------------------------------------------------------------ */

void process_table_init(void) {
    int i;
    for (i = 0; i < PROCESS_MAX; i++) {
        process_table[i].in_use   = 0;
        process_table[i].pid      = i;
        process_table[i].state    = PROCESS_TERMINATED;
        process_table[i].stack_base = NULL;
        process_table[i].saved_esp  = 0;
        process_table[i].priority   = 0;
        process_table[i].wait_ticks = 0;
        process_table[i].has_message = 0;
        process_table[i].name[0]  = '\0';
        process_table[i].entry    = 0;
    }
}

/* Copy a short name into a fixed size field, always NUL terminated. */
static void copy_name(char* dest, const char* src) {
    int i;
    for (i = 0; i < PROCESS_NAME_MAX - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

/* Forward declaration: the entry point of every brand new process,
   entered by iret, see build_initial_stack() below. */
void process_trampoline(void);

/*
 * Build the stack image of a process that has never run before.
 *
 * When the scheduler later loads saved_esp and executes popa + iret,
 * the CPU will pop this fake frame:
 *
 *      high address   EFLAGS   (0x202 = interrupts enabled)
 *                     CS       (our code segment)
 *      saved_esp ->   EIP      (process_trampoline)
 *                     8 dummy registers for popa
 *      low address
 *
 * The very first run of every process therefore "returns" into
 * process_trampoline, which then simply calls the process's entry
 * function.
 */
static void build_initial_stack(struct process* p) {
    uint32_t* top = (uint32_t*)((uint32_t)(p->stack_base + p->stack_size)
                                & ~(uint32_t)15);
    int i;

    *--top = 0x202;                       /* EFLAGS: IF bit set        */
    *--top = kernel_code_selector();      /* CS                        */
    *--top = (uint32_t)process_trampoline; /* EIP: where to start      */
    for (i = 0; i < 8; i++) {             /* dummy area popped by popa */
        *--top = 0;
    }
    p->saved_esp = (uint32_t)top;
}

/*
 * process_create - allocate a table entry and a stack, build the
 * initial context.
 *
 * Returns the new pid, or -1 when the process table is full or no
 * stack is available.  The entry becomes visible (in_use/state) only
 * as the very last step, so a timer interrupt arriving in the middle
 * of creation simply ignores the half-built entry.
 */
int process_create(const char* name, void (*entry)(void),
                   uint32_t stack_size, int priority) {
    uint32_t flags;
    struct process* p;
    int i;

    flags = interrupt_save();

    /* 1. find a free entry in the process table */
    for (i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].in_use == 0) {
            break;
        }
    }
    if (i == PROCESS_MAX) {
        serial_puts("process_create: process table is full\n");
        interrupt_restore(flags);
        return -1;
    }

    /* 2. get a stack from the memory manager */
    {
        uint8_t* stack = allocate_stack(stack_size);
        if (stack == NULL) {
            serial_puts("process_create: no free stack slot\n");
            interrupt_restore(flags);
            return -1;
        }
        process_table[i].stack_base = stack;
    }

    /* 3. fill in the entry (still invisible: in_use == 0) */
    p = &process_table[i];
    p->pid        = i;
    p->entry      = entry;
    p->stack_size = STACK_SLOT_SIZE;
    p->priority   = priority;
    p->wait_ticks = 0;
    p->has_message = 0;
    p->mailbox[0] = '\0';
    copy_name(p->name, name);
    build_initial_stack(p);

    /* 4. make the process visible to the scheduler */
    p->state  = PROCESS_READY;
    p->in_use = 1;

    interrupt_restore(flags);
    return i;
}

/* ------------------------------------------------------------------ */
/* Lookups and state transitions                                       */
/* ------------------------------------------------------------------ */

struct process* process_get(int pid) {
    if (pid < 0 || pid >= PROCESS_MAX) {
        return NULL;
    }
    if (process_table[pid].in_use == 0) {
        return NULL;
    }
    return &process_table[pid];
}

/*
 * process_set_state - the single place where states change.
 *
 * Allowed transitions:
 *     READY    -> CURRENT   scheduler selected this process
 *     CURRENT  -> READY     scheduler rotated away from it
 *     CURRENT  -> BLOCKED   the process is waiting for a message
 *     BLOCKED  -> READY     ipc_send() delivered a message
 *     READY/BLOCKED/CURRENT -> TERMINATED  finished or killed
 *
 * Everything else (for example TERMINATED -> READY) is refused, so a
 * finished process can never accidentally run again.
 */
int process_set_state(int pid, int state) {
    struct process* p = process_get(pid);

    if (p == NULL) {
        return -1;
    }
    if (p->state == state) {
        return 0;
    }

    switch (state) {
    case PROCESS_CURRENT:
        if (p->state != PROCESS_READY) return -1;
        break;
    case PROCESS_READY:
        if (p->state != PROCESS_CURRENT &&
            p->state != PROCESS_BLOCKED) return -1;
        break;
    case PROCESS_BLOCKED:
        if (p->state != PROCESS_CURRENT) return -1;
        break;
    case PROCESS_TERMINATED:
        if (p->state == PROCESS_TERMINATED) return -1;
        break;
    default:
        return -1;
    }

    p->state = state;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Termination                                                         */
/* ------------------------------------------------------------------ */

/*
 * process_exit - called when a process's entry function returns.
 *
 * Marks the process TERMINATED and hands the CPU away with a yield.
 * The scheduler will never select a TERMINATED process again, so this
 * function really does not come back.  The stack is NOT freed here:
 * we are still running on it.  scheduler_tick() frees it a moment
 * later, once another process owns the CPU.
 */
void process_exit(void) {
    int pid = process_current_pid();

    if (process_get(pid) != NULL) {
        process_set_state(pid, PROCESS_TERMINATED);
    }

    scheduler_yield();

    /* Should be unreachable.  Park forever instead of corrupting
       memory if the invariant above is ever broken. */
    serial_puts("process_exit: returned after yield, halting\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/*
 * process_terminate - kill some other process from the outside.
 *
 * Refuses the null process (pid 0), because it owns the shell and the
 * system would become unusable without it.  Killing yourself simply
 * goes through process_exit().
 */
int process_terminate(int pid) {
    uint32_t flags;
    int result = 0;

    flags = interrupt_save();

    if (process_get(pid) == NULL) {
        interrupt_restore(flags);
        return -1;
    }
    if (pid == 0) {
        serial_puts("kill: the null process cannot be terminated\n");
        interrupt_restore(flags);
        return -1;
    }
    if (process_get(pid)->state == PROCESS_TERMINATED) {
        interrupt_restore(flags);
        return -1;
    }

    if (pid == process_current_pid()) {
        interrupt_restore(flags);
        process_exit();                 /* never returns */
        return 0;
    }

    result = process_set_state(pid, PROCESS_TERMINATED);
    interrupt_restore(flags);
    return result;
}

/* ------------------------------------------------------------------ */
/* Utility functions                                                   */
/* ------------------------------------------------------------------ */

int process_current_pid(void) {
    return scheduler_current_pid();
}

int process_get_state(int pid) {
    struct process* p = process_get(pid);
    if (p == NULL) {
        return -1;
    }
    return p->state;
}

const char* process_get_name(int pid) {
    struct process* p = process_get(pid);
    if (p == NULL) {
        return NULL;
    }
    return p->name;
}

uint32_t process_get_stack_size(int pid) {
    struct process* p = process_get(pid);
    if (p == NULL) {
        return 0;
    }
    return p->stack_size;
}

int process_count(void) {
    uint32_t flags = interrupt_save();
    int i, count = 0;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].in_use) {
            count++;
        }
    }
    interrupt_restore(flags);
    return count;
}

/* ------------------------------------------------------------------ */
/* First run of a process                                              */
/* ------------------------------------------------------------------ */

/*
 * process_trampoline - the EIP of every freshly created process.
 *
 * iret "returns" into this function, so from here it looks like a
 * normal function call with no caller.  It simply starts the process
 * function; if that function ever returns, the process is finished.
 */
void process_trampoline(void) {
    struct process* p = process_get(process_current_pid());

    if (p != NULL && p->entry != NULL) {
        p->entry();
    }
    process_exit();
}

/* ------------------------------------------------------------------ */
/* Simple message passing (IPC)                                        */
/* ------------------------------------------------------------------ */

/*
 * Each process has a mailbox that can hold ONE message.  This is the
 * smallest useful IPC: the shell can send a command string to a
 * worker process, the worker reads it with ipc_receive().
 * If the mailbox is still full, ipc_send() fails instead of silently
 * overwriting the old message.
 */
int ipc_send(int to_pid, const char* msg) {
    uint32_t flags;
    struct process* p;
    int i;

    flags = interrupt_save();

    p = process_get(to_pid);
    if (p == NULL || msg == NULL) {
        interrupt_restore(flags);
        return -1;
    }
    if (p->has_message) {
        serial_puts("ipc_send: receiver mailbox is full\n");
        interrupt_restore(flags);
        return -1;
    }

    for (i = 0; i < PROCESS_IPC_MSG_MAX - 1 && msg[i] != '\0'; i++) {
        p->mailbox[i] = msg[i];
    }
    p->mailbox[i] = '\0';
    p->has_message = 1;

    /* Wake a receiver that is sleeping in ipc_receive_wait(). */
    if (p->state == PROCESS_BLOCKED) {
        process_set_state(to_pid, PROCESS_READY);
    }

    interrupt_restore(flags);
    return 0;
}

int ipc_receive(char* buf, uint32_t bufsize) {
    uint32_t flags;
    struct process* p;
    int i;

    if (buf == NULL || bufsize == 0) {
        return 0;
    }

    flags = interrupt_save();

    p = process_get(process_current_pid());
    if (p == NULL || p->has_message == 0) {
        interrupt_restore(flags);
        return 0;                       /* nothing to read */
    }

    for (i = 0; i < PROCESS_IPC_MSG_MAX - 1 && p->mailbox[i] != '\0'
                && (uint32_t)(i + 1) < bufsize; i++) {
        buf[i] = p->mailbox[i];
    }
    buf[i] = '\0';
    p->has_message = 0;

    interrupt_restore(flags);
    return 1;
}

/*
 * ipc_receive_wait - blocking receive (the Xinu receive() idea).
 *
 * If the mailbox is empty the process moves itself to BLOCKED and
 * gives the CPU away.  The scheduler ignores BLOCKED processes, so it
 * costs no CPU time while waiting.  As soon as somebody calls
 * ipc_send() to this process, it becomes READY again and this loop
 * takes the message.
 *
 * The check and the state change happen with interrupts off, so no
 * message can arrive in between and be lost.
 */
int ipc_receive_wait(char* buf, uint32_t bufsize) {
    int pid = process_current_pid();
    struct process* p;

    if (buf == NULL || bufsize == 0) {
        return 0;
    }

    for (;;) {
        uint32_t flags = interrupt_save();

        p = process_get(pid);
        if (p == NULL) {
            interrupt_restore(flags);
            return 0;
        }

        if (p->has_message) {
            int i;
            for (i = 0; i < PROCESS_IPC_MSG_MAX - 1 && p->mailbox[i] != '\0'
                        && (uint32_t)(i + 1) < bufsize; i++) {
                buf[i] = p->mailbox[i];
            }
            buf[i] = '\0';
            p->has_message = 0;
            interrupt_restore(flags);
            return 1;
        }

        /* No message yet: park here until ipc_send() wakes us. */
        process_set_state(pid, PROCESS_BLOCKED);
        interrupt_restore(flags);
        scheduler_yield();
    }
}
