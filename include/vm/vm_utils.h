#ifndef VM_UTILS_H
#define VM_UTILS_H

#include "essentials/lzbstr.h"

#include "types_utils.h"
#include "native.h"
#include "obj.h"
#include "native_fn.h"
#include "vm.h"

#include <stdio.h>

size_t vm_utils_validate_idx(VM *vm, size_t len, int64_t idx);

int64_t vm_utils_value_validate_int_range(VM *vm, Value value, int64_t from, int64_t to, const char *err_msg);

uint8_t vm_utils_value_validate_bool_arg(VM *vm, Value value, uint8_t param, const char *name);
int64_t vm_utils_value_validate_int_arg(VM *vm, Value value, uint8_t param, const char *name);
int64_t vm_utils_value_validate_int_range_arg(VM *vm, Value value, uint8_t param, const char *name, int64_t from, int64_t to);
double vm_utils_value_validate_ifloat_arg(VM *vm, Value value, uint8_t param, const char *name);
size_t vm_utils_value_validate_idx_range_arg(VM *vm, Value value, uint8_t param, const char *name, size_t from, size_t to);
size_t vm_utils_value_validate_len_arg(VM *vm, Value value, uint8_t param, const char *name);
StrObj *vm_utils_value_validate_str_arg(VM *vm, Value value, uint8_t param, const char *name);

void vm_utils_value_to_str_w(VM *vm, Value value, LZBStr *str);
char *vm_utils_value_to_str(VM *vm, Value value, size_t *out_len);
char *vm_utils_value_to_json(unsigned int default_spaces, unsigned int spaces, Value value, VM *vm, size_t *out_len);
void vm_utils_print_value(VM *vm, FILE *stream, Value value);

#endif
