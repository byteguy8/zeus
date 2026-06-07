#ifndef NATIVE_MODULE_DEFAULT_H
#define NATIVE_MODULE_DEFAULT_H

#include "vm/vm_utils.h"

#include "native/native_file.h"
#include "native/native_array.h"
#include "native/native_random.h"

#include <errno.h>
#include <stdarg.h>

static int match(size_t str_len, const char *str, size_t *current, size_t count, ...){
	if(*current >= str_len){
		return 0;
	}

	va_list args;
	va_start(args, count);

	for (size_t i = 0; i < count; i++){
		char c = (char)va_arg(args, int);

		if(str[*current] == c){
			(*current)++;
			va_end(args);

			return 1;
		}
	}

	va_end(args);

	return 0;
}

file_mode_t parse_file_mode(size_t str_mode_len, char *str_mode, VM *vm){
	if(str_mode_len == 0){
		vm_error(
			vm,
			"Illegal mode: empty"
		);
	}

	if(str_mode_len > 3){
		vm_error(
			vm,
			"Illegal mode: can only contains from 1 to 3 flags"
		);
	}

	size_t current = 0;
	file_mode_t mode = 0;

	if(match(str_mode_len, str_mode, &current, 3, 'r', 'w', 'a')){
		switch(str_mode[current - 1]) {
			case 'r':{
				mode |= NATIVE_FILE_READ_MODE;

                break;
			}case 'w':{
				mode |= NATIVE_FILE_WRITE_MODE;

                break;
			}case 'a':{
				mode |= NATIVE_FILE_APPEND_MODE;

                break;
			}
		}

		while(current < str_mode_len){
			if(match(str_mode_len, str_mode, &current, 2, '+', 'b')){
				switch(str_mode[current - 1]) {
					case '+':{
						if(mode & NATIVE_FILE_PLUS_MODE){
							vm_error(
								vm,
								"'+' flag is duplicated"
							);
						}

						mode |= NATIVE_FILE_PLUS_MODE;

						break;
					}case 'b':{
						if(mode & NATIVE_FILE_BINARY_MODE){
							vm_error(
								vm,
								"'b' flag is duplicated"
							);
						}

						mode |= NATIVE_FILE_BINARY_MODE;

						break;
					}
				}
			}else{
				vm_error(
					vm,
					"Unexpected flag '%c' at index %zu",
					str_mode[current],
					current
				);
			}
		}
	}else{
		vm_error(
			vm,
			"Unknown flag: '%c'",
			str_mode[current]
		);
	}

	return mode;
}

Value native_fn_exit(VM *vm, uint8_t argsc, Value *values, Value target){
    int64_t exit_code = vm_utils_value_validate_int_range_arg(vm, values[0], 1, "exit code", 0, 255);

    vm_exit(vm, (unsigned char)exit_code);

	return EMPTY_VALUE;
}

Value native_fn_assert(VM *vm, uint8_t argsc, Value *values, Value target){
    uint8_t value = vm_utils_value_validate_bool_arg(vm, values[0], 1, "assertion");

    if(!value){
        vm_error(vm, "ASSERTION FAILED");
    }

	return EMPTY_VALUE;
}

Value native_fn_assertm(VM *vm, uint8_t argsc, Value *values, Value target){
    uint8_t value = vm_utils_value_validate_bool_arg(vm, values[0], 1, "assertion");
    StrObj *str_obj = vm_utils_value_validate_str_arg(vm, values[1], 2, "message");

    if(!value){
        vm_error(vm, "%s", str_obj->buff);
    }

	return EMPTY_VALUE;
}

Value native_fn_is_str_int(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *str_obj = vm_utils_value_validate_str_arg(vm, values[0], 1, "string");
    return BOOL_VALUE((uint8_t)vm_str_is_int(str_obj));
}

Value native_fn_is_str_float(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *str_obj = vm_utils_value_validate_str_arg(vm, values[0], 1, "string");
    return BOOL_VALUE((uint8_t)vm_str_is_float(str_obj));
}

Value native_fn_to_str(VM *vm, uint8_t argsc, Value *values, Value target){
    size_t len;
    char *raw_str = vm_utils_value_to_str(vm, values[0], &len);
    StrObj *str_obj = NULL;

    if(vm_create_str(vm, 1, len, raw_str, &str_obj)){
        MEMORY_DEALLOC(
            vm_allocator(vm),
            char,
            len + 1,
            raw_str
        );
    }

    return OBJ_VALUE(str_obj);
}

Value native_fn_to_json(VM *vm, uint8_t argsc, Value *values, Value target){
    size_t len;
    char *raw_str = vm_utils_value_to_json(4, 0, values[0], vm, &len);
    StrObj *str_obj = NULL;

    if(vm_create_str(vm, 1, len, raw_str, &str_obj)){
        MEMORY_DEALLOC(
            vm_allocator(vm),
            char,
            len + 1,
            raw_str
        );
    }

    return OBJ_VALUE(str_obj);
}

Value native_fn_to_int(VM *vm, uint8_t argsc, Value *values, Value target){
    Value raw_value = values[0];

    if(IS_VALUE_BOOL(raw_value)){
        return INT_VALUE((int64_t)VALUE_TO_BOOL(raw_value));
    }

    if(IS_VALUE_INT(raw_value)){
        return raw_value;
    }

    if(IS_VALUE_FLOAT(raw_value)){
        return INT_VALUE((int64_t)VALUE_TO_FLOAT(raw_value));
    }

    if(is_value_str(raw_value)){
        int64_t value;
        StrObj *str_obj = VALUE_TO_STR(raw_value);

        if(!vm_str_is_int(str_obj)){
            vm_error(
                vm,
                "Failed to parse 'str' to 'int': contains not valid digits"
            );
        }

        utils_decimal_str_to_i64(str_obj->buff, &value);

        return INT_VALUE(value);
    }

    vm_error(
        vm,
        "Failed to parse to 'int': unsupported type"
    );

    return EMPTY_VALUE;
}

