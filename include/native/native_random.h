#ifndef RANDOM_NATIVE_H
#define RANDOM_NATIVE_H

#include "essentials/memory.h"

#include "vm/obj.h"
#include "vm/types_utils.h"
#include "vm/vmu.h"

#include "native.h"
#include "xoshiro256.h"

typedef struct random_native{
	NativeHeader header;
	XOShiro256 xos256;
}RandomNative;

RandomNative *random_native_create(Allocator *allocator);
CREATE_VALIDATE_NATIVE_DECLARATION(random_native, RandomNative)

#endif
