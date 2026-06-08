#include "vm_utils.h"

#include "essentials/lzbstr.h"
#include "essentials/memory.h"

#include "fn.h"
#include "obj.h"
#include "types_utils.h"
#include "native.h"
#include "vm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <assert.h>
#include <limits.h>
#include <string.h>

typedef struct pass_value{
    Obj *obj;
    struct pass_value *prev;
    struct pass_value *next;
}PassValue;

static const int64_t IndexableMaxValue =
	SIZE_MAX < INT64_MAX ? (int64_t)SIZE_MAX : INT64_MAX;

//----------------------------------------------------------------//
//                       PRIVATE INTERFACE                        //
//----------------------------------------------------------------//
//---------------------------  OTHERS  ---------------------------//
static void obj_to_str(VM *vm, PassValue pass, Obj *obj, LZBStr *str);
static void value_to_str(VM *vm, PassValue pass, Value value, LZBStr *str);
static void obj_to_json(
    unsigned int default_spaces,
    unsigned int spaces,
    PassValue pass,
    Obj *obj,
    LZBStr *str,
    VM *vm
);
static void value_to_json(
    unsigned int default_spaces,
    unsigned int spaces,
    PassValue pass,
    Value value,
    LZBStr *str,
    VM *vm
);
//----------------------------------------------------------------//
//                     PRIVATE IMPLEMENTATION                     //
//----------------------------------------------------------------//
void obj_to_str(VM *vm, PassValue pass, Obj *obj, LZBStr *str){
    PassValue *current = &pass;
    PassValue *prev = NULL;

    while(current){
        prev = current->prev;

        if(current->next && current->obj == obj){
            lzbstr_append(str, "...");
            return;
        }

        current = prev;
    }

    switch (obj->type){
        case STR_OBJ_TYPE:{
            StrObj *str_obj = OBJ_TO_STR(obj);
            lzbstr_append(str, str_obj->buff);
            break;
        }case ARRAY_OBJ_TYPE:{
            ArrayObj *array_obj = OBJ_TO_ARRAY(obj);
            size_t len = array_obj->len;
            Value *values = array_obj->values;

            lzbstr_append(str, "[");

            for (size_t i = 0; i < len; i++){
                Value value = values[i];

                if(is_value_str(value)){
                    lzbstr_append(str, "'");
                    value_to_str(vm, pass, value, str);
                    lzbstr_append(str, "'");
                }else{
                    value_to_str(vm, pass, value, str);
                }

                if(i + 1 < len){
                    lzbstr_append(str, ", ");
                }
            }

            lzbstr_append(str, "]");

            break;
        }case LIST_OBJ_TYPE:{
            ListObj *list_obj = OBJ_TO_LIST(obj);
            DynArr *items = list_obj->items;
            size_t len = dynarr_len(items);

            lzbstr_append(str, "(");

            for (size_t i = 0; i < len; i++){
                Value value = DYNARR_GET_AS(items, Value, i);

                if(is_value_str(value)){
                    lzbstr_append(str, "'");
                    value_to_str(vm, pass, value, str);
                    lzbstr_append(str, "'");
                }else{
                    value_to_str(vm, pass, value, str);
                }

                if(i + 1 < len){
                    lzbstr_append(str, ", ");
                }
            }

            lzbstr_append(str, ")");

            break;
        }case DICT_OBJ_TYPE:{
            DictObj *dict_obj = OBJ_TO_DICT(obj);
            LZOHTable *key_values = dict_obj->key_values;

            size_t count = 0;
            size_t m = key_values->m;
            size_t n = key_values->n;

            lzbstr_append(str, "{");

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = key_values->slots[i];

                if(!slot.used){
                    continue;
                }

                Value key = *(Value *)slot.key;
                Value value = *(Value *)slot.value;

                if(is_value_str(key)){
                    lzbstr_append(str, "'");
                    value_to_str(vm, pass, key, str);
                    lzbstr_append(str, "'");
                }else{
                    value_to_str(vm, pass, key, str);
                }

                lzbstr_append(str, ": ");

                if(is_value_str(value)){
                    lzbstr_append(str, "'");
                    value_to_str(vm, pass, value, str);
                    lzbstr_append(str, "'");
                }else{
                    value_to_str(vm, pass, value, str);
                }

                if(count + 1 < n){
                    lzbstr_append(str, ", ");
                }

                count++;
            }

            lzbstr_append(str, "}");

            break;
        }case RECORD_OBJ_TYPE:{
            RecordObj *record_obj = OBJ_TO_RECORD(obj);
            LZOHTable *attrs = record_obj->attrs;

            size_t count = 0;
            size_t n = attrs->n;
            size_t m = attrs->m;
            LZOHTableSlot *slots = attrs->slots;

            lzbstr_append(str, "{");

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = slots[i];

                if(!slot.used){
                    continue;
                }

                char *key = (char *)slot.key;
                Value value = *(Value *)slot.value;

                lzbstr_append_args(str, "%.*s: ", slot.key_size, key);

                if(is_value_str(value)){
                    lzbstr_append(str, "'");
                    value_to_str(vm, pass, value, str);
                    lzbstr_append(str, "'");
                }else{
                    value_to_str(vm, pass, value, str);
                }

                if(count + 1 < n){
                    lzbstr_append(str, ", ");
                }

                count++;
            }

            lzbstr_append(str, "}");

            break;
        }case NATIVE_OBJ_TYPE:{
       		NativeObj *native_obj = OBJ_TO_NATIVE(obj);
            NativeContext *context = vm_native_context(vm);
            native_t native = *(native_t *)native_obj->raw_native;
            const char *native_type_name = native_type_get_name(context, native);

        	lzbstr_append_args(str, "<native '%s'>", native_type_name);

       		break;
        }case NATIVE_FN_OBJ_TYPE:{
            NativeFnObj *native_fn_obj = OBJ_TO_NATIVE_FN(obj);
            NativeFn *native_fn = native_fn_obj->native_fn;

            lzbstr_append_args(str, "<native function '%s' %" PRIu8 " %p>", native_fn->name, native_fn->arity, native_fn_obj);

            break;
        }case FN_OBJ_TYPE:{
            FnObj *fn_obj = OBJ_TO_FN(obj);
            const Fn *fn = fn_obj->fn;

            lzbstr_append_args(str, "<function '%s' %zu %p>", fn->name, fn->arity, fn_obj);

            break;
        }case CLOSURE_OBJ_TYPE:{
            ClosureObj *closure_obj = OBJ_TO_CLOSURE(obj);

            lzbstr_append_args(str, "<closure %p>", closure_obj);

            break;
        }case NATIVE_MODULE_OBJ_TYPE:{
            NativeModuleObj *native_module_obj = OBJ_TO_NATIVE_MODULE(obj);
            NativeModule *native_module = native_module_obj->native_module;
            LZOHTable *symbols = native_module->symbols;
            LZOHTableSlot *slots = symbols->slots;
            size_t m = symbols->m;
            size_t n = symbols->n;
            size_t count = 0;

            lzbstr_append(str, "{");

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = slots[i];

                if(!slot.used){
                    continue;
                }

                count++;

                char *name = (char *)slot.key;
                Value value = *(Value *)slot.value;

                lzbstr_append_args(str, "%s: ", name);
                value_to_str(vm, pass, value, str);

                if(count < n){
                    lzbstr_append(str, ", ");
                }
            }

            lzbstr_append(str, "}");

            break;
        }case MODULE_OBJ_TYPE:{
            ModuleObj *module_obj = OBJ_TO_MODULE(obj);
            Module *module = module_obj->module;
            ModuleContext *module_context = module->context;
            LZOHTable *globals = module_context->globals;
            LZOHTableSlot *slots = globals->slots;
            size_t n = globals->n;
            size_t m = globals->m;

            lzbstr_append(str, "{");

            for (size_t i = 0, u = 0; i < m && u < n; i++){
                LZOHTableSlot slot = slots[i];

                if(!slot.used){
                    continue;
                }

                u++;

                char *global_name = slot.key;
                GlobalValue *global_value = slot.value;
                Value value = global_value->value;

                if(global_value->access == PRIVATE_GLOBAL_VALUE_TYPE){
                    continue;
                }

                lzbstr_append_args(str, "%s: ", global_name);
                value_to_str(vm, pass, value, str);

                if(u + 1 < n){
                	lzbstr_append(str, ", ");
                }
            }

            lzbstr_append(str, "}");

            break;
        }default:{
            assert(0 && "Illegal object type");

            break;
        }
    }
}

