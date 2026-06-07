#ifndef LEXER_H
#define LEXER_H

#include "essentials/dynarr.h"
#include "essentials/lzbstr.h"
#include "essentials/lzohtable.h"
#include "essentials/lzarena.h"
#include "essentials/memory.h"

#include "types.h"

#include <setjmp.h>

typedef struct lexer{
    jmp_buf         err_buf;
	int             line;
	size_t          start;
	size_t          current;
    const char      *pathname;
    const DStr      *source;
	DynArr          *tokens;
    const LZOHTable *keywords;

    const Allocator *rtallocator;
    const Allocator *ctallocator;
    Allocator       *lexer_allocator;
}Lexer;

Lexer *lexer_create(const Allocator *compile_time_allocator, const Allocator *runtime_allocator);

int lexer_lex(
    Lexer *lexer,
    const DStr *source,
    const char *pathname,
    const LZOHTable *keywords,
    DynArr *tokens
);

#endif
