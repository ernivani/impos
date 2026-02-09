#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define HEAP_MAGIC 0xBEEF

typedef struct block_header {
    uint32_t size;
    uint16_t magic;
    uint16_t free;
    struct block_header* next;
} block_header_t;

void* realloc(void* ptr, size_t size) {
    if (ptr == NULL)
        return malloc(size);

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_header_t* header = (block_header_t*)ptr - 1;
    if (header->magic != HEAP_MAGIC)
        return NULL;

    /* Current block is big enough */
    if (header->size >= size)
        return ptr;

    /* Need bigger block */
    void* new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    memcpy(new_ptr, ptr, header->size);
    free(ptr);
    return new_ptr;
}