void value_to_str(VM *vm, PassValue pass, Value value, LZBStr *str){
    switch (value.type){
        case EMPTY_VALUE_TYPE:{
            lzbstr_append(str, "empty");

            break;
        }case BOOL_VALUE_TYPE:{
            uint8_t bool_value = VALUE_TO_BOOL(value);

            lzbstr_append_args(str, "%s", bool_value ? "true" : "false");

            break;
        }case INT_VALUE_TYPE:{
            int64_t int_value = VALUE_TO_INT(value);

            lzbstr_append_args(str, "%" PRId64, int_value);

            break;
        }case FLOAT_VALUE_TYPE:{
            double float_value = VALUE_TO_FLOAT(value);

            lzbstr_append_args(str, "%.6f", float_value);

            break;
        }case OBJ_VALUE_TYPE:{
            Obj *obj = VALUE_TO_OBJ(value);

            if(pass.obj){
                PassValue next_pass = {
                    .obj = obj,
                    .prev = &pass,
                    .next = NULL
                };

                pass.next = &next_pass;

                obj_to_str(vm, next_pass, obj, str);

                next_pass.prev = NULL;
                pass.next = NULL;
            }else{
                pass.obj = obj;

                obj_to_str(vm, pass, obj, str);
            }

            break;
        }default:{
            assert(0 && "Illegal value type");

            break;
        }
    }
}

