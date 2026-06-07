#include "native_file.h"

#include "native_array.h"

#include <stdio.h>
#include <errno.h>
#include <limits.h>

#define NATIVE_FILE_R_PLUS_B 0b10011000 // r+b
#define NATIVE_FILE_W_PLUS_B 0b01011000 // w+b
#define NATIVE_FILE_A_PLUS_B 0b00111000 // a+b

#define READ_BYTES_MODE_0    0b10010000 //rb
#define READ_BYTES_MODE_1    NATIVE_FILE_R_PLUS_B
#define READ_BYTES_MODE_2    NATIVE_FILE_W_PLUS_B
#define READ_BYTES_MODE_3    NATIVE_FILE_A_PLUS_B

#define WRITE_BYTES_MODE_0   0b01010000 //wb
#define WRITE_BYTES_MODE_1   0b00110000 //ab
#define WRITE_BYTES_MODE_2   NATIVE_FILE_R_PLUS_B
#define WRITE_BYTES_MODE_3   NATIVE_FILE_W_PLUS_B
#define WRITE_BYTES_MODE_4   NATIVE_FILE_A_PLUS_B

static native_t FILE_ID = NATIVE_T_UNSET;

static int native_file_can_read_bytes(file_mode_t mode){
    file_mode_t mode_0 = (mode & READ_BYTES_MODE_0) == READ_BYTES_MODE_0;
    file_mode_t mode_1 = (mode & READ_BYTES_MODE_1) == READ_BYTES_MODE_1;
    file_mode_t mode_2 = (mode & READ_BYTES_MODE_2) == READ_BYTES_MODE_2;
    file_mode_t mode_3 = (mode & READ_BYTES_MODE_3) == READ_BYTES_MODE_3;

    return mode_0 | mode_1 | mode_2 | mode_3;
}

static int native_file_can_write_bytes(file_mode_t mode){
    file_mode_t mode_0 = (mode & WRITE_BYTES_MODE_0) == WRITE_BYTES_MODE_0;
    file_mode_t mode_1 = (mode & WRITE_BYTES_MODE_1) == WRITE_BYTES_MODE_1;
    file_mode_t mode_2 = (mode & WRITE_BYTES_MODE_2) == WRITE_BYTES_MODE_2;
    file_mode_t mode_3 = (mode & WRITE_BYTES_MODE_3) == WRITE_BYTES_MODE_3;
    file_mode_t mode_4 = (mode & WRITE_BYTES_MODE_4) == WRITE_BYTES_MODE_4;

    return mode_0 | mode_1 | mode_2 | mode_3 | mode_4;
}

void native_file_destroy(const Allocator *allocator, void *raw_native){
	NativeFile *native_file = raw_native;
	FILE *stream = native_file->stream;

	if(stream){
		fclose(stream);
	}

	MEMORY_DEALLOC(allocator, NativeFile, 1, native_file);
}

#define VALIDATE_FILE_OPENED(_stream) \
    if(!_stream){vm_error(vm, "File is closed");}

#define VALIDATE_FILE_NATIVE_READ(_mode, _stream) \
	if(!_stream){vm_error(vm, "File is closed");} \
	if(!native_file_can_read_bytes(_mode)){vm_error(vm, "File not opened to read");}

#define VALIDATE_FILE_NATIVE_READ_BYTES(_mode, _stream) \
	if(!_stream){vm_error(vm, "File is closed");}       \
	if(!native_file_can_read_bytes(_mode)){vm_error(vm, "File not opened to read bytes");}

#define VALIDATE_NATIVE_FILE_WRITE_BYTES(_mode, _stream) \
    if(!_stream){vm_error(vm, "File is closed");}        \
	if(!native_file_can_write_bytes(_mode)){vm_error(vm, "File not opened to write bytes");}

static Value native_file_is_closed(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;

    return BOOL_VALUE(native_file->stream != NULL);
}

static Value native_file_length(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;
    FILE *stream = native_file->stream;

	VALIDATE_FILE_OPENED(stream);

	long len = 0;
	long old_position = ftell(stream);

	fseek(stream, 0, SEEK_END);
	len = ftell(stream);
	fseek(stream, old_position, SEEK_SET);

	return INT_VALUE(len);
}

static Value native_file_position(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;
    FILE *stream = native_file->stream;

	VALIDATE_FILE_OPENED(stream)

	return INT_VALUE(ftell(stream));
}

