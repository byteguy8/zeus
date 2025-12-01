#ifndef NATIVE_H
#define NATIVE_H

#include "essentials/memory.h"

typedef enum native_type{
	RANDOM_NATIVE_TYPE,
	FILE_NATIVE_TYPE,
	NBARRAY_NATIVE_TYPE,
}NativeType;

typedef void (* NativeDestroyHelper)(void *native, Allocator *allocator);

typedef struct native_header{
	NativeType type;
	char *name;
	NativeDestroyHelper destroy_helper;
}NativeHeader;

void native_init_header(
	NativeHeader *header,
	NativeType type,
	char *name,
	NativeDestroyHelper destroy_helper,
	Allocator *allocator
);

#endif