void obj_to_json(
    unsigned int default_spaces,
    unsigned int spaces,
    PassValue pass,
    Obj *obj,
    LZBStr *str,
    VM *vm
){
    PassValue *current = &pass;
    PassValue *prev = NULL;

    while(current){
        prev = current->prev;

        if(current->next && current->obj == obj){
            lzbstr_append(str, "...");
            return;
        }

        current = prev;
    }

    switch (obj->type){
        case STR_OBJ_TYPE:{
            StrObj *str_obj = OBJ_TO_STR(obj);
            lzbstr_append(str, str_obj->buff);
            break;
        }case ARRAY_OBJ_TYPE:{
            ArrayObj *array_obj = OBJ_TO_ARRAY(obj);
            size_t len = array_obj->len;
            Value *values = array_obj->values;

            lzbstr_append(str, "[");

            for (size_t i = 0; i < len; i++){
                Value value = values[i];

                if(is_value_str(value)){
                    lzbstr_append(str, "\"");
                    value_to_json(default_spaces, spaces, pass, value, str, vm);
                    lzbstr_append(str, "\"");
                }else{
                    value_to_json(default_spaces, spaces, pass, value, str, vm);
                }

                if(i + 1 < len){
                    lzbstr_append(str, ", ");
                }
            }

            lzbstr_append(str, "]");

            break;
        }case LIST_OBJ_TYPE:{
            ListObj *list_obj = OBJ_TO_LIST(obj);
            DynArr *items = list_obj->items;
            size_t len = dynarr_len(items);

            lzbstr_append(str, "[");

            for (size_t i = 0; i < len; i++){
                Value value = DYNARR_GET_AS(items, Value, i);

                if(is_value_str(value)){
                    lzbstr_append(str, "\"");
                    value_to_json(default_spaces, spaces, pass, value, str, vm);
                    lzbstr_append(str, "\"");
                }else{
                    value_to_json(default_spaces, spaces, pass, value, str, vm);
                }

                if(i + 1 < len){
                    lzbstr_append(str, ", ");
                }
            }

            lzbstr_append(str, "]");

            break;
        }case DICT_OBJ_TYPE:{
            DictObj *dict_obj = OBJ_TO_DICT(obj);
            LZOHTable *key_values = dict_obj->key_values;

            size_t count = 0;
            size_t m = key_values->m;
            size_t n = key_values->n;

            lzbstr_append(str, "{\n");

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = key_values->slots[i];

                if(!slot.used){
                    continue;
                }

                Value key = *(Value *)slot.key;
                Value value = *(Value *)slot.value;

                lzbstr_append_args(str, "%*s\"", spaces + default_spaces, "");
                value_to_json(default_spaces, spaces, pass, key, str, vm);
                lzbstr_append(str, "\"");

                lzbstr_append(str, ": ");

                if(is_value_str(value)){
                    lzbstr_append(str, "\"");
                    value_to_json(default_spaces, spaces, pass, value, str, vm);
                    lzbstr_append(str, "\"");
                }else{
                    value_to_json(default_spaces, spaces + default_spaces, pass, value, str, vm);
                }

                if(count + 1 < n){
                    lzbstr_append(str, ",\n");
                }

                count++;
            }

            lzbstr_append_args(str, "\n%*s}", spaces, "");

            break;
        }case RECORD_OBJ_TYPE:{
            RecordObj *record_obj = OBJ_TO_RECORD(obj);
            LZOHTable *attrs = record_obj->attrs;

            size_t count = 0;
            size_t n = attrs->n;
            size_t m = attrs->m;
            LZOHTableSlot *slots = attrs->slots;

            lzbstr_append(str, "{\n");

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = slots[i];

                if(!slot.used){
                    continue;
                }

                char *key = (char *)slot.key;
                Value value = *(Value *)slot.value;

                lzbstr_append_args(str, "%*s\"%.*s\": ", spaces + default_spaces, "", slot.key_size, key);

                if(is_value_str(value)){
                    lzbstr_append(str, "\"");
                    value_to_json(default_spaces, spaces, pass, value, str, vm);
                    lzbstr_append(str, "\"");
                }else{
                    value_to_json(default_spaces, spaces + default_spaces, pass, value, str, vm);
                }

                if(count + 1 < n){
                    lzbstr_append(str, ",\n");
                }

                count++;
            }

            lzbstr_append_args(str, "\n%*s}", spaces, "");

            break;
        }case NATIVE_OBJ_TYPE:{
       		NativeObj *native_obj = OBJ_TO_NATIVE(obj);
            NativeContext *context = vm_native_context(vm);
            native_t native = *(native_t *)native_obj->raw_native;
            const char *native_type_name = native_type_get_name(context, native);

       		lzbstr_append_args(str, "<native '%s' at %p>", native_type_name, native_obj);

       		break;
        }case NATIVE_FN_OBJ_TYPE:{
            NativeFnObj *native_fn_obj = OBJ_TO_NATIVE_FN(obj);
            NativeFn *native_fn = native_fn_obj->native_fn;

            lzbstr_append_args(str, "<native function '%s' %" PRIu8 ">", native_fn->name, native_fn->arity);

            break;
        }case FN_OBJ_TYPE:{
            FnObj *fn_obj = OBJ_TO_FN(obj);
            const Fn *fn = fn_obj->fn;

            lzbstr_append_args(str, "<function '%s' %zu>", fn->name, fn->arity);

            break;
        }case CLOSURE_OBJ_TYPE:{
            ClosureObj *closure_obj = OBJ_TO_CLOSURE(obj);
            Fn *fn = closure_obj->closure.inf.fn;

            lzbstr_append_args(str, "<closure %zu>", fn->arity);

            break;
        }case NATIVE_MODULE_OBJ_TYPE:{
            NativeModuleObj *native_module_obj = OBJ_TO_NATIVE_MODULE(obj);
            NativeModule *native_module = native_module_obj->native_module;
            LZOHTable *symbols = native_module->symbols;
            LZOHTableSlot *slots = symbols->slots;
            size_t m = symbols->m;
            size_t n = symbols->n;
            size_t count = 0;

            lzbstr_append(str, "{\n");

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = slots[i];

                if(!slot.used){
                    continue;
                }

                count++;

                char *name = (char *)slot.key;
                Value value = *(Value *)slot.value;

                lzbstr_append_args(str, "%*s\"%.*s\": ", spaces + default_spaces, "", slot.key_size, name);
                value_to_json(default_spaces, spaces + default_spaces, pass, value, str, vm);

                if(count < n){
                    lzbstr_append(str, ",\n");
                }
            }

            lzbstr_append_args(str, "\n%*s}", spaces, "");

            break;
        }case MODULE_OBJ_TYPE:{
            ModuleObj *module_obj = OBJ_TO_MODULE(obj);
            Module *module = module_obj->module;
            ModuleContext *module_context = module->context;
            LZOHTable *globals = module_context->globals;
            LZOHTableSlot *slots = globals->slots;
            size_t m = globals->m;
            size_t n = globals->n;
            size_t count = 0;

            lzbstr_append(str, "{\n");

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = slots[i];

                if(!slot.used){
                    continue;
                }

                char *name = (char *)slot.key;
                GlobalValue global_value = *(GlobalValue *)slot.value;

                if(global_value.access == PRIVATE_GLOBAL_VALUE_TYPE){
                    continue;
                }

                lzbstr_append_args(str, "%*s\"%s\": ", spaces + default_spaces, "", name);
                value_to_json(default_spaces, spaces + default_spaces, pass, global_value.value, str, vm);

                if(count += 1 < n){
                    lzbstr_append(str, ",\n");
                }
            }

            lzbstr_append_args(str, "\n%*s}", spaces, "");

            break;
        }default:{
            assert(0 && "Illegal object type");
        }
    }
}

