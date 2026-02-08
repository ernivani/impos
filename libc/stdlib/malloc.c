#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define HEAP_MAGIC 0xBEEF
#define HEAP_MAX   (16 * 1024 * 1024)  /* 16 MB */
#define ALIGN(x)   (((x) + 7) & ~7)

typedef struct block_header {
    uint32_t size;      /* payload size */
    uint16_t magic;
    uint16_t free;      /* 1 = free, 0 = used */
    struct block_header* next;
} block_header_t;

extern char _heap_start[];
static block_header_t* free_list = NULL;
static char* heap_end = NULL;

static void heap_init(void) {
    free_list = (block_header_t*)(void*)_heap_start;
    heap_end = (char*)_heap_start;
    free_list->size = 0;
    free_list->magic = HEAP_MAGIC;
    free_list->free = 1;
    free_list->next = NULL;
}

static block_header_t* request_space(block_header_t* last, size_t size) {
    size_t total = sizeof(block_header_t) + size;
    char* new_end = heap_end + total;

    if ((size_t)(new_end - (char*)_heap_start) > HEAP_MAX) {
        return NULL;
    }

    block_header_t* block = (block_header_t*)heap_end;
    heap_end = new_end;

    block->size = size;
    block->magic = HEAP_MAGIC;
    block->free = 0;
    block->next = NULL;

    if (last) {
        last->next = block;
    }

    return block;
}

void* malloc(size_t size) {
    if (size == 0) return NULL;

    size = ALIGN(size);

    if (!free_list) {
        heap_init();
    }

    /* First-fit search */
    block_header_t* current = free_list;
    block_header_t* last = free_list;

    while (current) {
        if (current->free && current->size >= size) {
            current->free = 0;
            return (void*)(current + 1);
        }
        last = current;
        current = current->next;
    }

    /* No free block found, request more space */
    block_header_t* block = request_space(last, size);
    if (!block) return NULL;

    return (void*)(block + 1);
}

void free(void* ptr) {
    if (!ptr) return;

    block_header_t* header = (block_header_t*)ptr - 1;
    if (header->magic != HEAP_MAGIC) return;

    header->free = 1;

    /* Coalesce adjacent free blocks */
    block_header_t* current = free_list;
    while (current && current->next) {
        if (current->free && current->next->free) {
            current->size += sizeof(block_header_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}
