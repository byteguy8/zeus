#include "native_random.h"

#include "essentials/memory.h"
#include "vm/vm_utils.h"

static native_t RANDOM_ID = NATIVE_T_UNSET;

static void native_random_destroy(const Allocator *allocator, void *raw_native){
    MEMORY_DEALLOC(allocator, NativeRandom, 1, raw_native);
}

static Value native_random_next_int(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeRandom *random = (NativeRandom *)VALUE_TO_NATIVE(target)->raw_native;

    return INT_VALUE(xoshiro256_next(&random->xos256));
}

static Value native_random_next_float(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeRandom *random = (NativeRandom *)VALUE_TO_NATIVE(target)->raw_native;
    uint64_t u64_value = xoshiro256_next(&random->xos256);
    double f64_value = ((double)u64_value) / ((double)UINT64_MAX);

    if(f64_value == 1.0){
        f64_value = 0.3142857143;
    }

    return FLOAT_VALUE(f64_value);
}

static Value native_random_next_int_range(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeRandom *random = (NativeRandom *)VALUE_TO_NATIVE(target)->raw_native;
    int64_t min = vm_utils_value_validate_int_arg(vm, args[0], 1, "min");
    int64_t max = vm_utils_value_validate_int_range_arg(vm, args[1], 2, "max", min, INT64_MAX);

    return INT_VALUE(xoshiro256_next(&random->xos256) % (max - min + 1) + min);
}

static Value native_random_bools(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeRandom *random = (NativeRandom *)VALUE_TO_NATIVE(target)->raw_native;

    size_t len = vm_utils_value_validate_len_arg(vm, args[0], 1, "length");

    ArrayObj *array_obj = vm_create_array(vm, (int64_t)len);
    Value *array_obj_values = array_obj->values;

    for (size_t i = 0; i < len; i++){
        array_obj_values[i] = BOOL_VALUE(xoshiro256_next(&random->xos256) % 2);
    }

    return OBJ_VALUE(array_obj);
}

static Value native_random_ints(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeRandom *random = (NativeRandom *)VALUE_TO_NATIVE(target)->raw_native;

    size_t len = vm_utils_value_validate_len_arg(vm, args[0], 1, "length");

    ArrayObj *array_obj = vm_create_array(vm, (int64_t)len);
    Value *array_obj_values = array_obj->values;

    for (size_t i = 0; i < len; i++){
        array_obj_values[i] = INT_VALUE(xoshiro256_next(&random->xos256));
    }

    return OBJ_VALUE(array_obj);
}

static Value native_random_floats(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeRandom *random = (NativeRandom *)VALUE_TO_NATIVE(target)->raw_native;

    size_t len = vm_utils_value_validate_len_arg(vm, args[0], 1, "length");

    ArrayObj *array_obj = vm_create_array(vm, (int64_t)len);
    Value *array_obj_values = array_obj->values;

    for (size_t i = 0; i < len; i++){
        uint64_t u64_value = xoshiro256_next(&random->xos256);
        double f64_value = ((double)u64_value) / ((double)UINT64_MAX);

        array_obj_values[i] = FLOAT_VALUE(f64_value == 1 ? 0.3142857143 : f64_value);
    }

    return OBJ_VALUE(array_obj);
}

static Value native_random_ints_range(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeRandom *random = (NativeRandom *)VALUE_TO_NATIVE(target)->raw_native;

    size_t len = vm_utils_value_validate_len_arg(vm, args[0], 1, "length");
    int64_t min = vm_utils_value_validate_int_arg(vm, args[1], 2, "min");
    int64_t max = vm_utils_value_validate_int_range_arg(vm, args[2], 3, "max", min, INT64_MAX);

    ArrayObj *array_obj = vm_create_array(vm, (int64_t)len);
    Value *array_obj_values = array_obj->values;

    for (size_t i = 0; i < len; i++){
        array_obj_values[i] = INT_VALUE(xoshiro256_next(&random->xos256) % (max - min + 1) + min);
    }

    return OBJ_VALUE(array_obj);
}

CREATE_VALIDATE_NATIVE_IMPLEMENTATION(NATIVE_RANDOM_NAME, native_random, RANDOM_ID, NativeRandom)

void native_random_init(NativeContext *context){
    RANDOM_ID = native_identificate(
        context,
        NATIVE_RANDOM_NAME,
        native_random_destroy,
        NULL,
        NULL,
        NULL
    );

    native_properties_add_native_fn(context, RANDOM_ID, "next_int",       0, native_random_next_int);
    native_properties_add_native_fn(context, RANDOM_ID, "next_float",     0, native_random_next_float);
    native_properties_add_native_fn(context, RANDOM_ID, "next_int_range", 2, native_random_next_int_range);
    native_properties_add_native_fn(context, RANDOM_ID, "bools",          1, native_random_bools);
    native_properties_add_native_fn(context, RANDOM_ID, "ints",           1, native_random_ints);
    native_properties_add_native_fn(context, RANDOM_ID, "floats",         1, native_random_floats);
    native_properties_add_native_fn(context, RANDOM_ID, "ints_range",     3, native_random_ints_range);
}

NativeRandom *native_random_create(NativeContext *context){
	const Allocator *allocator = native_allocator(context);
    NativeRandom *random = MEMORY_ALLOC(allocator, NativeRandom, 1);

    *random = (NativeRandom){
        .header = RANDOM_ID,
        .xos256 = xoshiro256_init()
    };

	return random;
}

NativeRandom *native_random_create_seed(NativeContext *context, int64_t seed){
	const Allocator *allocator = native_allocator(context);
    NativeRandom *random = MEMORY_ALLOC(allocator, NativeRandom, 1);

    *random = (NativeRandom){
        .header = RANDOM_ID,
        .xos256 = xoshiro256_init_seed(seed)
    };

	return random;
}