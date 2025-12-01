#include "native_random.h"
#include "native.h"
#include "vm/vmu.h"

static void random_native_destroy(void *native, Allocator *allocator){
	MEMORY_DEALLOC(allocator, RandomNative, 1, native);
}

RandomNative *random_native_create(Allocator *allocator){
	RandomNative *native = MEMORY_ALLOC(allocator, RandomNative, 1);

	native_init_header(
		(NativeHeader *)native,
		RANDOM_NATIVE_TYPE,
		"random",
		random_native_destroy, allocator
	);

	return native;
}

CREATE_VALIDATE_NATIVE("random", random_native, RANDOM_NATIVE_TYPE, RandomNative)