static Value native_file_close(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;
    FILE *stream = native_file->stream;

    if(stream){
        fclose(stream);
        native_file->stream = NULL;

        return EMPTY_VALUE;
    }

    vm_error(
        vm,
        "Trying to close not opened file"
    );

    return EMPTY_VALUE;
}

static Value native_file_set_position(VM *vm, uint8_t argsc, Value *args, Value target){
    int64_t position = vm_utils_value_validate_int_arg(vm, args[0], 1, "position");
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;
    FILE *stream = native_file->stream;

	VALIDATE_FILE_OPENED(stream)

    if(fseek(stream, (long)position, SEEK_SET) == 0){
        return EMPTY_VALUE;
    }

    char *err_description = strerror(errno);

    vm_throw_raw(vm, "Failed to set position: %s", err_description);

	return EMPTY_VALUE;
}

static Value native_file_read_byte(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;
    file_mode_t mode = native_file->mode;
    FILE *stream = native_file->stream;

	VALIDATE_FILE_NATIVE_READ_BYTES(mode, stream)

	return INT_VALUE(fgetc(stream));
}

static Value native_file_read_bytes(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;
    NativeArray *native_array = native_array_validate_value_arg(
		vm,
		1,
		"native_array",
        args[0]
	);

	FILE *stream = native_file->stream;

	VALIDATE_FILE_NATIVE_READ_BYTES(native_file->mode, stream)

	return INT_VALUE(fread(native_array->bytes, 1, native_array->len, stream));
}

static Value native_file_write_byte(VM *vm, uint8_t argsc, Value *args, Value target){
    int byte = (int)vm_utils_value_validate_int_range_arg(vm, args[0], 1, "byte", 0, UCHAR_MAX);
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;
	FILE *stream = native_file->stream;

    VALIDATE_NATIVE_FILE_WRITE_BYTES(native_file->mode, stream)

    if(fputc((unsigned char)byte, stream) == EOF){
        vm_throw_raw(vm, "I/O Error: %s", strerror(errno));
    }

	return EMPTY_VALUE;
}

static Value native_file_write_bytes(VM *vm, uint8_t argsc, Value *args, Value target){
    NativeArray *native_array = native_array_validate_value_arg(vm, 1, "bytes", args[0]);
    NativeFile *native_file = (NativeFile *)VALUE_TO_NATIVE(target)->raw_native;
	FILE *stream = native_file->stream;

    VALIDATE_NATIVE_FILE_WRITE_BYTES(native_file->mode, stream)

    if(fwrite(native_array->bytes, sizeof(unsigned char), native_array->len, stream) != native_array->len){
        vm_throw_raw(vm, "I/O Error: %s", strerror(errno));
    }

	return EMPTY_VALUE;
}

CREATE_VALIDATE_NATIVE_IMPLEMENTATION(NATIVE_FILE_NAME, native_file, FILE_ID, NativeFile)

void native_file_init(NativeContext *context){
    FILE_ID = native_identificate(
        context,
        NATIVE_FILE_NAME,
        native_file_destroy,
        NULL,
        NULL,
        NULL
    );

    native_properties_add_native_fn(context, FILE_ID, "is_closed",    0, native_file_is_closed);
    native_properties_add_native_fn(context, FILE_ID, "length",       0, native_file_length);
    native_properties_add_native_fn(context, FILE_ID, "position",     0, native_file_position);
    native_properties_add_native_fn(context, FILE_ID, "close",        0, native_file_close);
    native_properties_add_native_fn(context, FILE_ID, "set_position", 1, native_file_set_position);
    native_properties_add_native_fn(context, FILE_ID, "read_byte",    0, native_file_read_byte);
    native_properties_add_native_fn(context, FILE_ID, "read_bytes",   1, native_file_read_bytes);
    native_properties_add_native_fn(context, FILE_ID, "write_byte",   1, native_file_write_byte);
    native_properties_add_native_fn(context, FILE_ID, "write_bytes",  1, native_file_write_bytes);
}

NativeFile *native_file_create(NativeContext *context, file_mode_t mode, FILE *file){
	const Allocator *allocator = native_allocator(context);
    NativeFile *native_file = MEMORY_ALLOC(allocator, NativeFile, 1);

    *native_file = (NativeFile){
        .header = FILE_ID,
        .mode   = mode,
        .stream = file
    };

	return native_file;
}