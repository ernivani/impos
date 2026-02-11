#include <string.h>
#include <stdint.h>

int memcmp(const void* aptr, const void* bptr, size_t size) {
	const uint32_t *a32 = (const uint32_t *)aptr;
	const uint32_t *b32 = (const uint32_t *)bptr;
	size_t dwords = size >> 2;

	/* Compare 4 bytes at a time */
	for (size_t i = 0; i < dwords; i++) {
		if (a32[i] != b32[i]) {
			const unsigned char *a = (const unsigned char *)(a32 + i);
			const unsigned char *b = (const unsigned char *)(b32 + i);
			for (int j = 0; j < 4; j++) {
				if (a[j] < b[j]) return -1;
				if (a[j] > b[j]) return 1;
			}
		}
	}

	/* Compare remaining bytes */
	const unsigned char *a = (const unsigned char *)(a32 + dwords);
	const unsigned char *b = (const unsigned char *)(b32 + dwords);
	for (size_t i = 0; i < (size & 3); i++) {
		if (a[i] < b[i]) return -1;
		if (a[i] > b[i]) return 1;
	}

	return 0;
}
