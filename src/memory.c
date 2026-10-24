/* memory.c - Memory manager (stack pool + heap)
 *
 * Memory layout of the machine while kacchiOS runs (QEMU -m 64M):
 *
 *   1 MB        kernel image (.text, .data, .bss)   <- loaded by boot.S
 *   __kernel_end ---- heap region starts here (8 MB)
 *   ...         free physical memory (not used by us)
 *   64 MB       end of RAM
 *
 * The stack pool lives inside the kernel .bss (stack_pool below), so it is
 * cleared automatically by boot.S and its address is always valid.
 */
#include "memory.h"
#include "serial.h"
#include "interrupts.h"

/* ------------------------------------------------------------------ */
/* Stack pool: fixed size slots                                        */
/* ------------------------------------------------------------------ */

/* uint32_t (not uint8_t) so the compiler aligns every slot to 4 bytes. */
static uint32_t stack_pool[STACK_POOL_SLOTS][STACK_SLOT_SIZE / 4];
static uint8_t  stack_used[STACK_POOL_SLOTS];

/* ------------------------------------------------------------------ */
/* Heap: variable sized blocks kept in one address ordered list        */
/* ------------------------------------------------------------------ */

/*
 * Every block starts with this 16-byte header.  Blocks are linked in
 * increasing address order, so walking the list shows the heap exactly
 * as it looks in memory.  That makes splitting and coalescing easy:
 *
 *   before free(B):   [A used][B used][C used]
 *   after  free(B):   [A used][B free][C used]
 *   after  free(A):   [A free ..............][C used]   (A and B merged)
 */
struct mem_block {
    uint32_t size;              /* payload bytes that follow the header */
    uint32_t in_use;            /* 0 = free, 1 = allocated              */
    struct mem_block* next;     /* next block in address order          */
    struct mem_block* prev;     /* previous block in address order      */
};

#define HEAP_ALIGN      16                       /* all payloads 16-byte aligned */
#define MIN_SPARE       32                       /* do not split off tiny scraps */

/* The linker script exports __kernel_end = first byte after the kernel. */
extern char __kernel_end[];

static struct mem_block* heap_first = NULL;
static uint8_t* heap_end = NULL;

/* Round any size up to the next multiple of HEAP_ALIGN. */
static uint32_t align_up(uint32_t size) {
    return (size + (HEAP_ALIGN - 1)) & ~(uint32_t)(HEAP_ALIGN - 1);
}

/* Payload pointer <-> header pointer conversions. */
static void* payload_of(struct mem_block* block) {
    return (void*)((uint8_t*)block + sizeof(struct mem_block));
}

void memory_init(void) {
    uint32_t i;

    /* All stack slots start out free.  (bss is already zero, but being
       explicit makes the initial state obvious to a reader.) */
    for (i = 0; i < STACK_POOL_SLOTS; i++) {
        stack_used[i] = 0;
    }

    /* Heap = everything from __kernel_end up to HEAP_TOTAL_SIZE later. */
    uint8_t* start = (uint8_t*)(((uint32_t)__kernel_end + (HEAP_ALIGN - 1))
                                & ~(uint32_t)(HEAP_ALIGN - 1));
    heap_end = start + HEAP_TOTAL_SIZE;

    /* Safety net: QEMU runs with 64 MB of RAM, so anything below 60 MB
       is safe.  If someone lowers -m too far we complain at boot
       instead of silently corrupting memory later. */
    if ((uint32_t)heap_end > 60 * 1024 * 1024) {
        serial_puts("memory_init: WARNING heap end beyond 60MB\n");
    }

    /* One big free block covering the whole heap. */
    heap_first = (struct mem_block*)start;
    heap_first->size  = HEAP_TOTAL_SIZE - sizeof(struct mem_block);
    heap_first->in_use = 0;
    heap_first->next  = NULL;
    heap_first->prev  = NULL;
}

/* ------------------------------------------------------------------ */
/* Stack allocation                                                    */
/* ------------------------------------------------------------------ */

/*
 * allocate_stack - reserve one stack slot.
 *
 * size   requested stack size in bytes (must fit into one slot)
 * returns base address of the slot, or NULL if no slot is free or the
 * requested size does not fit.
 */
void* allocate_stack(uint32_t size) {
    uint32_t flags;
    uint32_t i;

    if (size == 0 || size > STACK_SLOT_SIZE) {
        return NULL;
    }

    flags = interrupt_save();
    for (i = 0; i < STACK_POOL_SLOTS; i++) {
        if (stack_used[i] == 0) {
            stack_used[i] = 1;
            interrupt_restore(flags);
            return &stack_pool[i][0];
        }
    }

    /* Every slot is busy: the caller must handle this (return code -1
       in the process manager when the process table is also full). */
    interrupt_restore(flags);
    return NULL;
}

/*
 * free_stack - give a stack slot back.
 *
 * Checks three obvious mistakes instead of corrupting the pool:
 *   1. pointer is not inside the stack pool at all
 *   2. pointer is inside a slot but not at its base
 *   3. slot is already free (double free)
 */
