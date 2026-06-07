#ifndef NATIVE_MODULE_OS_H
#define NATIVE_MODULE_OS_H

#include "utils.h"

#include "vm/vm_factory.h"
#include "vm/vm_utils.h"

static char *os_name = OS_NAME;
static char *path_separator = (char[]){OS_PATH_SEPARATOR, 0};

NativeModule *os_native_module = NULL;

Value native_fn_os_name(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *str_obj = NULL;

    vm_create_str(
        vm,
        0,
        strlen(os_name),
        os_name,
        &str_obj
    );

    return OBJ_VALUE(str_obj);
}

Value native_fn_os_path_separator(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *str_obj = NULL;

    vm_create_str(
        vm,
        0,
        1,
        path_separator,
        &str_obj
    );

    return OBJ_VALUE(str_obj);
}

void os_module_init(const Allocator *allocator){
    os_native_module = vm_factory_native_module_create(allocator, "os");

    vm_factory_native_module_add_native_fn(os_native_module, "name", 0, native_fn_os_name);
    vm_factory_native_module_add_native_fn(os_native_module, "path_separator", 0, native_fn_os_path_separator);
}

#endif
