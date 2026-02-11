#include <string.h>
#include <stdint.h>

void* memset(void* bufptr, int value, size_t size) {
	void *ret = bufptr;

	/* Replicate the byte across all 4 bytes of a dword */
	uint32_t val32 = (uint8_t)value;
	val32 |= val32 << 8;
	val32 |= val32 << 16;

	size_t dwords = size >> 2;
	size_t remain = size & 3;

	/* Bulk fill 4 bytes at a time using rep stosd */
	__asm__ volatile(
		"rep stosl\n\t"
		: "+D"(bufptr), "+c"(dwords)
		: "a"(val32)
		: "memory"
	);

	/* Fill remaining 0-3 bytes */
	__asm__ volatile(
		"rep stosb\n\t"
		: "+D"(bufptr), "+c"(remain)
		: "a"(value)
		: "memory"
	);

	return ret;
}
