#ifndef NATIVE_MODULE_H
#define NATIVE_MODULE_H

#include "essentials/lzohtable.h"
#include "essentials/memory.h"

typedef struct native_module{
	char            *name;
	LZOHTable       *symbols;
    const Allocator *allocator;
}NativeModule;

#endif
