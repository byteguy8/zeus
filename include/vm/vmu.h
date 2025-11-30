#ifndef VM_UTILS_H
#define VM_UTILS_H

#include "native/native.h"
#include "obj.h"
#include "vm.h"
#include "essentials/lzbstr.h"
#include "native_fn.h"
#include "types_utils.h"
#include <stdio.h>

#define VMU_VM ((VM *)context)

#define VMU_VALUES_POOL (&(vm->values_pool))
#define VMU_STR_OBJS_POOL (&(vm->str_objs_pool))
#define VMU_ARRAY_OBJS_POOL (&(vm->array_objs_pool))
#define VMU_LIST_OBJS_POOL (&(vm->list_objs_pool))
#define VMU_DICT_OBJS_POOL (&(vm->dict_objs_pool))
#define VMU_RECORD_OBJS_POOL (&(vm->record_objs_pool))
#define VMU_FN_OBJS_POOL (&(vm->fn_objs_pool))
#define VMU_NATIVE_FN_OBJS_POOL (&(vm->native_fn_objs_pool))
#define VMU_CLOSURES_POOL (&(vm->closures_pool))
#define VMU_CLOSURE_OBJS_POOL (&(vm->closure_objs_pool))
#define VMU_NATIVE_MODULE_OBJS_POOL (&(vm->native_module_objs_pool))
#define VMU_MODULE_OBJS_POOL (&(vm->module_objs_pool))

#define VMU_FRONT_ALLOCATOR (&(vm->front_allocator))
#define VMU_NATIVE_FRONT_ALLOCATOR (&(((VM *)context)->front_allocator))

int vmu_error(VM *vm, char *msg, ...);
int vmu_internal_error(VM *vm, char *msg, ...);

size_t validate_idx(VM *vm, size_t len, int64_t idx);

uint8_t validate_value_bool_arg(Value value, uint8_t param, const char *name, VM *vm);
int64_t validate_value_int_arg(Value value, uint8_t param, const char *name, VM *vm);
double validate_value_float_arg(Value value, uint8_t param, const char *name, VM *vm);
double validate_value_ifloat_arg(Value value, uint8_t param, const char *name, VM *vm);
int64_t validate_value_int_range_arg(
	Value value,
	uint8_t param,
	const char *name,
	int64_t from,
	int64_t to,
	VM *vm
);
size_t validate_value_len_arg(Value value, uint8_t param, const char *name, VM *vm);
size_t validate_value_idx_arg(
	Value value,
	uint8_t param,
	const char *name,
	size_t len,
	VM *vm
);
StrObj *validate_value_str_arg(Value value, uint8_t param, const char *name, VM *vm);
ArrayObj *validate_value_array_arg(Value value, uint8_t param, const char *name, VM *vm);
RecordObj *validate_value_record_arg(Value value, uint8_t param, const char *name, VM *vm);

#define CREATE_VALIDATE_NATIVE_DECLARATION(_struct_name, _struct_type) \
	_struct_type *_struct_name##_validate_value_arg(Value value, uint8_t param, const char *name, VM *vm);

#define CREATE_VALIDATE_NATIVE(_name, _struct_name, _native_type, _struct_type)                          \
	_struct_type *_struct_name ## _validate_value_arg(Value value, uint8_t param, const char *name, VM *vm){ \
	    NativeObj *native_obj = is_value_native(value) ? VALUE_TO_NATIVE(value) : NULL;                      \
	    NativeHeader *native_header = native_obj ? (NativeHeader *)native_obj->native : NULL;                \
	    																				                     \
	    if(native_header && native_header->type == _native_type){                                            \
			return (_struct_type *)native_header;                                                            \
		}																						             \
																							                 \
		vmu_error(																		                     \
			vm,																				                 \
			"Illegal type of argument %" PRIu8 ": expect '%s' of type '%s'",                                 \
			param,                                                                                           \
	        name,																			                 \
			_name																			                 \
		);																				                     \
																							                 \
		return NULL;                                                                                         \
	}

void vmu_clean_up(VM *vm);
void vmu_gc(VM *vm);

Frame *vmu_current_frame(VM *vm);
#define VMU_CURRENT_FN(_vm)(vmu_current_frame(_vm)->fn)
#define VMU_CURRENT_MODULE(_vm)(VMU_CURRENT_FN(_vm)->module)

void vmu_value_to_str_w(Value value, LZBStr *str);
char *vmu_value_to_str(Value value, VM *vm, size_t *out_len);
char *vmu_value_to_json(
	unsigned int default_spaces,
	unsigned int spaces,
	Value value,
	VM *vm,
	size_t *out_len
);
void vmu_print_value(FILE *stream, Value value);

