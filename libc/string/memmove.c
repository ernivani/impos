#include <string.h>
#include <stdint.h>

void* memmove(void* dstptr, const void* srcptr, size_t size) {
	if (dstptr == srcptr || size == 0)
		return dstptr;

	if (dstptr < srcptr) {
		/* Forward copy — same as memcpy */
		void *d = dstptr;
		const void *s = srcptr;
		size_t dwords = size >> 2;
		size_t remain = size & 3;
		__asm__ volatile(
			"rep movsl\n\t"
			: "+D"(d), "+S"(s), "+c"(dwords)
			: : "memory"
		);
		__asm__ volatile(
			"rep movsb\n\t"
			: "+D"(d), "+S"(s), "+c"(remain)
			: : "memory"
		);
	} else {
		/* Backward copy — start from end, set direction flag */
		unsigned char *d = (unsigned char *)dstptr + size - 1;
		const unsigned char *s = (const unsigned char *)srcptr + size - 1;
		size_t remain = size & 3;
		size_t dwords = size >> 2;
		/* Copy trailing bytes first (backward) */
		__asm__ volatile(
			"std\n\t"
			"rep movsb\n\t"
			: "+D"(d), "+S"(s), "+c"(remain)
			: : "memory"
		);
		/* Back up pointers to dword-aligned end, copy dwords backward */
		if (dwords > 0) {
			d -= 3;
			s -= 3;
			__asm__ volatile(
				"rep movsl\n\t"
				: "+D"(d), "+S"(s), "+c"(dwords)
				: : "memory"
			);
		}
		__asm__ volatile("cld\n\t" ::: "memory");
	}

	return dstptr;
}