void value_to_json(
    unsigned int default_spaces,
    unsigned int spaces,
    PassValue pass,
    Value value,
    LZBStr *str,
    VM *vm
){
    switch (value.type){
        case EMPTY_VALUE_TYPE:{
            lzbstr_append(str, "null");
            break;
        }case BOOL_VALUE_TYPE:{
            uint8_t bool_value = VALUE_TO_BOOL(value);
            lzbstr_append_args(str, "%s", bool_value ? "true" : "false");
            break;
        }case INT_VALUE_TYPE:{
            int64_t int_value = VALUE_TO_INT(value);
            lzbstr_append_args(str, "%" PRId64, int_value);
            break;
        }case FLOAT_VALUE_TYPE:{
            double float_value = VALUE_TO_FLOAT(value);
            lzbstr_append_args(str, "%g", float_value);
            break;
        }case OBJ_VALUE_TYPE:{
            Obj *obj = VALUE_TO_OBJ(value);

            if(pass.obj){
                PassValue next_pass = {
                    .obj = obj,
                    .prev = &pass,
                    .next = NULL
                };

                pass.next = &next_pass;

                obj_to_json(default_spaces, spaces, next_pass, obj, str, vm);

                next_pass.prev = NULL;
                pass.next = NULL;
            }else{
                pass.obj = obj;

                obj_to_json(default_spaces, spaces, pass, obj, str, vm);
            }

            break;
        }default:{
            assert(0 && "Illegal value type");
        }
    }
}
//----------------------------------------------------------------//
//                     PUBLIC IMPLEMENTATION                      //
//----------------------------------------------------------------//
size_t vm_utils_validate_idx(VM *vm, size_t len, int64_t idx){
	if(idx < 0 || idx > IndexableMaxValue){
		vm_error(
        	vm,
         	"Illegal index value: %"PRId64"",
            idx
        );
	}

	if(idx >= (int64_t)len){
		vm_error(
        	vm,
         	"Index (%"PRId64") out of bounds: must be less than length (%zu)",
            idx,
            len
        );
	}

	return (size_t)idx;
}

