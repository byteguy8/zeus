#include "native_array.h"

#include "vm/types_utils.h"
#include "vm/vm.h"

#include <limits.h>

static native_t NBARRAY_ID = NATIVE_T_UNSET;

static void destroy_native_array(const Allocator *allocator, void *raw_native){
	NativeArray *native_array = raw_native;

	MEMORY_DEALLOC(allocator, char, native_array->len, native_array->bytes);
    MEMORY_DEALLOC(allocator, NativeArray, 1, raw_native);
}

static Value access_get(NativeContext *context, const void *self, Value idx){
    VM *vm = native_vm(context);
    const NativeArray *native_array = self;
    size_t safe_idx = vm_utils_validate_idx(vm, native_array->len, VALUE_TO_INT(idx));

    return INT_VALUE(native_array->bytes[safe_idx]);
}

static Value access_set(NativeContext *context, void *self, Value idx, Value value){
    VM *vm = native_vm(context);
    NativeArray *native_array = self;
    size_t safe_idx = vm_utils_validate_idx(vm, native_array->len, VALUE_TO_INT(idx));
    unsigned char input_value = (unsigned char)vm_utils_value_validate_int_range(
        vm,
        value,
        0,
        UCHAR_MAX,
        "Illegal value for native array"
    );

    native_array->bytes[safe_idx] = input_value;

    return EMPTY_VALUE;
}

static Value native_array_native_len(VM *vm, uint8_t argcs, Value *args, Value target){
    NativeArray *native_array = (NativeArray *)VALUE_TO_NATIVE(target)->raw_native;

    return INT_VALUE(native_array->len);
}

static Value native_array_native_fill(VM *vm, uint8_t argcs, Value *args, Value target){
    NativeArray *native_array = (NativeArray *)VALUE_TO_NATIVE(target)->raw_native;
    size_t native_array_len = native_array->len;

    size_t from = vm_utils_value_validate_idx_range_arg(vm, args[0], 1, "from", 0, native_array_len);
    size_t to = vm_utils_value_validate_idx_range_arg(vm, args[1], 2, "to", from, native_array_len);
    unsigned char input_value = (unsigned char)vm_utils_value_validate_int_range_arg(
        vm,
        args[2],
        3,
        "value",
        0,
        UCHAR_MAX
    );

    size_t written_len = to - from;

    memset(native_array->bytes + from, input_value, written_len);

    return INT_VALUE(written_len);
}

static Value native_array_native_move(VM *vm, uint8_t argcs, Value *args, Value target){
    NativeArray *a_native_array = (NativeArray *)VALUE_TO_NATIVE(target)->raw_native;
    size_t a_native_array_len = a_native_array->len;

    size_t src_from = vm_utils_value_validate_idx_range_arg(vm, args[0], 1, "source from", 0, a_native_array_len);
    size_t src_to = vm_utils_value_validate_idx_range_arg(vm, args[1], 2, "source to", src_from, a_native_array_len);
    NativeArray *b_native_array = native_array_validate_value_arg(vm, 3, "destination", args[2]);

    size_t copy_size = src_to - src_from;
    size_t b_native_array_len = b_native_array->len;

    size_t dst_offset = vm_utils_value_validate_idx_range_arg(vm, args[3], 1, "destination offset", 0, b_native_array_len);

    if(copy_size > (b_native_array_len - dst_offset)){
        vm_error(
            vm,
            "Copy size %zu exceed destination size. Destination offset %zu and length %zu only let available %zu bytes",
            copy_size,
            dst_offset,
            b_native_array_len,
            b_native_array_len - dst_offset
        );
    }

    memmove(
        b_native_array->bytes + dst_offset,
        a_native_array->bytes + src_from,
        copy_size
    );

    return INT_VALUE(copy_size);
}

static Value native_array_native_clone(VM *vm, uint8_t argcs, Value *args, Value target){
    NativeArray *a_native_barray = (NativeArray *)VALUE_TO_NATIVE(target)->raw_native;
    size_t a_native_array_len = a_native_barray->len;

    if(a_native_array_len == 0){
        return EMPTY_VALUE;
    }

    NativeArray *new_native_array = native_array_create(vm_native_context(vm), a_native_array_len);

    memcpy(new_native_array->bytes, a_native_barray->bytes, a_native_array_len);

    return OBJ_VALUE(vm_create_native(vm, new_native_array));
}

static Value native_array_native_to_str(VM *vm, uint8_t argcs, Value *args, Value target){
    const Allocator *allocator = vm_allocator(vm);

    NativeArray *native_array = (NativeArray *)VALUE_TO_NATIVE(target)->raw_native;
    size_t native_array_len = native_array->len;

    size_t from = vm_utils_value_validate_idx_range_arg(vm, args[0], 1, "from", 0, native_array_len);
    size_t to = vm_utils_value_validate_idx_range_arg(vm, args[1], 2, "to", from, native_array_len);

    size_t str_len = to - from;
    char *str_buff = MEMORY_ALLOC(allocator, char, str_len + 1);

    memcpy(str_buff + from, native_array->bytes, str_len);
    str_buff[str_len] = 0;

    StrObj *str_obj = NULL;

    if(vm_create_str(
        vm,
        1,
        str_len,
        str_buff,
        &str_obj
    )){
        MEMORY_DEALLOC(allocator, char, str_len + 1, str_buff);
    }

    return OBJ_VALUE(str_obj);
}

CREATE_VALIDATE_NATIVE_IMPLEMENTATION("native_array", native_array, NBARRAY_ID, NativeArray)

void native_array_init(NativeContext *context){
    NBARRAY_ID = native_identificate(
        context,
        "native_array",
        destroy_native_array,
        access_get,
        access_set,
        NULL
    );

    native_properties_add_native_fn(context, NBARRAY_ID, "len", 0, native_array_native_len);
    native_properties_add_native_fn(context, NBARRAY_ID, "fill", 3, native_array_native_fill);
    native_properties_add_native_fn(context, NBARRAY_ID, "move", 4, native_array_native_move);
    native_properties_add_native_fn(context, NBARRAY_ID, "clone", 0, native_array_native_clone);
    native_properties_add_native_fn(context, NBARRAY_ID, "to_str", 2, native_array_native_to_str);
}

NativeArray *native_array_create(NativeContext *context, size_t len){
    const Allocator *allocator = native_allocator(context);
    unsigned char *bytes = len > 0 ? MEMORY_ALLOC(allocator, unsigned char, len) : NULL;
	NativeArray *native_array = MEMORY_ALLOC(allocator, NativeArray, 1);

    *native_array = (NativeArray){
        .header    = NBARRAY_ID,
        .len       = len,
        .bytes     = bytes
    };

	return native_array;
}