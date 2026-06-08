#ifndef VM_FACTORY_H
#define VM_FACTORY_H

#include "essentials/memory.h"

#include "native_fn.h"
#include "fn.h"
#include "native_module.h"
#include "obj.h"
#include "value.h"
#include "vm/module.h"

#include <stdint.h>

NativeFn *vm_factory_native_fn_create(
    const Allocator *allocator,
    uint8_t core,
    const char *name,
    uint8_t arity,
    RawNativeFn raw_native
);
void vm_factory_native_fn_destroy(NativeFn *native_fn);

ModuleContext *vm_factory_module_context_create(const Allocator *allocator, const char *pathname);
void vm_factory_module_context_destroy(ModuleContext *module_context, const Allocator *allocator);

Module *vm_factory_module_create(const Allocator *allocator, const char *name, const char *pathname);
Module *vm_factory_module_sole_create(const Allocator *allocator, ModuleContext *context, const char *name, const char *pathname);
void vm_factory_module_destroy(Module *module, const Allocator *allocator);

Fn *vm_factory_module_fn_create(const Allocator *allocator, Module *module, const char *name, uint8_t arity, size_t *out_fn_idx);
ClosureInf *vm_factory_module_closure_inf_create(const Allocator *allocator, Module *module, size_t ats_len, size_t *out_closure_idx);

int vm_factory_module_globals_add_obj(
	Module *module,
	Obj *obj,
	const char *name,
	GlobalValueAccessType access_type
);

NativeModule *vm_factory_native_module_create(const Allocator *allocator, const char *name);
void vm_factory_native_module_destroy(NativeModule *module);
int vm_factory_native_module_add_value(
    NativeModule *native_module,
    const char *name,
    Value value
);
int vm_factory_native_module_add_native_fn(
    NativeModule *module,
    const char *name,
    uint8_t arity,
    RawNativeFn raw_native
);

int vm_factory_native_fn_add_info(
    LZOHTable *natives,
    const Allocator *allocator,
    const char *name,
    uint8_t arity,
    RawNativeFn raw_native
);

FnObj *vm_factory_fn_obj_create(const Allocator *allocator, Fn *fn);
NativeFnObj *vm_factory_native_fn_obj_create(const Allocator *allocator, NativeFn *native_fn);
NativeModuleObj *vm_factory_native_module_obj_create(
    const Allocator *allocator,
    NativeModule *native_module
);
ModuleObj *vm_factory_module_obj_create(const Allocator *allocator, Module *module);

#endif
