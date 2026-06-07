#ifndef NATIVE_ARRAY_H
#define NATIVE_ARRAY_H

#include "vm/native.h"
#include "vm/vm_utils.h"

typedef struct native_array{
	native_t      header;
	size_t        len;
	unsigned char *bytes;
}NativeArray;

CREATE_VALIDATE_NATIVE_DECLARATION(native_array, NativeArray)

void native_array_init(NativeContext *context);
NativeArray *native_array_create(NativeContext *context, size_t len);

#endif