int64_t vm_utils_value_validate_int_range(VM *vm, Value value, int64_t from, int64_t to, const char *err_msg){
    if(!IS_VALUE_INT(value)){
        vm_error(
        	vm,
         	"%s: illegal type. Expect 'int'",
          	err_msg
        );
    }

    int64_t i64_value = VALUE_TO_INT(value);

    if(i64_value < from){
        vm_error(
        	vm,
         	"%s: illegal value. Expect greater or equals to %" PRId64 ", but got %" PRId64,
            err_msg,
            from,
            i64_value
        );
    }

    if(i64_value > to){
        vm_error(
        	vm,
         	"%s: Illegal value. Expect less or equals to %" PRId64 ", but got %" PRId64,
          	err_msg,
            to,
            i64_value
        );
    }

    return i64_value;
}

inline uint8_t vm_utils_value_validate_bool_arg(VM *vm, Value value, uint8_t param, const char *name){
    if(!IS_VALUE_BOOL(value)){
        vm_error(
        	vm,
         	"Illegal type of argument %" PRIu8 ": expect '%s' of type 'bool'",
          	param,
           	name
        );
    }

    return VALUE_TO_BOOL(value);
}

inline int64_t vm_utils_value_validate_int_arg(VM *vm, Value value, uint8_t param, const char *name){
    if(!IS_VALUE_INT(value)){
        vm_error(
        	vm,
         	"Illegal type of argument %" PRIu8 ": expect '%s' of type 'int'",
          	param,
           	name
        );
    }

    return VALUE_TO_INT(value);
}

