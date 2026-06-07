#ifndef RANDOM_NATIVE_H
#define RANDOM_NATIVE_H

#include "vm/native.h"
#include "vm/vm_utils.h"

#include "xoshiro256.h"

#define NATIVE_RANDOM_NAME "random"

typedef struct native_random{
	native_t   header;
	XOShiro256 xos256;
}NativeRandom;

CREATE_VALIDATE_NATIVE_DECLARATION(native_random, NativeRandom)

void native_random_init(NativeContext *context);
NativeRandom *native_random_create(NativeContext *context);
NativeRandom *native_random_create_seed(NativeContext *context, int64_t seed);

#endif
