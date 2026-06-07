#ifndef NATIVE_FN_H
#define NATIVE_FN_H

#include "essentials/memory.h"

#include "vm_types.h"
#include "value.h"

#include <stdint.h>

typedef Value (*RawNativeFn)(VM *vm, uint8_t argc, Value *values, Value target);

typedef struct native_fn_info{
    uint8_t     arity;
    RawNativeFn raw_native;
}NativeFnInfo;

typedef struct native_fn{
    uint8_t         core;
    uint8_t         arity;
    char            *name;
    RawNativeFn     raw_fn;
    const Allocator *allocator;
}NativeFn;

#endif