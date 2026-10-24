/* kernel.c - Boot sequence, null process (shell) and demo processes
 *
 * Boot flow:
 *   boot.S  -> sets up a stack, clears bss, calls kmain()
 *   kmain() -> serial_init, init the three components, create the
 *              null process (pid 0), then scheduler_start()
 *   scheduler_start() switches into the null process and kmain never
 *   returns.  From then on the timer interrupt decides who runs.
 */
#include "types.h"
#include "serial.h"
#include "string.h"
#include "memory.h"
#include "process.h"
#include "scheduler.h"
#include "interrupts.h"

#define MAX_INPUT 128

/* ---------- tiny number printing helpers (no libc here) ---------- */

static void print_u32_dec(uint32_t value) {
    char buf[12];
    int i = 0;
    if (value == 0) {
        serial_putc('0');
        return;
    }
    while (value > 0) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

static void print_u32_hex(uint32_t value) {
    char buf[9];
    const char* digits = "0123456789abcdef";
    int i;
    serial_puts("0x");
    for (i = 7; i >= 0; i--) {
        buf[i] = digits[value & 0xF];
        value >>= 4;
    }
    buf[8] = '\0';
    serial_puts(buf);
}

/* Parse a decimal number.  Returns 1 on success, 0 if the text is not
   a number (empty string, signs, letters, ...). */
static int parse_uint(const char* text, uint32_t* out) {
    uint32_t value = 0;
    int i = 0;
    if (text == NULL || text[0] < '0' || text[0] > '9') {
        return 0;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (uint32_t)(text[i] - '0');
        i++;
    }
    if (text[i] != '\0') {
        return 0;
    }
    *out = value;
    return 1;
}

/* Parse a leading decimal number.  Returns the number of digits that
   were consumed (0 = there is no number at the start). */
static int parse_leading_uint(const char* text, uint32_t* out) {
    uint32_t value = 0;
    int i = 0;
    if (text == NULL) {
        return 0;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (uint32_t)(text[i] - '0');
        i++;
    }
    if (i > 0) {
        *out = value;
    }
    return i;
}

static const char* state_name(int state) {
    switch (state) {
    case PROCESS_CURRENT:    return "CURRENT";
    case PROCESS_READY:      return "READY";
    case PROCESS_TERMINATED: return "TERMINATED";
    case PROCESS_BLOCKED:    return "BLOCKED";
    default:                 return "?";
    }
}

/* Print a string padded with spaces up to a fixed column width. */
static void print_padded_str(const char* text, int width) {
    int len = (int)strlen(text);
    serial_puts(text);
    while (len < width) {
        serial_putc(' ');
        len++;
    }
}

/* Same for a decimal number. */
static void print_padded_dec(uint32_t value, int width) {
    char buf[12];
    int i = 0;
    int len;
    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            buf[i++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    len = i;
    while (len < width) {
        serial_putc(' ');
        len++;
    }
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

/* ================================================================== */
/* Demo worker process (created by the shell command "spawn")          */
/* ================================================================== */

/*
 * worker_process - a second process to demonstrate preemption and
 * IPC.  It never finishes on its own.  Every 200 timer ticks (2
 * seconds) it prints a heartbeat line: if you can see those lines
 * appearing between shell prompts, the scheduler really is switching
 * between processes.  It also prints whatever the shell sends it.
 */
static void worker_process(void) {
    char msg[PROCESS_IPC_MSG_MAX];
    uint32_t last_beat = scheduler_ticks();

    while (1) {
        if (ipc_receive(msg, sizeof(msg))) {
            serial_puts("[worker ");
            print_u32_dec((uint32_t)process_current_pid());
            serial_puts("] got message: ");
            serial_puts(msg);
            serial_puts("\n");
        }
        if (scheduler_ticks() - last_beat >= 200) {
            last_beat = scheduler_ticks();
            serial_puts("[worker ");
            print_u32_dec((uint32_t)process_current_pid());
            serial_puts("] alive at tick ");
            print_u32_dec(scheduler_ticks());
            serial_puts("\n");
        }
        {
            volatile uint32_t spin;
            for (spin = 0; spin < 100000; spin++) {
                /* burn some CPU time */
            }
        }
    }
}

/*
 * server_process - demonstrates the BLOCKED state.  It sleeps inside
 * ipc_receive_wait() until another process sends it a message; ps
 * shows it as BLOCKED while it waits, and it prints the message as
 * soon as it is woken up.  Blocked processes use no CPU at all.
 */
static void server_process(void) {
    char msg[PROCESS_IPC_MSG_MAX];

    while (1) {
        if (ipc_receive_wait(msg, sizeof(msg))) {
            serial_puts("[server] received: ");
            serial_puts(msg);
            serial_puts("\n");
        }
    }
}

/* ================================================================== */
/* Shell commands                                                      */
/* ================================================================== */

static void cmd_help(void) {
    serial_puts("commands:\n");
    serial_puts("  help            this list\n");
    serial_puts("  ps              show the process table\n");
    serial_puts("  spawn           create a worker process\n");
    serial_puts("  server          create a server (blocks until messaged)\n");
    serial_puts("  kill <pid>      terminate a process\n");
    serial_puts("  quantum <ticks> set the time quantum (100 ticks = 1s)\n");
    serial_puts("  send <pid> <msg> send a message to a process\n");
    serial_puts("  inbox           read the shell's mailbox\n");
    serial_puts("  busy <ticks>    burn CPU (demonstrates aging)\n");
    serial_puts("  mem             memory manager statistics\n");
    serial_puts("  memtest         run the memory manager self test\n");
}

static void cmd_ps(void) {
    int i;

    serial_puts(" pid  state        prio  wait  name\n");
    serial_puts(" ---  -----------  ----  ----  ----------------\n");
    for (i = 0; i < PROCESS_MAX; i++) {
        struct process* p = process_get(i);
        print_padded_dec((uint32_t)i, 3);
        serial_puts("  ");
        if (p == NULL) {
            serial_puts("(free slot)\n");
            continue;
        }
        print_padded_str(state_name(p->state), 11);
        serial_puts("  ");
        print_padded_dec((uint32_t)p->priority, 4);
        serial_puts("  ");
        print_padded_dec((uint32_t)p->wait_ticks, 4);
        serial_puts("  ");
        serial_puts(p->name);
        serial_puts("\n");
    }
    serial_puts("processes alive: ");
    print_u32_dec((uint32_t)process_count());
    serial_puts(", ticks since boot: ");
    print_u32_dec(scheduler_ticks());
    serial_puts(", quantum: ");
    print_u32_dec((uint32_t)scheduler_get_quantum());
    serial_puts(" ticks\n");
}

static void cmd_spawn(void) {
    int pid = process_create("worker", worker_process, STACK_SLOT_SIZE, 1);
    if (pid < 0) {
        serial_puts("spawn failed\n");
    } else {
        serial_puts("created worker with pid ");
        print_u32_dec((uint32_t)pid);
        serial_puts("\n");
    }
}

static void cmd_server(void) {
    int pid = process_create("server", server_process, STACK_SLOT_SIZE, 1);
    if (pid < 0) {
        serial_puts("spawn failed\n");
    } else {
        serial_puts("created server with pid ");
        print_u32_dec((uint32_t)pid);
        serial_puts(" (try: send ");
        print_u32_dec((uint32_t)pid);
        serial_puts(" hello)\n");
    }
}

static void cmd_kill(const char* args) {
    uint32_t pid;
    if (!parse_uint(args, &pid)) {
        serial_puts("usage: kill <pid>\n");
        return;
    }
    if (process_terminate((int)pid) == 0) {
        serial_puts("process terminated\n");
    } else {
        serial_puts("kill failed (invalid pid?)\n");
    }
}

static void cmd_quantum(const char* args) {
    uint32_t ticks;
    if (!parse_uint(args, &ticks)) {
        serial_puts("usage: quantum <ticks>\n");
        serial_puts("current quantum: ");
        print_u32_dec((uint32_t)scheduler_get_quantum());
        serial_puts(" ticks\n");
        return;
    }
    scheduler_set_quantum((int)ticks);
    serial_puts("quantum set to ");
    print_u32_dec((uint32_t)scheduler_get_quantum());
    serial_puts(" ticks (100 ticks = 1 second)\n");
}

static void cmd_send(const char* args) {
    uint32_t pid = 0;
    const char* msg;
    int used;

    /* "<pid> <message>": parse the number, then everything behind the
       first space is the message (spaces allowed inside). */
    used = parse_leading_uint(args, &pid);
    if (used == 0 || args[used] != ' ') {
        serial_puts("usage: send <pid> <message>\n");
        return;
    }
    msg = args + used;
    while (*msg == ' ') msg++;
    if (*msg == '\0') {
        serial_puts("usage: send <pid> <message>\n");
        return;
    }

    if (ipc_send((int)pid, msg) == 0) {
        serial_puts("message sent\n");
    } else {
        serial_puts("send failed (bad pid or full mailbox)\n");
    }
}

static void cmd_inbox(void) {
    char msg[PROCESS_IPC_MSG_MAX];
    if (ipc_receive(msg, sizeof(msg))) {
        serial_puts("inbox: ");
        serial_puts(msg);
        serial_puts("\n");
    } else {
        serial_puts("inbox: no message\n");
    }
}

static void cmd_busy(const char* args) {
    uint32_t ticks = 0;
    uint32_t start;

    if (!parse_uint(args, &ticks)) {
        serial_puts("usage: busy <ticks>\n");
        return;
    }
    serial_puts("busy: spinning (workers must still run - aging)...\n");
    start = scheduler_ticks();
    while (scheduler_ticks() - start < ticks) {
        /* Spin without blocking or yielding.  The shell has the higher
           priority, so without aging the workers would starve during
           this loop.  With aging their wait_ticks grow until they win. */
    }
    serial_puts("busy: done\n");
}

static void cmd_mem(void) {
    serial_puts("heap free bytes  = ");
    print_u32_dec(heap_free_bytes());
    serial_puts("\nheap used bytes  = ");
    print_u32_dec(heap_used_bytes());
    serial_puts("\nfree stack slots = ");
    print_u32_dec(stack_free_slots());
    serial_puts("\n");
}

/* Memory manager self test: allocation, reuse, double free, invalid
   free, out of memory, pool exhaustion. */
static void run_memory_test(void) {
    uint32_t initial_free;
    void* slot1;
    void* slot2;
    void* slot3;
    void* a;
    void* b;
    void* c;
    void* d;
    void* huge;

    serial_puts("--- memory test start ---\n");
    initial_free = heap_free_bytes();
    serial_puts("heap free bytes = ");
    print_u32_dec(initial_free);
    serial_puts("\nfree stack slots = ");
    print_u32_dec(stack_free_slots());
    serial_puts("\n");

    /* 1. stack allocation */
    slot1 = allocate_stack(STACK_SLOT_SIZE);
    slot2 = allocate_stack(STACK_SLOT_SIZE);
    serial_puts("stack1 = ");
    print_u32_hex((uint32_t)slot1);
    serial_puts("  stack2 = ");
    print_u32_hex((uint32_t)slot2);
    serial_puts("\n");
    if (slot1 == NULL || slot2 == NULL) {
        serial_puts("stack alloc: FAILED (no free slots?)\n");
        serial_puts("--- memory test end ---\n");
        return;
    }

    /* 2. stack free + reuse of the same slot */
    free_stack(slot1);
    slot3 = allocate_stack(STACK_SLOT_SIZE);
    serial_puts((slot3 != NULL && slot3 == slot1)
                ? "stack reuse: OK\n" : "stack reuse: FAILED\n");

    /* 3. heap allocation, free in the middle, double free, reuse */
    a = allocate_heap(100);
    b = allocate_heap(200);
    c = allocate_heap(300);
    free_heap(b);
    serial_puts("next line should report a double free:\n");
    free_heap(b);
    serial_puts("double free was ignored: OK\n");
    d = allocate_heap(50);
    serial_puts(d == b ? "heap reuse: OK\n" : "heap reuse: FAILED\n");

    /* 4. free everything: coalescing must restore the initial state */
    free_heap(a);
    free_heap(c);
    free_heap(d);
    serial_puts((heap_free_bytes() == initial_free && heap_used_bytes() == 0)
                ? "coalesce after free: OK\n"
                : "coalesce after free: FAILED\n");

    /* 5. allocation failure */
    huge = allocate_heap(HEAP_TOTAL_SIZE + 1);
    serial_puts(huge == NULL ? "out of memory returns NULL: OK\n"
                             : "out of memory returns NULL: FAILED\n");

    /* 6. invalid free (pointer in the middle of a valid block) */
    a = allocate_heap(64);
    serial_puts("next line should report an invalid pointer:\n");
    free_heap((void*)((uint8_t*)a + 8));
    serial_puts("invalid free was ignored: OK\n");
    free_heap(a);

    /* 7. stack pool exhaustion */
    {
        void* held[STACK_POOL_SLOTS];
        int count = 0;
        while (count < STACK_POOL_SLOTS) {
            void* s = allocate_stack(STACK_SLOT_SIZE);
            if (s == NULL) break;
            held[count++] = s;
        }
        serial_puts("stack slots allocated until failure = ");
        print_u32_dec((uint32_t)count);
        serial_puts("\n");
        free_stack(slot2);
        free_stack(slot3);
        while (count > 0) {
            count--;
            free_stack(held[count]);
        }
        serial_puts("free stack slots after test = ");
        print_u32_dec(stack_free_slots());
        serial_puts("\n");
    }

    serial_puts("--- memory test end ---\n");
}

/* ================================================================== */
/* The null process (interactive shell)                                */
/* ================================================================== */

/*
 * null_process - process pid 0.  It always exists, it never
 * terminates (the shell refuses "kill 0") and it gives the user the
 * kacchiOS> prompt.  Because it is just another process, the timer
 * can preempt it while it waits for a key - which is exactly how the
 * other processes get a chance to print in between prompt lines.
 */
static void null_process(void) {
    char input[MAX_INPUT];
    int pos;

    while (1) {
        serial_puts("kacchiOS> ");
        pos = 0;

        /* Read one line from the serial console. */
        while (1) {
            char c = serial_getc();

            if (c == '\r' || c == '\n') {
                input[pos] = '\0';
                serial_puts("\n");
                break;
            } else if ((c == '\b' || c == 0x7F) && pos > 0) {
                pos--;
                serial_puts("\b \b");
            } else if (c >= 32 && c < 127 && pos < MAX_INPUT - 1) {
                input[pos++] = c;
                serial_putc(c);
            }
        }

        if (pos == 0) {
            continue;
        }

        /* Split "command arguments" at the first space. */
        {
            char full[MAX_INPUT];
            char* args;
            int i;

            for (i = 0; i <= pos; i++) {
                full[i] = input[i];
            }
            args = input;
            while (*args != '\0' && *args != ' ') args++;
            if (*args == ' ') {
                *args = '\0';
                args++;
                while (*args == ' ') args++;
            }

            if (strcmp(input, "help") == 0) {
                cmd_help();
            } else if (strcmp(input, "ps") == 0) {
                cmd_ps();
            } else if (strcmp(input, "spawn") == 0) {
                cmd_spawn();
            } else if (strcmp(input, "server") == 0) {
                cmd_server();
            } else if (strcmp(input, "kill") == 0) {
                cmd_kill(args);
            } else if (strcmp(input, "quantum") == 0) {
                cmd_quantum(args);
            } else if (strcmp(input, "send") == 0) {
                cmd_send(args);
            } else if (strcmp(input, "inbox") == 0) {
                cmd_inbox();
            } else if (strcmp(input, "busy") == 0) {
                cmd_busy(args);
            } else if (strcmp(input, "mem") == 0) {
                cmd_mem();
            } else if (strcmp(input, "memtest") == 0) {
                run_memory_test();
            } else {
                serial_puts("You typed: ");
                serial_puts(full);
                serial_puts("\n");
            }
        }
    }
}

/* ================================================================== */
/* Boot                                                                */
/* ================================================================== */

void kmain(void) {
    int pid;

    serial_init();

    serial_puts("\n");
    serial_puts("========================================\n");
    serial_puts("    kacchiOS - Minimal Baremetal OS\n");
    serial_puts("========================================\n");
    serial_puts("Hello from kacchiOS!\n");

    /* Order matters: the process table and the heap must exist before
       the first process is created, and the timer must be running
       before the scheduler starts making decisions. */
    process_table_init();
    memory_init();
    scheduler_init();
    interrupts_init();

    pid = process_create("null", null_process, STACK_SLOT_SIZE, 2);
    if (pid != 0) {
        serial_puts("kmain: failed to create the null process\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    serial_puts("type 'help' for commands\n\n");
    serial_puts("Starting the scheduler...\n");

    /* Never returns: we jump into the null process here. */
    scheduler_start();
}
