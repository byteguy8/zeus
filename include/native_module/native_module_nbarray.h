#ifndef NATIVE_MODULE_NBUFF_H
#define NATIVE_MODULE_NBUFF_H

#include "native/native_nbarray.h"

#include "vm/obj.h"
#include "vm/vm_factory.h"
#include "vm/types_utils.h"
#include "vm/vmu.h"

#include <limits.h>
#include <string.h>

NativeModule *nbarray_native_module = NULL;

Value native_fn_nbarray_len(uint8_t argsc, Value *values, Value target, void *context){
	NBArrayNative *nbarray_native = nbarray_native_validate_value_arg(
		values[0],
		1,
		"array",
		VMU_VM
	);

	return INT_VALUE(nbarray_native->len);
}

Value native_fn_nbarray_set(uint8_t argsc, Value *values, Value target, void *context){
	NBArrayNative *nbarray_native = nbarray_native_validate_value_arg(
		values[0],
		1,
		"dst",
		VMU_VM
	);
	unsigned char value = (unsigned char)validate_value_int_range_arg(
		values[1],
		2,
		"value",
		0,
		UCHAR_MAX,
		VMU_VM
	);

	size_t nbarray_native_len = nbarray_native->len;

	if(nbarray_native_len == 0){
		return EMPTY_VALUE;
	}

	memset(nbarray_native->bytes, value, nbarray_native_len);

	return EMPTY_VALUE;
}

Value native_fn_nbarray_cpy(uint8_t argsc, Value *values, Value target, void *context){
	NBArrayNative *dst_nbarray = nbarray_native_validate_value_arg(
		values[0],
		1,
		"dst",
		VMU_VM
	);
	size_t dst_nbarray_len = dst_nbarray->len;
	size_t dst_offset = validate_value_idx_arg(
		values[1],
		1,
		"dst offset",
		dst_nbarray_len,
		VMU_VM
	);
	NBArrayNative *src_nbarray = nbarray_native_validate_value_arg(
		values[2],
		3,
		"src",
		VMU_VM
	);
	size_t src_nbarray_len = src_nbarray->len;
	size_t src_offset = validate_value_idx_arg(
		values[3],
		4,
		"src offset",
		src_nbarray_len,
		VMU_VM
	);
	int64_t count = validate_value_int_arg(
		values[4],
		5,
		"count",
		VMU_VM
	);

	if(count < 0){
		vmu_error(VMU_VM, "Illegal count: negative");
	}

	if(count == 0){
		return values[0];
	}

	size_t rcount = (size_t)count;
	size_t dst_max_count = dst_nbarray_len - dst_offset;
	size_t src_max_count = src_nbarray_len - src_offset;

	if(rcount > dst_max_count){
		vmu_error(
			VMU_VM,
			"Dest + offset (%zu) left %zu slots to write, but count is %zu",
			dst_offset,
			dst_max_count,
			rcount
		);
	}

	if(rcount > src_max_count){
		vmu_error(
			VMU_VM,
			"Src + offset (%zu) left %zu slots to write, but count is %zu",
			src_offset,
			src_max_count,
			rcount
		);
	}

	memcpy(
		dst_nbarray->bytes + dst_offset,
		src_nbarray->bytes + src_offset,
		count
	);

	return values[0];
}

Value native_fn_nbarray_mov(uint8_t argsc, Value *values, Value target, void *context){
	NBArrayNative *dst_nbarray = nbarray_native_validate_value_arg(
		values[0],
		1,
		"dst",
		VMU_VM
	);
	size_t dst_nbarray_len = dst_nbarray->len;
	size_t dst_offset = validate_value_idx_arg(
		values[1],
		1,
		"dst offset",
		dst_nbarray_len,
		VMU_VM
	);
	NBArrayNative *src_nbarray = nbarray_native_validate_value_arg(
		values[2],
		3,
		"src",
		VMU_VM
	);
	size_t src_nbarray_len = src_nbarray->len;
	size_t src_offset = validate_value_idx_arg(
		values[3],
		4,
		"src offset",
		src_nbarray_len,
		VMU_VM
	);
	int64_t count = validate_value_int_arg(
		values[4],
		5,
		"count",
		VMU_VM
	);

	if(count < 0){
		vmu_error(VMU_VM, "Illegal count: negative");
	}

	if(count == 0){
		return values[0];
	}

	size_t rcount = (size_t)count;
	size_t dst_max_count = dst_nbarray_len - dst_offset;
	size_t src_max_count = src_nbarray_len - src_offset;

	if(rcount > dst_max_count){
		vmu_error(
			VMU_VM,
			"Dest + offset (%zu) left %zu slots to write, but count is %zu",
			dst_offset,
			dst_max_count,
			rcount
		);
	}

	if(rcount > src_max_count){
		vmu_error(
			VMU_VM,
			"Src + offset (%zu) left %zu slots to write, but count is %zu",
			src_offset,
			src_max_count,
			rcount
		);
	}

	memmove(
		dst_nbarray->bytes + dst_offset,
		src_nbarray->bytes + src_offset,
		count
	);

	return values[0];
}