void free_stack(void* stack_base) {
    uint32_t flags;
    uint8_t* p;
    uint32_t offset;
    uint32_t index;

    if (stack_base == NULL) {
        return;
    }

    flags = interrupt_save();

    p = (uint8_t*)stack_base;
    if (p < (uint8_t*)stack_pool ||
        p >= (uint8_t*)stack_pool + sizeof(stack_pool)) {
        serial_puts("free_stack: invalid pointer ignored\n");
        interrupt_restore(flags);
        return;
    }

    offset = (uint32_t)(p - (uint8_t*)stack_pool);
    if (offset % STACK_SLOT_SIZE != 0) {
        serial_puts("free_stack: pointer is not a stack base\n");
        interrupt_restore(flags);
        return;
    }

    index = offset / STACK_SLOT_SIZE;
    if (stack_used[index] == 0) {
        serial_puts("free_stack: double free ignored\n");
        interrupt_restore(flags);
        return;
    }

    stack_used[index] = 0;   /* slot can be reused by the next process */
    interrupt_restore(flags);
}

uint32_t stack_free_slots(void) {
    uint32_t flags = interrupt_save();
    uint32_t i, free_count = 0;
    for (i = 0; i < STACK_POOL_SLOTS; i++) {
        if (stack_used[i] == 0) {
            free_count++;
        }
    }
    interrupt_restore(flags);
    return free_count;
}

/* ------------------------------------------------------------------ */
/* Heap allocation                                                     */
/* ------------------------------------------------------------------ */

/*
 * allocate_heap - first fit allocation from the heap.
 *
 * Walks the block list from the low address upwards and takes the first
 * free block that is large enough (that is "first fit").  If the block
 * is much larger than needed it is split in two, so the unused tail
 * stays available for the next request.
 *
 * returns 16-byte aligned memory, or NULL when the heap is exhausted.
 */
void* allocate_heap(uint32_t size) {
    uint32_t flags;
    struct mem_block* block;

    if (size == 0) {
        return NULL;
    }
    size = align_up(size);

    flags = interrupt_save();

    for (block = heap_first; block != NULL; block = block->next) {
        if (block->in_use == 0 && block->size >= size) {

            /* Split only when the remainder can hold a real block. */
            if (block->size >= size + sizeof(struct mem_block) + MIN_SPARE) {
                struct mem_block* rest;
                uint32_t rest_size = block->size - size - sizeof(struct mem_block);

                rest = (struct mem_block*)((uint8_t*)payload_of(block) + size);
                rest->size   = rest_size;
                rest->in_use = 0;
                rest->next   = block->next;
                rest->prev   = block;
                if (rest->next != NULL) {
                    rest->next->prev = rest;
                }
                block->next = rest;
                block->size = size;
            }

            block->in_use = 1;
            interrupt_restore(flags);
            return payload_of(block);
        }
    }

    /* No free block was big enough: out of memory. */
    interrupt_restore(flags);
    return NULL;
}

/*
 * free_heap - mark a block as free and merge it with free neighbours.
 *
 * Merging (coalescing) with the neighbour on each side puts the bytes
 * back together into one large block, which avoids the heap slowly
 * breaking into unusable fragments.
 */
void free_heap(void* ptr) {
    uint32_t flags;
    struct mem_block* block;
    struct mem_block* found = NULL;
    uint8_t* p;

    if (ptr == NULL) {
        return;
    }

    flags = interrupt_save();
    p = (uint8_t*)ptr;

    if (p < (uint8_t*)heap_first || p >= heap_end) {
        serial_puts("free_heap: invalid pointer ignored\n");
        interrupt_restore(flags);
        return;
    }

    /* Validate the pointer: it must be exactly the payload start of a
       block we know about.  A wrong pointer cannot pass this walk. */
    for (block = heap_first; block != NULL; block = block->next) {
        if ((uint8_t*)payload_of(block) == p) {
            found = block;
            break;
        }
    }
    if (found == NULL) {
        serial_puts("free_heap: pointer is not a heap block\n");
        interrupt_restore(flags);
        return;
    }

    if (found->in_use == 0) {
        serial_puts("free_heap: double free ignored\n");
        interrupt_restore(flags);
        return;
    }

    found->in_use = 0;

    /* Merge with the next block in memory if that one is free. */
    if (found->next != NULL && found->next->in_use == 0) {
        struct mem_block* next = found->next;
        found->size += sizeof(struct mem_block) + next->size;
        found->next = next->next;
        if (next->next != NULL) {
            next->next->prev = found;
        }
    }

    /* Merge with the previous block in memory if that one is free.
       Because every free merges both sides, two neighbouring free
       blocks can never exist, so one step here is always enough. */
    if (found->prev != NULL && found->prev->in_use == 0) {
        struct mem_block* prev = found->prev;
        prev->size += sizeof(struct mem_block) + found->size;
        prev->next = found->next;
        if (found->next != NULL) {
            found->next->prev = prev;
        }
    }

    interrupt_restore(flags);
}

uint32_t heap_free_bytes(void) {
    uint32_t flags = interrupt_save();
    struct mem_block* block;
    uint32_t total = 0;
    for (block = heap_first; block != NULL; block = block->next) {
        if (block->in_use == 0) {
            total += block->size;
        }
    }
    interrupt_restore(flags);
    return total;
}

uint32_t heap_used_bytes(void) {
    uint32_t flags = interrupt_save();
    struct mem_block* block;
    uint32_t total = 0;
    for (block = heap_first; block != NULL; block = block->next) {
        if (block->in_use != 0) {
            total += block->size;
        }
    }
    interrupt_restore(flags);
    return total;
}