inline double vm_utils_value_validate_ifloat_arg(VM *vm, Value value, uint8_t param, const char *name){
    if(IS_VALUE_INT(value)){
        return (double)VALUE_TO_INT(value);
    }

    if(IS_VALUE_FLOAT(value)){
        return VALUE_TO_FLOAT(value);
    }

    vm_error(
    	vm,
     	"Illegal type of argument %" PRIu8 ": expect '%s' of type 'int' or 'float'",
      	param,
       	name
    );

    return -1;
}

inline int64_t vm_utils_value_validate_int_range_arg(VM *vm, Value value, uint8_t param, const char *name, int64_t from, int64_t to){
    if(!IS_VALUE_INT(value)){
        vm_error(
        	vm,
         	"Illegal type of argument %" PRIu8 ": expect '%s' of type 'int'",
          	param,
           	name
        );
    }

    int64_t i64_value = VALUE_TO_INT(value);

    if(i64_value < from){
        vm_error(
        	vm,
         	"Illegal value of argument %" PRIu8 ": expect '%s' be greater or equals to %" PRId64 ", but got %" PRId64,
          	param,
           	name,
            from,
            i64_value
        );
    }

    if(i64_value > to){
        vm_error(
        	vm,
         	"Illegal value of argument %" PRIu8 ": expect '%s' be less or equals to %" PRId64 ", but got %" PRId64,
          	param,
           	name,
            to,
            i64_value
        );
    }

    return i64_value;
}

inline size_t vm_utils_value_validate_len_arg(VM *vm, Value value, uint8_t param, const char *name){
	if(!IS_VALUE_INT(value)){
        vm_error(
        	vm,
         	"Illegal type of argument %" PRIu8 ": expect '%s' of type 'int'",
          	param,
           	name
        );
    }

	int64_t idx = VALUE_TO_INT(value);

	if(idx < 0 || idx > IndexableMaxValue){
		vm_error(
        	vm,
         	"Illegal value of argument %" PRIu8 ": expect '%s' bigger or equals to 0 and less THAN %"PRId64,
          	param,
           	name,
            IndexableMaxValue
        );
	}

	return (size_t)idx;
}

size_t vm_utils_value_validate_idx_range_arg(VM *vm, Value value, uint8_t param, const char *name, size_t from, size_t to){
    if(!IS_VALUE_INT(value)){
        vm_error(
        	vm,
         	"Illegal type of argument %" PRIu8 ": expect '%s' of type 'int'",
          	param,
           	name
        );
    }

	int64_t unvalidated_idx = VALUE_TO_INT(value);

	if(unvalidated_idx < 0 || unvalidated_idx > IndexableMaxValue){
		vm_error(
        	vm,
         	"Illegal value of argument %" PRIu8 " ( '%s' ): all indices must fall in range [%"PRId64", %"PRId64"), but got %"PRId64"",
          	param,
           	name,
            0,
            IndexableMaxValue,
            unvalidated_idx
        );
	}

    size_t validated_idx = (size_t)unvalidated_idx;

	if(validated_idx < from || validated_idx > to){
		vm_error(
        	vm,
         	"Illegal value of argument %" PRIu8 ": '%s' must fall in range [%zu, %zu), but got: %zu",
          	param,
           	name,
            from,
            to,
            validated_idx
        );
	}

	return validated_idx;
}

inline StrObj *vm_utils_value_validate_str_arg(VM *vm, Value value, uint8_t param, const char *name){
    if(!is_value_str(value)){
        vm_error(
        	vm,
         	"Illegal type of argument %" PRIu8 ": expect '%s' of type 'str'",
          	param,
           	name
        );
    }

    return VALUE_TO_STR(value);
}

inline void vm_utils_value_to_str_w(VM *vm, Value value, LZBStr *str){
    value_to_str(vm, (PassValue){0}, value, str);
}

char *vm_utils_value_to_str(VM *vm, Value value, size_t *out_len){
    char *str_value = NULL;
    LZBStr *str = MEMORY_LZBSTR(vm_allocator(vm));

    value_to_str(vm, (PassValue){0}, value, str);
    lzbstr_rclone_buff(
        str,
        (const LZBStrAllocator *)vm_allocator(vm),
        out_len,
        &str_value
    );
    lzbstr_destroy(str);

    return str_value;
}

char *vm_utils_value_to_json(unsigned int default_spaces, unsigned int spaces, Value value, VM *vm, size_t *out_len){
    char *str_value = NULL;
    LZBStr *str = MEMORY_LZBSTR(vm_allocator(vm));

    value_to_json(default_spaces, spaces, (PassValue){0}, value, str, vm);
    lzbstr_rclone_buff(
        str,
        (const LZBStrAllocator *)vm_allocator(vm),
        out_len,
        &str_value
    );
    lzbstr_destroy(str);

    return str_value;
}

