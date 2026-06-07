#ifndef NATIVE_H
#define NATIVE_H

#include "essentials/memory.h"

#include "vm_types.h"
#include "native_fn.h"
#include "value.h"
#include "obj.h"

#include <stdint.h>

typedef size_t                native_t;
typedef struct native_context NativeContext;

typedef void (*NativeDestroyHelper)(const Allocator *allocator, void *raw_native);
typedef Value (*NativeAccessIdxGet)(NativeContext *context, const void *raw_native, Value idx);
typedef Value (*NativeAccessIdxSet)(NativeContext *context, void *raw_native, Value idx, Value value);
typedef Value (*NativeGetProperty)(NativeContext *context, const void *raw_native, size_t len, const char *name);

#define NATIVE_T_UNSET SIZE_MAX

#define CREATE_VALIDATE_NATIVE_DECLARATION(_struct_name, _struct_type) \
	_struct_type *_struct_name##_validate_value_arg(VM *vm, uint8_t param, const char *name, Value value);

#define CREATE_VALIDATE_NATIVE_IMPLEMENTATION(_name, _struct_name, _native_id, _struct_type)                 \
	_struct_type *_struct_name ## _validate_value_arg(VM *vm, uint8_t param, const char *name, Value value){ \
	    NativeObj *native_obj = is_value_native(value) ? VALUE_TO_NATIVE(value) : NULL;                      \
        void *raw_native = native_obj ? native_obj->raw_native : NULL;                                       \
	    native_t native = raw_native ? *(native_t *)raw_native : NATIVE_T_UNSET;                                         \
	    																				                     \
	    if(native == _native_id){                                                                            \
			return (_struct_type *)raw_native;                                                               \
		}																						             \
																							                 \
		vm_error(																		                     \
			vm,																				                 \
			"Illegal type of argument %" PRIu8 ": expect '%s' of type '%s'",                                 \
			param,                                                                                           \
	        name,																			                 \
			_name																			                 \
		);																				                     \
																							                 \
		return NULL;                                                                                         \
	}

native_t native_identificate(
    NativeContext       *context,
    const char          *name,
    NativeDestroyHelper destroy_helper,
    NativeAccessIdxGet  access_get_fn,
    NativeAccessIdxSet  access_set_fn,
    NativeGetProperty     get_symbol_fn
);

VM *native_vm(NativeContext *context);
Allocator *native_allocator(NativeContext *context);

const char *native_type_get_name(const NativeContext *context, native_t native);
NativeDestroyHelper native_type_destroy_helper_fn(const NativeContext *context, native_t native);
NativeAccessIdxGet native_type_access_idx_get_fn(const NativeContext *context, native_t native);
NativeAccessIdxSet native_type_access_idx_set_fn(const NativeContext *context, native_t native);
NativeGetProperty native_type_get_property_fn(const NativeContext *context, native_t native);

void native_properties_add_native_fn(NativeContext *context, native_t native, const char *name, uint8_t arity, RawNativeFn native_fn);

#endif
