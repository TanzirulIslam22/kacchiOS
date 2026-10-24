/* process.h - Process manager (process table and process lifecycle) */
#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"

#define PROCESS_MAX        16         /* like Xinu's NPROC           */
#define PROCESS_NAME_MAX   16
#define PROCESS_NONE       (-1)       /* "no process" / invalid pid  */
#define PROCESS_IPC_MSG_MAX 32        /* size of one mailbox message */

/*
 * The three states from the assignment, plus BLOCKED (bonus item
 * "add more states").  The comments say which transition is allowed
 * from each state:
 *
 *            create                scheduler picks it
 *   (free) --------> READY ------------------------> CURRENT
 *                       ^          ^                    |
 *                       |          | ipc_send wakes it  | scheduler rotates away
 *                       |          |                    v
 *                       +----------+                READY
 *                                    +--------------- CURRENT
 *                       BLOCKED <----+                |
 *                       (waiting for a message)       | process finishes
 *                                                      v
 *                                                  TERMINATED
 *                                                        |
 *                                                        v
 *                                            (stack freed, slot reused)
 *
 * A process moves to BLOCKED only by blocking itself inside
 * ipc_receive_wait(); only ipc_send() can move it back to READY.
 */
enum process_state {
    PROCESS_CURRENT,     /* is using the CPU right now              */
    PROCESS_READY,       /* able to run, waiting for the CPU        */
    PROCESS_TERMINATED,  /* finished; will never run again          */
    PROCESS_BLOCKED      /* waiting for a message from ipc_send()   */
};

/*
 * One process table entry (a "PCB" in other textbooks).  The table is
 * an array of these, so pid = index into the table.
 */
struct process {
    int        in_use;        /* 1 = this slot describes a process   */
    int        pid;           /* index of this entry                 */
    int        state;         /* one of enum process_state           */
    char       name[PROCESS_NAME_MAX];
    void     (*entry)(void);  /* function the process runs           */
    uint8_t*   stack_base;    /* bottom of the process stack         */
    uint32_t   stack_size;    /* size of that stack in bytes         */
    uint32_t   saved_esp;     /* stack pointer, saved at switch away */
    int        priority;      /* higher value = more important       */
    int        wait_ticks;    /* ticks spent READY (used for aging)  */
    char       mailbox[PROCESS_IPC_MSG_MAX]; /* one pending message  */
    int        has_message;   /* 1 = mailbox holds a message         */
};

/* One time setup at boot. */
void process_table_init(void);

/* Create a process.  Returns its pid, or -1 if the table or the
   stack pool is full.  The new process is READY and will start
   running when the scheduler chooses it. */
int process_create(const char* name, void (*entry)(void),
                   uint32_t stack_size, int priority);

/* Table lookup.  Returns NULL for an invalid or free pid. */
struct process* process_get(int pid);

/* The ONLY place where a state changes.  Checks that the transition
   is one of the allowed ones.  Returns 0, or -1 on error. */
int process_set_state(int pid, int state);

/* Terminate another process (used by the shell "kill" command). */
int process_terminate(int pid);

/* Terminate the calling process.  Never returns. */
void process_exit(void);

/* ---- utility functions (assignment: "good to have") ---- */
int         process_current_pid(void);        /* pid of the running process */
int         process_get_state(int pid);       /* state, or -1               */
const char* process_get_name(int pid);        /* name, or NULL              */
uint32_t    process_get_stack_size(int pid);  /* stack size, or 0           */
int         process_count(void);              /* how many processes exist   */

/* ---- simple message passing between processes ---- */
/* ipc_send copies a message into the receiver's mailbox.
   Returns 0 on success, -1 on error (bad pid, mailbox already full). */
int ipc_send(int to_pid, const char* msg);
/* ipc_receive copies the caller's pending message into buf.
   Returns 1 if a message was received, 0 if the mailbox is empty. */
int ipc_receive(char* buf, uint32_t bufsize);
/* ipc_receive_wait blocks (state BLOCKED) until another process
   sends a message, then returns it.  Never returns 0. */
int ipc_receive_wait(char* buf, uint32_t bufsize);

#endif
