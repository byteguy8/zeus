#ifndef FILE_NATIVE_H
#define FILE_NATIVE_H

#include "vm/native.h"
#include "vm/vm_utils.h"

#include <stdint.h>
#include <stdio.h>

#define NATIVE_FILE_NAME "file"

#define NATIVE_FILE_READ_MODE   0b10000000
#define NATIVE_FILE_WRITE_MODE  0b01000000
#define NATIVE_FILE_APPEND_MODE 0b00100000
#define NATIVE_FILE_BINARY_MODE 0b00010000
#define NATIVE_FILE_PLUS_MODE   0b00001000

typedef uint8_t file_mode_t;

typedef struct native_file{
	native_t    header;
	file_mode_t mode;
	FILE        *stream;
}NativeFile;

CREATE_VALIDATE_NATIVE_DECLARATION(native_file, NativeFile)

void native_file_init(NativeContext *context);
NativeFile *native_file_create(NativeContext *context, file_mode_t mode, FILE *file);

#endif
