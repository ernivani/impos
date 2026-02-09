#include <stdlib.h>

static unsigned long rand_seed = 1;

void srand(unsigned int seed) {
    rand_seed = seed;
}

int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (int)((rand_seed >> 16) & RAND_MAX);
}