Value *vmu_clone_value(VM *vm, Value value);
void vmu_destroy_value(Value *value);
//----------------------------------------------------------------//
//                      OBJECTS MANIPULATION                      //
//----------------------------------------------------------------//
//---------------------------  STRING  ---------------------------//
int vmu_create_str(
	char runtime,
	size_t raw_str_len,
	char *raw_str,
	VM *vm,
	StrObj **out_str_obj
);
void vmu_destroy_str(StrObj *str_obj, VM *vm);
int vmu_str_is_int(StrObj *str_obj);
int vmu_str_is_float(StrObj *str_obj);
int64_t vmu_str_len(StrObj *str_obj);
StrObj *vmu_str_char(int64_t idx, StrObj *str_obj, VM *vm);
int64_t vmu_str_code(int64_t idx, StrObj *str_obj, VM *vm);
StrObj *vmu_str_concat(StrObj *a_str_obj, StrObj *b_str_obj, VM *vm);
StrObj *vmu_str_mul(int64_t by, StrObj *str_obj, VM *vm);
StrObj *vmu_str_insert_at(int64_t idx, StrObj *a, StrObj *b, VM *vm);
StrObj *vmu_str_remove(int64_t from, int64_t to, StrObj *str_obj, VM *vm);
StrObj *vmu_str_sub_str(int64_t from, int64_t to, StrObj *str_obj, VM *vm);
//---------------------------  ARRAY  ----------------------------//
ArrayObj *vmu_create_array(int64_t len, VM *vm);
void vmu_destroy_array(ArrayObj *array_obj, VM *vm);
int64_t vmu_array_len(ArrayObj *array_obj);
Value vmu_array_get_at(int64_t idx, ArrayObj *array_obj, VM *vm);
void vmu_array_set_at(int64_t idx, Value value, ArrayObj *array_obj, VM *vm);
Value vmu_array_first(ArrayObj *array_obj, VM *vm);
Value vmu_array_last(ArrayObj *array_obj, VM *vm);
ArrayObj *vmu_array_grow(int64_t by, ArrayObj *array_obj, VM *vm);
ArrayObj *vmu_array_join(ArrayObj *a_array_obj, ArrayObj *b_array_obj, VM *vm);
ArrayObj *vmu_array_join_value(Value value, ArrayObj *array_obj, VM *vm);
//----------------------------  LIST  ----------------------------//
ListObj *vmu_create_list(VM *vm);
void vmu_destroy_list(ListObj *list_obj, VM *vm);
int64_t vmu_list_len(ListObj *list_obj);
int64_t vmu_list_clear(ListObj *list_obj);
ListObj *vmu_list_join(ListObj *a_list_obj, ListObj *b_list_obj, VM *vm);
Value vmu_list_get_at(int64_t idx, ListObj *list_obj, VM *vm);
void vmu_list_insert(Value value, ListObj *list_obj, VM *vm);
ListObj *vmu_list_insert_new(Value value, ListObj *list_obj, VM *vm);
void vmu_list_insert_at(int64_t idx, Value value, ListObj *list_obj, VM *vm);
Value vmu_list_set_at(int64_t idx, Value value, ListObj *list_obj, VM *vm);
Value vmu_list_remove_at(int64_t idx, ListObj *list_obj, VM *vm);
//----------------------------  DICT  ----------------------------//
DictObj *vmu_create_dict(VM *vm);
void vmu_destroy_dict(DictObj *dict_obj, VM *vm);
void vmu_dict_put(Value key, Value value, DictObj *dict_obj, VM *vm);
void vmu_dict_put_cstr_value(const char *str, Value value, DictObj *dict_obj, VM *vm);
int vmu_dict_contains(Value key, DictObj *dict_obj);
Value vmu_dict_get(Value key, DictObj *dict_obj, VM *vm);
void vmu_dict_remove(Value key, DictObj *dict_obj);
//---------------------------  RECORD  ---------------------------//
RecordObj *vmu_create_record(uint16_t length, VM *vm);
void vmu_destroy_record(RecordObj *record_obj, VM *vm);
void vmu_record_insert_attr(
	size_t key_size,
	char *key,
	Value value,
	RecordObj *record_obj,
	VM *vm
);
void vmu_record_set_attr(
	size_t key_size,
	char *key,
	Value value,
	RecordObj *record_obj,
	VM *vm
);
Value vmu_record_get_attr(size_t key_size, char *key, RecordObj *record_obj, VM *vm);
//---------------------------  NATIVE  ---------------------------//
NativeObj *vmu_create_native(void *native, VM *vm);
void vmu_destroy_native(NativeObj *native_obj, VM *vm);
//-------------------------  NATIVE FN  --------------------------//
NativeFnObj *vmu_create_native_fn(Value target, NativeFn *native_fn, VM *vm);
void vmu_destroy_native_fn(NativeFnObj *native_fn_obj, VM *vm);
//-----------------------------  FN  -----------------------------//
FnObj *vmu_create_fn(Fn *fn, VM *vm);
void vmu_destroy_fn(FnObj *fn_obj, VM *vm);
//--------------------------  CLOSURE  ---------------------------//
ClosureObj *vmu_create_closure(MetaClosure *meta, VM *vm);
void vmu_destroy_closure(ClosureObj *closure_obj, VM *vm);
//-----------------------  NATIVE MODULE  ------------------------//
NativeModuleObj *vmu_create_native_module(NativeModule *native_module, VM *vm);
void vmu_destroy_native_module_obj(NativeModuleObj *native_module_obj, VM *vm);
//---------------------------  MODULE  ---------------------------//
ModuleObj *vmu_create_module_obj(Module *module, VM *vm);
void vmu_destroy_module_obj(ModuleObj *module_obj, VM *vm);

#endif
