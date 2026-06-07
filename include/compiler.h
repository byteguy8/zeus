#ifndef COMPILER_H
#define COMPILER_H

#include "essentials/dynarr.h"
#include "essentials/lzarena.h"
#include "essentials/lzflist.h"
#include "essentials/lzohtable.h"
#include "essentials/lzpool.h"
#include "essentials/memory.h"

#include "scope_manager/scope_manager.h"

#include "types.h"
#include "token.h"
#include "vm/fn.h"
#include "vm/module.h"

#include <stdint.h>
#include <setjmp.h>

typedef struct label{
	size_t offset;
	size_t name_len;
	char   *name;
}Label;

typedef struct jmp{
	size_t update_offset;
	size_t jump_offset;
	size_t label_name_len;
	char   *label_name;
}Jmp;

typedef struct mark{
    size_t update_offset;
    size_t jump_offset;
	size_t label_name_len;
	char   *label_name;
}Mark;

typedef struct loop{
    int32_t     id;
    struct loop *prev;
}Loop;

typedef struct block{
	size_t       stmts_len;
	size_t       current_stmt;
	struct block *prev;
}Block;

typedef struct compilation_unit{
	int32_t     counter;

	LZOHTable   *labels;
	DynArr      *jmps;
    DynArr      *marks;
    Block       *blocks;
    Loop        *loops;
    LZOHTable   *captured_symbols;

    LZPool      *labels_pool;
	LZPool      *jmps_pool;
    LZPool      *marks_pool;
    LZPool      *loops_pool;
    LZPool      *blocks_pool;

    Allocator   *lzflist_allocator;

    void        *arena_state;
    Fn          *fn;
	struct compilation_unit *prev;
}CompilationUnit;

typedef struct compiler{
    jmp_buf         err_buf;
	CompilationUnit *units_stack;

    LZOHTable       *keywords;
    DStr            *main_search_pathname;
    DynArr          *search_pathnames;
    LZOHTable       *default_natives;
    ScopeManager    *manager;
    Module          *module;

    LZOHTable       *modules;
    LZArena         *compiler_arena;

	const Allocator *ctallocator;  // To allocate things only needed at compile time
	const Allocator *rtallocator;  // to allocate things only needed at runtime
    const Allocator *pass_allocator;
    Allocator       *arena_allocator;
}Compiler;

Compiler *compiler_create(const Allocator *ctallocator, const Allocator *rtallocator);
void compiler_destroy(Compiler *compiler);

Module *compiler_compile(
    Compiler *compiler,
    LZOHTable *keywords,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    LZOHTable *default_natives,
    DynArr *proc_prototypes,
    ScopeManager *manager,
    LZOHTable *modules,
    DynArr *stmts,
    const char *pathname
);

Module *compiler_import(
    Compiler *compiler,
    LZOHTable *keywords,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    LZOHTable *default_natives,
    DynArr *proc_prototypes,
    ScopeManager *manager,
    LZOHTable *modules,
    LZArena *compiler_arena,
    const Allocator *pass_allocator,
    Allocator *arena_allocator,
    DynArr *stmts,
    const char *pathname,
    const char *name
);

#endif
