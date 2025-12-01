#ifndef NARRAY_NATIVE_H
#define NARRAY_NATIVE_H

#include "native.h"
#include "vm/vmu.h"

typedef struct narray_native{
	NativeHeader header;
	size_t len;
	unsigned char *bytes;
}NBArrayNative;

NBArrayNative *nbarray_native_create(size_t len, Allocator *allocator);
CREATE_VALIDATE_NATIVE_DECLARATION(nbarray_native, NBArrayNative)

#endif
