#ifndef FILE_NATIVE_H
#define FILE_NATIVE_H

#include "native.h"

#include "vm/vmu.h"

#include <stdint.h>
#include <stdio.h>

#define FILE_NATIVE_READ_MODE   0b10000000
#define FILE_NATIVE_WRITE_MODE  0b01000000
#define FILE_NATIVE_APPEND_MODE 0b00100000
#define FILE_NATIVE_BINARY_MODE 0b00010000
#define FILE_NATIVE_PLUS_MODE   0b00001000

#define FILE_NATIVE_CAN_READ(_mode) (                               \
	((_mode) & FILE_NATIVE_READ_MODE)                            || \
	((_mode) & (FILE_NATIVE_WRITE_MODE | FILE_NATIVE_PLUS_MODE)) || \
	((_mode) & (FILE_NATIVE_APPEND_MODE | FILE_NATIVE_PLUS_MODE))   \
)
#define FILE_NATIVE_CAN_READ_BYTES(_mode) \
	(FILE_NATIVE_CAN_READ(_mode) && ((_mode) & FILE_NATIVE_BINARY_MODE))
#define FILE_NATIVE_CAN_WRITE(_mode) \
	(((_mode) & FILE_NATIVE_WRITE_MODE) || ((_mode) & FILE_NATIVE_APPEND_MODE) || ((_mode) & FILE_PLUS_MODE))
#define FILE_NATIVE_CAN_APPEND(_mode) \
	((_mode) & FILE_NATIVE_APPEND_MODE)
#define FILE_NATIVE_IS_BINARY(_mode) \
	((_mode) & FILE_NATIVE_BINARY_MODE)

typedef uint8_t file_mode_t;

typedef struct file_native{
	NativeHeader header;
	file_mode_t mode;
	FILE *stream;
}FileNative;

FileNative *file_native_create(file_mode_t mode, FILE *file, Allocator *allocator);
CREATE_VALIDATE_NATIVE_DECLARATION(file_native, FileNative)

#endif
