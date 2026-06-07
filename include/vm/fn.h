#ifndef FN_H
#define FN_H

#include "essentials/memory.h"
#include "essentials/dynarr.h"

#include "vm_types.h"

typedef struct opcode_location{
	size_t offset;
	int    line;
    char   *filepath;
}OpCodeInfo;

struct fn{
    uint8_t         arity;
    char            *name;
    DynArr          *chunks;
    DynArr          *iconsts;
    DynArr          *fconsts;
    DynArr          *locations;
    Module          *module;
};

#endif