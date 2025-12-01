#ifndef NATIVE_IO_H
#define NATIVE_IO_H

#include "utils.h"

#include "native/native_nbarray.h"
#include "native/native_file.h"

#include "vm/types_utils.h"
#include "vm/vm_factory.h"
#include "vm/obj.h"
#include "vm/vmu.h"
#include "vm/vm.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

NativeModule *io_native_module = NULL;

#define VALIDATE_FILE_OPENED(_stream) \
	if(!_stream){                     \
		vmu_error(                    \
			VMU_VM,                   \
			"File is closed"          \
		);                            \
	}

#define VALIDATE_FILE_NATIVE_READ(_mode, _stream) \
	if(!_stream){                                 \
		vmu_error(                                \
			VMU_VM,                               \
			"File is closed"                      \
		);                                        \
	}                                             \
												  \
	if(!FILE_NATIVE_CAN_READ(_mode)){             \
		vmu_error(                                \
			VMU_VM,                               \
			"File not opened to read"             \
		);                                        \
	}                                             \

#define VALIDATE_FILE_NATIVE_READ_BYTES(_mode, _stream) \
	if(!_stream){                                       \
		vmu_error(                                      \
			VMU_VM,                                     \
			"File is closed"                            \
		);                                              \
	}                                                   \
													    \
	if(!FILE_NATIVE_CAN_READ_BYTES(_mode)){             \
		vmu_error(                                      \
			VMU_VM,                                     \
			"File not opened to read bytes"             \
		);                                              \
	}                                                   \

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

file_mode_t parse_mode(size_t str_mode_len, char *str_mode, VM *vm){
	if(str_mode_len == 0){
		vmu_error(
			vm,
			"Illegal mode: empty"
		);
	}

	if(str_mode_len > 3){
		vmu_error(
			vm,
			"Illegal mode: can only contains from 1 to 3 flags"
		);
	}

	size_t current = 0;
	file_mode_t mode = 0;

	if(match(str_mode_len, str_mode, &current, 3, 'r', 'w', 'a')){
		switch (str_mode[current - 1]) {
			case 'r':{
				mode |= FILE_NATIVE_READ_MODE;
				break;
			}case 'w':{
				mode |= FILE_NATIVE_WRITE_MODE;
				break;
			}case 'a':{
				mode |= FILE_NATIVE_APPEND_MODE;
				break;
			}
		}

		while(current < str_mode_len){
			if(match(str_mode_len, str_mode, &current, 2, '+', 'b')){
				switch (str_mode[current - 1]) {
					case '+':{
						if(mode & FILE_NATIVE_PLUS_MODE){
							vmu_error(
								vm,
								"'+' flag is duplicated"
							);
						}

						mode |= FILE_NATIVE_PLUS_MODE;

						break;
					}case 'b':{
						if(mode & FILE_NATIVE_BINARY_MODE){
							vmu_error(
								vm,
								"'b' flag is duplicated"
							);
						}

						mode |= FILE_NATIVE_BINARY_MODE;

						break;
					}
				}
			}else{
				vmu_error(
					vm,
					"Unexpected flag '%c' at index %zu",
					str_mode[current],
					current
				);
			}
		}
	}else{
		vmu_error(
			vm,
			"Unknown flag: '%c'",
			str_mode[current]
		);
	}

	return mode;
}

Value native_fn_io_open(uint8_t argsc, Value *values, Value target, void *context){
	StrObj *pathname_str = validate_value_str_arg(values[0], 1, "path", VMU_VM);
	StrObj *mode_str = validate_value_str_arg(values[1], 2, "mode", VMU_VM);

	size_t str_mode_len = mode_str->len;
	char *str_mode = mode_str->buff;
	file_mode_t mode = parse_mode(str_mode_len, str_mode, VMU_VM);
	char *pathname = pathname_str->buff;

    if(!UTILS_FILES_CAN_READ(pathname)){
		vmu_error(
			VMU_VM,
			"Error opening pathname '%s': does not exist or cannot be read",
			pathname
		);
	}

	if(!utils_files_is_regular(pathname)){
		vmu_error(
			VMU_VM,
			"Error opening pathname '%s': not a regular file",
			pathname
		);
	}

	FILE *file = fopen(pathname_str->buff, str_mode);

	if(!file){
		vmu_error(
			VMU_VM,
			"Error opening pathname '%s': %s",
			pathname,
			strerror(errno)
		);
	}

	FileNative *file_native = file_native_create(
		mode,
		file,
		VMU_NATIVE_FRONT_ALLOCATOR
	);
	NativeObj *file_native_obj = vmu_create_native(file_native, VMU_VM);

	return OBJ_VALUE(file_native_obj);
}

