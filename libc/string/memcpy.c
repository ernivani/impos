#include <string.h>
#include <stdint.h>

void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
	void *ret = dstptr;
	size_t dwords = size >> 2;
	size_t remain = size & 3;

	/* Bulk copy 4 bytes at a time using rep movsd */
	__asm__ volatile(
		"rep movsl\n\t"
		: "+D"(dstptr), "+S"(srcptr), "+c"(dwords)
		: : "memory"
	);

	/* Copy remaining 0-3 bytes */
	__asm__ volatile(
		"rep movsb\n\t"
		: "+D"(dstptr), "+S"(srcptr), "+c"(remain)
		: : "memory"
	);

	return ret;
}