Value native_fn_nbarray_clone(uint8_t argsc, Value *values, Value target, void *context){
	NBArrayNative *nbarray = nbarray_native_validate_value_arg(
		values[0],
		1,
		"array",
		VMU_VM
	);
	size_t nbarray_len = nbarray->len;
	NBArrayNative *cloned_nbarray = nbarray_native_create(
		nbarray_len,
		VMU_NATIVE_FRONT_ALLOCATOR
	);
	NativeObj *native_obj = vmu_create_native(cloned_nbarray, VMU_VM);

	memcpy(cloned_nbarray->bytes, nbarray->bytes, nbarray_len);

	return OBJ_VALUE(native_obj);
}

Value native_fn_nbarray_to_str(uint8_t argsc, Value *values, Value target, void *context){
	NBArrayNative *nbarray = nbarray_native_validate_value_arg(
		values[0],
		1,
		"array",
		VMU_VM
	);
    size_t len = validate_value_len_arg(values[1], 2, "len", VMU_VM);
	size_t nbarray_len = nbarray->len;
    StrObj *str_obj = NULL;

    if(len > nbarray_len){
        vmu_error(
            VMU_VM,
            "len (%zu) out of bounds (%zu)",
            len,
            nbarray_len
        );
    }

    char *buff = MEMORY_ALLOC(
        VMU_NATIVE_FRONT_ALLOCATOR,
        char,
        len + 1
    );

    memcpy(buff, nbarray->bytes, len);
    buff[len] = 0;

	if(vmu_create_str(
		1,
		len,
		buff,
		VMU_VM,
		&str_obj
	)){
        MEMORY_DEALLOC(
            VMU_NATIVE_FRONT_ALLOCATOR,
            unsigned char,
            len + 1,
            buff
        );
    }

	return OBJ_VALUE(str_obj);
}

Value native_fn_nbarray_create(uint8_t argsc, Value *values, Value target, void *context){
	size_t len = validate_value_len_arg(values[0], 1, "len", VMU_VM);
	NBArrayNative *nbarray_native = nbarray_native_create(
		len,
		VMU_NATIVE_FRONT_ALLOCATOR
	);
	NativeObj *native_obj = vmu_create_native(nbarray_native, VMU_VM);

	return OBJ_VALUE(native_obj);
}

void nbarray_module_init(const Allocator *allocator){
    nbarray_native_module = vm_factory_native_module_create(allocator, "nbarray");

    vm_factory_native_module_add_native_fn(nbarray_native_module, "len", 1, native_fn_nbarray_len);
    vm_factory_native_module_add_native_fn(nbarray_native_module, "set", 2, native_fn_nbarray_set);
    vm_factory_native_module_add_native_fn(nbarray_native_module, "cpy", 5, native_fn_nbarray_cpy);
    vm_factory_native_module_add_native_fn(nbarray_native_module, "mov", 5, native_fn_nbarray_mov);
    vm_factory_native_module_add_native_fn(nbarray_native_module, "clone", 1, native_fn_nbarray_clone);
    vm_factory_native_module_add_native_fn(nbarray_native_module, "to_str", 2, native_fn_nbarray_to_str);
    vm_factory_native_module_add_native_fn(nbarray_native_module, "create", 1, native_fn_nbarray_create);
}

#endif
