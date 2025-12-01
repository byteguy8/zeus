#include "native_nbarray.h"

#include <string.h>

static void narray_native_destroy(void *native, Allocator *allocator){
	NBArrayNative *buff_native = native;

	MEMORY_DEALLOC(allocator, char, buff_native->len, buff_native->bytes);
	MEMORY_DEALLOC(allocator, NBArrayNative, 1, buff_native);
}

NBArrayNative *nbarray_native_create(size_t len, Allocator *allocator){
	unsigned char *bytes = MEMORY_ALLOC(allocator, unsigned char, len);
	NBArrayNative *nbarray_native = MEMORY_ALLOC(allocator, NBArrayNative, 1);

	memset(bytes, 0, len);

	native_init_header(
		(NativeHeader *)nbarray_native,
		NBARRAY_NATIVE_TYPE,
		"nbuff",
		narray_native_destroy,
		allocator
	);
	nbarray_native->len = len;
	nbarray_native->bytes = bytes;

	return nbarray_native;
}

CREATE_VALIDATE_NATIVE("nbarray", nbarray_native, NBARRAY_NATIVE_TYPE, NBArrayNative)
