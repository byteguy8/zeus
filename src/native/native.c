#include "native.h"

#include "essentials/memory.h"

#include <string.h>

void native_init_header(
	NativeHeader *header,
	NativeType type,
	char *name,
	NativeDestroyHelper destroy_helper,
	Allocator *allocator
){
	size_t name_len = strlen(name);
	char *cloned_name = MEMORY_ALLOC(allocator, char, name_len + 1);

	memcpy(cloned_name, name, name_len);
	cloned_name[name_len] = 0;

	header->type = type;
	header->name = name;
	header->destroy_helper = destroy_helper;
}