void vmu_print_obj(VM *vm, FILE *stream, Obj *object){
    switch (object->type){
        case STR_OBJ_TYPE:{
            StrObj *str = OBJ_TO_STR(object);

            fprintf(stream, "%s", str->buff);

            break;
        }case ARRAY_OBJ_TYPE:{
			ArrayObj *array = OBJ_TO_ARRAY(object);

            fprintf(stream, "<array %zu at %p>", array->len, array);

            break;
		}case LIST_OBJ_TYPE:{
			ListObj *list_obj = OBJ_TO_LIST(object);
            DynArr *list = list_obj->items;
            size_t len = dynarr_len(list);

            fprintf(stream, "<list %zu at %p>", len, list);

            break;
		}case DICT_OBJ_TYPE:{
            DictObj *dict_obj = OBJ_TO_DICT(object);
            LZOHTable *dict = dict_obj->key_values;

            fprintf(stream, "<dict %zu at %p>", dict->n, dict);

            break;
        }case RECORD_OBJ_TYPE:{
			RecordObj *record_obj = OBJ_TO_RECORD(object);

            fprintf(stream, "<record %zu at %p>", record_obj->attrs ? record_obj->attrs->n : 0, record_obj);

            break;
		}case NATIVE_OBJ_TYPE:{
			NativeObj *native_obj = OBJ_TO_NATIVE(object);
            NativeContext *context = vm_native_context(vm);
            native_t native = *(native_t *)native_obj->raw_native;
            const char *native_type_name = native_type_get_name(context, native);

			fprintf(stream, "<native '%s' at %p>", native_type_name, native_obj);

			break;
		}case NATIVE_FN_OBJ_TYPE:{
            NativeFnObj *native_fn_obj = OBJ_TO_NATIVE_FN(object);
            NativeFn *native_fn = native_fn_obj->native_fn;

            fprintf(stream, "<native procedure '%s' - %d at %p>", native_fn->name, native_fn->arity, native_fn);

            break;
        }case FN_OBJ_TYPE:{
            FnObj *fn_obj = OBJ_TO_FN(object);
            const Fn *fn = fn_obj->fn;

            fprintf(stream, "<procedure '%s' - %d at %p>", fn->name, fn->arity, fn);

            break;
        }case CLOSURE_OBJ_TYPE:{
            ClosureObj *closure_obj = OBJ_TO_CLOSURE(object);
            Fn *fn = closure_obj->closure.inf.fn;

            fprintf(stream, "<closure '%s' - %d at %p>", fn->name, fn->arity, fn);

            break;
        }case NATIVE_MODULE_OBJ_TYPE:{
            NativeModuleObj *native_module_obj = OBJ_TO_NATIVE_MODULE(object);
            NativeModule *module = native_module_obj->native_module;

            fprintf(stream, "<native module '%s' at %p>", module->name, module);

            break;
        }case MODULE_OBJ_TYPE:{
            ModuleObj *module_obj = OBJ_TO_MODULE(object);
            Module *module = module_obj->module;
            ModuleContext *context = module->context;

            fprintf(stream, "<module '%s' from '%s' at %p>", module->name, context->pathname, module);

            break;
        }default:{
            assert("Illegal object type");

            break;
        }
    }
}

void vm_utils_print_value(VM *vm, FILE *stream, Value value){
    switch (value.type){
        case EMPTY_VALUE_TYPE:{
            fprintf(stream, "empty");

            break;
        }case BOOL_VALUE_TYPE:{
            uint8_t bool = VALUE_TO_BOOL(value);

            fprintf(stream, "%s", bool == 0 ? "false" : "true");

            break;
        }case INT_VALUE_TYPE:{
            fprintf(stream, "%" PRId64, VALUE_TO_INT(value));

            break;
        }case FLOAT_VALUE_TYPE:{
			fprintf(stream, "%.8f", VALUE_TO_FLOAT(value));

            break;
		}case OBJ_VALUE_TYPE:{
            vmu_print_obj(vm, stream, VALUE_TO_OBJ(value));

            break;
        }default:{
            assert("Illegal value type");

            break;
        }
    }
}