Value native_fn_io_close(uint8_t argsc, Value *values, Value target, void *context){
	FileNative *file = file_native_validate_value_arg(
		values[0],
		1,
		"file",
		VMU_VM
	);

	FILE *stream = file->stream;

	if(!stream){
		vmu_error(VMU_VM, "Trying to close not opened file");
	}

	fclose(stream);

	file->stream = NULL;

	return EMPTY_VALUE;
}

Value native_fn_io_is_closed(uint8_t argsc, Value *values, Value target, void *context){
	FileNative *file = file_native_validate_value_arg(
		values[0],
		1,
		"file",
		VMU_VM
	);

	return BOOL_VALUE(!file->stream);
}

Value native_fn_io_len(uint8_t argsc, Value *values, Value target, void *context){
	FileNative *file_native = file_native_validate_value_arg(
		values[0],
		1,
		"file",
		VMU_VM
	);
	FILE *stream = file_native->stream;

	VALIDATE_FILE_OPENED(stream);

	long len = 0;
	long old_position = ftell(stream);

	fseek(stream, 0, SEEK_END);
	len = ftell(stream);
	fseek(stream, old_position, SEEK_SET);

	return INT_VALUE(len);
}

Value native_fn_io_pos(uint8_t argsc, Value *values, Value target, void *context){
	FileNative *file_native = file_native_validate_value_arg(
		values[0],
		1,
		"file",
		VMU_VM
	);
	FILE *stream = file_native->stream;

	VALIDATE_FILE_OPENED(stream)

	return INT_VALUE(ftell(stream));
}

Value native_fn_io_read_byte(uint8_t argsc, Value *values, Value target, void *context){
	FileNative *file_native = file_native_validate_value_arg(
		values[0],
		1,
		"file",
		VMU_VM
	);
	file_mode_t mode = file_native->mode;
	FILE *stream = file_native->stream;

	VALIDATE_FILE_NATIVE_READ_BYTES(mode, stream)

	return INT_VALUE(fgetc(stream));
}

Value native_fn_io_read_bytes(uint8_t argsc, Value *values, Value target, void *context){
	FileNative *file_native = file_native_validate_value_arg(
		values[0],
		1,
		"file",
		VMU_VM
	);
	NBArrayNative *nbarray_native = nbarray_native_validate_value_arg(
		values[1],
		2,
		"array",
		VMU_VM
	);

	file_mode_t mode = file_native->mode;
	FILE *stream = file_native->stream;
	size_t nbuff_len = nbarray_native->len;
	unsigned char *nbuff_bytes = nbarray_native->bytes;

	VALIDATE_FILE_NATIVE_READ_BYTES(mode, stream)

	return INT_VALUE(fread(nbuff_bytes, 1, nbuff_len, stream));
}

Value native_fn_io_read_text(uint8_t argsc, Value *values, Value target, void *context){
    StrObj *pathname_str_obj = validate_value_str_arg(
    	values[0],
     	1,
      	"pathname",
       	VMU_VM
    );
    char *pathname = pathname_str_obj->buff;

    if(!UTILS_FILES_CAN_READ(pathname)){
		vmu_error(
			VMU_VM,
			"File at '%s' do not exists or cannot be read",
			pathname
		);
	}

	if(!utils_files_is_regular(pathname)){
		vmu_error(
			VMU_VM,
			"File at '%s' is not a regular file",
			pathname
		);
	}

    size_t content_len;
    char *content_buff = utils_read_file_as_text(pathname, VMU_NATIVE_FRONT_ALLOCATOR, &content_len);
    StrObj *content_str_obj = NULL;

    if(vmu_create_str(1, content_len, content_buff, VMU_VM, &content_str_obj)){
        MEMORY_DEALLOC(VMU_NATIVE_FRONT_ALLOCATOR, char, content_len + 1, content_buff);
    }

    return OBJ_VALUE(content_str_obj);
}

void io_module_init(const Allocator *allocator){
    io_native_module = vm_factory_native_module_create(allocator, "io");

    vm_factory_native_module_add_native_fn(io_native_module, "open", 2, native_fn_io_open);
    vm_factory_native_module_add_native_fn(io_native_module, "close", 1, native_fn_io_close);
    vm_factory_native_module_add_native_fn(io_native_module, "is_closed", 1, native_fn_io_is_closed);
    vm_factory_native_module_add_native_fn(io_native_module, "len", 1, native_fn_io_len);
    vm_factory_native_module_add_native_fn(io_native_module, "pos", 1, native_fn_io_pos);
    vm_factory_native_module_add_native_fn(io_native_module, "read_byte", 1, native_fn_io_read_byte);
    vm_factory_native_module_add_native_fn(io_native_module, "read_bytes", 2, native_fn_io_read_bytes);
}

#endif