Value native_fn_to_float(VM *vm, uint8_t argsc, Value *values, Value target){
    Value raw_value = values[0];

    if(IS_VALUE_INT(raw_value)){
        return FLOAT_VALUE((double)VALUE_TO_INT(raw_value));
    }

    if(IS_VALUE_FLOAT(raw_value)){
        return raw_value;
    }

    if(is_value_str(raw_value)){
        double value;
        StrObj *str_obj = VALUE_TO_STR(raw_value);

        if(!vm_str_is_float(str_obj)){
            vm_error(
                vm,
                "Failed to parse 'str' to 'float': malformed float string"
            );
        }

        utils_str_to_double(str_obj->buff, &value);

        return FLOAT_VALUE(value);
    }

    vm_error(
        vm,
        "Failed to parse to 'int': unsupported type"
    );

    return EMPTY_VALUE;
}

Value native_fn_print(VM *vm, uint8_t argsc, Value *values, Value target){
    Value raw_value = values[0];

    vm_utils_print_value(vm, stdout, raw_value);

    return EMPTY_VALUE;
}

Value native_fn_println(VM *vm, uint8_t argsc, Value *values, Value target){
    Value raw_value = values[0];

    vm_utils_print_value(vm, stdout, raw_value);
    printf("\n");

    return EMPTY_VALUE;
}

Value native_fn_eprint(VM *vm, uint8_t argsc, Value *values, Value target){
    Value raw_value = values[0];

    vm_utils_print_value(vm, stderr, raw_value);

    return EMPTY_VALUE;
}

Value native_fn_eprintln(VM *vm, uint8_t argsc, Value *values, Value target){
    Value raw_value = values[0];

    vm_utils_print_value(vm, stderr, raw_value);
    fprintf(stderr, "\n");

    return EMPTY_VALUE;
}

Value native_fn_readln(VM *vm, uint8_t argsc, Value *values, Value target){
    size_t raw_buff_len = 1024;
    char raw_buff[raw_buff_len];
    LZBStr *backup_buff = MEMORY_LZBSTR(vm_allocator(vm));

    while(fgets(raw_buff, raw_buff_len, stdin)){
        size_t len = strlen(raw_buff);
        char last = raw_buff[len - 1];

        lzbstr_append(backup_buff, raw_buff);

        if(last == '\n' || last == EOF){
            break;
        }
    }

    StrObj *str_obj = NULL;
    size_t backup_raw_buff_len;
    char *cloned_backup_raw_buff = NULL;

    lzbstr_rclone_buff(
        backup_buff,
        (LZBStrAllocator *)vm_allocator(vm),
        &backup_raw_buff_len,
        &cloned_backup_raw_buff
    );

    if(vm_create_str(
        vm,
        1,
        backup_raw_buff_len,
        cloned_backup_raw_buff,
        &str_obj)
    ){
        lzbstr_destroy(backup_buff);
        MEMORY_DEALLOC(
            vm_allocator(vm),
            char,
            backup_raw_buff_len + 1,
            cloned_backup_raw_buff
        );

        return OBJ_VALUE(str_obj);
    }

    lzbstr_destroy(backup_buff);

    return OBJ_VALUE(str_obj);
}

Value native_fn_gc(VM *vm, uint8_t argsc, Value *values, Value target){
    vm_gc(vm);

    return EMPTY_VALUE;
}

Value native_fn_halt(VM *vm, uint8_t argsc, Value *values, Value target){
    vm_halt(vm);

    return EMPTY_VALUE;
}

Value native_fn_native_array_new(VM *vm, uint8_t argsc, Value *values, Value target){
    size_t len = vm_utils_value_validate_len_arg(vm, values[0], 1, "length");

    return OBJ_VALUE(vm_create_native(vm, native_array_create(vm_native_context(vm), len)));
}

Value native_fn_native_random_new(VM *vm, uint8_t argsc, Value *values, Value target){
    return OBJ_VALUE(vm_create_native(vm, native_random_create(vm_native_context(vm))));
}

Value native_fn_native_random_seed_new(VM *vm, uint8_t argsc, Value *values, Value target){
    return OBJ_VALUE(vm_create_native(
        vm,
        native_random_create_seed(
            vm_native_context(vm),
            vm_utils_value_validate_int_arg(
                vm,
                values[0],
                1,
                "seed"
            )
        )
    ));
}

Value native_fn_native_file_open(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *pathname_str = vm_utils_value_validate_str_arg(vm, values[0], 1, "path");
	StrObj *mode_str = vm_utils_value_validate_str_arg(vm, values[1], 2, "mode");

	size_t str_mode_len = mode_str->len;
	char *str_mode = mode_str->buff;
	file_mode_t mode = parse_file_mode(str_mode_len, str_mode, vm);
	char *pathname = pathname_str->buff;

    if(!utils_files_can_read(pathname)){
		vm_error(
			vm,
			"Error opening pathname '%s': does not exist or cannot be read",
			pathname
		);
	}

	if(!utils_files_is_regular(pathname)){
		vm_error(
			vm,
			"Error opening pathname '%s': not a regular file",
			pathname
		);
	}

	FILE *file = fopen(pathname_str->buff, str_mode);

	if(!file){
		vm_error(
			vm,
			"Error opening pathname '%s': %s",
			pathname,
			strerror(errno)
		);
	}

	NativeFile *file_native = native_file_create(vm_native_context(vm), mode, file);
	NativeObj *file_native_obj = vm_create_native(vm, file_native);

	return OBJ_VALUE(file_native_obj);
}

#endif
