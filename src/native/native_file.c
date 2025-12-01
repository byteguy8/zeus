#include "native_file.h"
#include <stdio.h>

void file_native_destroy(void *native, Allocator *allocator){
	FileNative *file = native;
	FILE *stream = file->stream;

	if(stream){
		fclose(stream);
	}

	MEMORY_DEALLOC(allocator, FileNative, 1, file);
}

FileNative *file_native_create(file_mode_t mode, FILE *file, Allocator *allocator){
	FileNative *file_native = MEMORY_ALLOC(allocator, FileNative, 1);

	native_init_header(
		(NativeHeader *)file_native,
		FILE_NATIVE_TYPE,
		"file",
		file_native_destroy,
		allocator
	);
	file_native->mode = mode;
	file_native->stream = file;

	return file_native;
}

CREATE_VALIDATE_NATIVE("file", file_native, FILE_NATIVE_TYPE, FileNative)
