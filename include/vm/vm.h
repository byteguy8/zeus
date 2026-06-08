#ifndef VM_H
#define VM_H

#include "vm_types.h"
#include "native.h"
#include "module.h"
#include "memory.h"

#include <setjmp.h>

typedef enum vm_result{
    OK_VM_RESULT,
    ERR_VM_RESULT,
}VMResult;

typedef struct frame Frame;

VM *vm_create(Allocator *allocator);
void vm_destroy(VM *vm);

void vm_initialize(VM *vm);
int vm_execute(LZOHTable *native_fns, Module *module, VM *vm);

void vm_error(VM *vm, const char *msg, ...);
void vm_throw(VM *vm, Value value);
void vm_throw_raw(VM *vm, const char *format, ...);

void vm_gc(VM *vm);
Allocator *vm_allocator(VM *vm);
NativeContext *vm_native_context(VM *vm);

size_t vm_unit_reservate(VM *vm, size_t size);
void *vm_unit_alloc(VM *vm, size_t id);
void vm_unit_dealloc(void *ptr);

void vm_halt(VM *vm);
void vm_exit(VM *vm, unsigned char code);
void vm_add_native_symbols(VM *vm, LZOHTable *symbols);

//----------------------------------------------------------------//
//                      OBJECTS MANIPULATION                      //
//----------------------------------------------------------------//
//---------------------------  STRING  ---------------------------//
int vm_create_str(
    VM *vm,
    char runtime,
	size_t raw_str_len,
	char *raw_str,
	StrObj **out_str_obj
);
StrObj *vm_create_str_raw(VM *vm, const char *format, ...);
int vm_str_is_int(StrObj *str_obj);
int vm_str_is_float(StrObj *str_obj);
int64_t vm_str_len(StrObj *str_obj);
StrObj *vm_str_char(VM *vm, int64_t idx, StrObj *str_obj);
int64_t vm_str_code(VM *vm, int64_t idx, StrObj *str_obj);
StrObj *vm_str_concat(VM *vm, StrObj *a_str_obj, StrObj *b_str_obj);
StrObj *vm_str_mul(VM *vm, int64_t by, StrObj *str_obj);
StrObj *vm_str_insert_at(VM *vm, int64_t idx, StrObj *a, StrObj *b);
StrObj *vm_str_remove(VM *vm, int64_t from, int64_t to, StrObj *str_obj);
StrObj *vm_str_sub_str(VM *vm, int64_t from, int64_t to, StrObj *str_obj);
//---------------------------  ARRAY  ----------------------------//
ArrayObj *vm_create_array(VM *vm, int64_t len);
int64_t vm_array_len(ArrayObj *array_obj);
Value vm_array_get_at(VM *vm, int64_t idx, ArrayObj *array_obj);
void vm_array_set_at(VM *vm, int64_t idx, Value value, ArrayObj *array_obj);
Value vm_array_first(VM *vm, ArrayObj *array_obj);
Value vm_array_last(VM *vm, ArrayObj *array_obj);
ArrayObj *vm_array_grow(VM *vm, int64_t by, ArrayObj *array_obj);
ArrayObj *vm_array_join(VM *vm, ArrayObj *a_array_obj, ArrayObj *b_array_obj);
ArrayObj *vm_array_join_value(VM *vm, Value value, ArrayObj *array_obj);
//----------------------------  LIST  ----------------------------//
ListObj *vm_create_list(VM *vm);
int64_t vm_list_len(ListObj *list_obj);
int64_t vm_list_clear(ListObj *list_obj);
ListObj *vm_list_join(VM *vm, ListObj *a_list_obj, ListObj *b_list_obj);
Value vm_list_get_at(VM *vm, int64_t idx, ListObj *list_obj);
void vm_list_insert(VM *vm, Value value, ListObj *list_obj);
ListObj *vm_list_insert_new(VM *vm, Value value, ListObj *list_obj);
void vm_list_insert_at(VM *vm, int64_t idx, Value value, ListObj *list_obj);
Value vm_list_set_at(VM *vm, int64_t idx, Value value, ListObj *list_obj);
Value vm_list_remove_at(VM *vm, int64_t idx, ListObj *list_obj);
//----------------------------  DICT  ----------------------------//
DictObj *vm_create_dict(VM *vm);
void vm_dict_put(VM *vm, Value key, Value value, DictObj *dict_obj);
void vm_dict_put_cstr_value(VM *vm, const char *str, Value value, DictObj *dict_obj);
int vm_dict_contains(Value key, DictObj *dict_obj);
Value vm_dict_get(VM *vm, Value key, DictObj *dict_obj);
void vm_dict_remove(Value key, DictObj *dict_obj);
//---------------------------  RECORD  ---------------------------//
RecordObj *vm_create_record(VM *vm, uint16_t length);
void vm_record_insert_attr(
    VM *vm,
	size_t key_size,
	char *key,
	Value value,
	RecordObj *record_obj
);
void vm_record_set_attr(
    VM *vm,
	size_t key_size,
	char *key,
	Value value,
	RecordObj *record_obj
);
Value vm_record_get_attr(VM *vm, size_t key_size, char *key, RecordObj *record_obj);
//---------------------------  NATIVE  ---------------------------//
NativeObj *vm_create_native(VM *vm, void *native);
//-------------------------  NATIVE FN  --------------------------//
NativeFnObj *vm_create_native_fn(VM *vm, Value target, NativeFn *native_fn);
//-----------------------------  FN  -----------------------------//
FnObj *vm_create_fn(VM *vm, Fn *fn);
//--------------------------  CLOSURE  ---------------------------//
ClosureObj *vm_create_closure(VM *vm, ClosureInf *closure);
//-----------------------  NATIVE MODULE  ------------------------//
NativeModuleObj *vm_create_native_module(VM *vm, NativeModule *native_module);
//---------------------------  MODULE  ---------------------------//
ModuleObj *vm_create_module_obj(VM *vm, Module *module);

#endif
