/* memory.h - Memory manager (stack pool + heap) */
#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

/*
 * Two separate memory pools are managed here:
 *
 *   Stack pool - STACK_POOL_SLOTS fixed slots of STACK_SLOT_SIZE bytes each.
 *                Every process gets exactly one slot for its stack.
 *                Fixed slots make allocation, free and reuse trivial, and
 *                make double free / invalid free easy to detect.
 *
 *   Heap       - one large region of HEAP_TOTAL_SIZE bytes that starts
 *                right after the kernel image (see link.ld, __kernel_end).
 *                It is cut into variable sized blocks, so any code can
 *                request any amount of memory with allocate_heap().
 *
 * QEMU is started with "-m 64M", so the first 8 MB after the kernel is
 * always safe to use.
 */

#define STACK_POOL_SLOTS  16              /* one slot per possible process */
#define STACK_SLOT_SIZE   8192            /* 8 KB per process stack        */
#define HEAP_TOTAL_SIZE   (8 * 1024 * 1024) /* 8 MB heap                   */

/* Call once at boot, before any allocation happens. */
void memory_init(void);

/* Stack allocation: returns the base of a free stack slot, or NULL. */
void* allocate_stack(uint32_t size);

/* Stack deallocation: slot becomes available again for a new process. */
void free_stack(void* stack_base);

/* Heap allocation: returns a 16-byte aligned block, or NULL on failure. */
void* allocate_heap(uint32_t size);

/* Heap deallocation: block goes back to the heap and can be reused. */
void free_heap(void* ptr);

/* Simple statistics, used by the shell "mem" command. */
uint32_t stack_free_slots(void);
uint32_t heap_free_bytes(void);
uint32_t heap_used_bytes(void);

#endif
