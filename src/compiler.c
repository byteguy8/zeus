#include "compiler.h"

#include "essentials/lzbstr.h"
#include "essentials/dynarr.h"
#include "essentials/lzohtable.h"
#include "essentials/lzpool.h"
#include "essentials/memory.h"

#include "scope_manager/scope.h"
#include "scope_manager/scope_manager.h"
#include "scope_manager/symbol.h"

#include "vm/value.h"
#include "native_module/native_module_os.h"
#include "native_module/native_module_math.h"
#include "native_module/native_module_time.h"

#include "utils.h"
#include "types.h"

#include "token.h"
#include "expr.h"
#include "stmt.h"
#include "lexer.h"
#include "parser.h"

#include "vm/fn.h"
#include "vm/module.h"
#include "vm/native_module.h"
#include "vm/obj.h"
#include "vm/vm_factory.h"
#include "vm/opcode.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

typedef enum status_code{
    OK_STATUS_CODE,
    FILE_IS_DIRECTORY_ERR_STATUS_CODE,
    MODULE_NOT_FOUND_ERR_STATUS_CODE,
    IMPORT_FAILED_ERR_STATUS_CODE,
}StatusCode;

static void error(Compiler *compiler, const Token *token, const char *fmt, ...);
static void internal_error(Compiler *compiler, const char *fmt, ...);

static CompilationUnit *create_unit(Compiler *compiler, Fn *fn);
static CompilationUnit *peek_unit(Compiler *compiler);
static CompilationUnit *push_unit(Compiler *compiler, Fn *fn);
static CompilationUnit *pop_unit(Compiler *compiler);

static void pop_scope_locals(Compiler *compiler, Scope *scope);
static void pop_locals(Compiler *compiler);

static Block *peek_block(Compiler *compiler);
static Block *push_block(Compiler *compiler);
static void pop_block(Compiler *compiler);

static Loop *peek_loop(Compiler *compiler);
static void push_loop(Compiler *compiler, int32_t loop_id);
static void pop_loop(Compiler *compiler);

static Module *current_module(Compiler *compiler);
static int32_t generate_id(Compiler *compiler);
static Fn *current_fn(Compiler *compiler);
static DynArr *current_chunks(Compiler *compiler);
static DynArr *current_locations(Compiler *compiler);
static DynArr *current_iconsts(Compiler *compiler);
static DynArr *current_fconsts(Compiler *compiler);

static void descompose_i16(int16_t value, uint8_t *bytes);
static void descompose_i32(int32_t value, uint8_t *bytes);
static size_t chunks_len(Compiler *compiler);
static size_t write_chunk(Compiler *compiler, uint8_t chunk);
static size_t write_i16(Compiler *compiler, int16_t value);
static size_t write_i32(Compiler *compiler, int32_t value);
static size_t write_iconst(Compiler *compiler, int64_t value);
static size_t write_fconst(Compiler *compiler, double value);
static void update_i16(Compiler *compiler, size_t offset, uint16_t value);
static void write_str(Compiler *compiler, size_t raw_str_len, char *raw_str);
static void write_str_alloc(Compiler *compiler, size_t raw_str_len, char *raw_str);
static void write_location(Compiler *compiler, const Token *token);

static void label(Compiler *compiler, const Token *ref_token, const char *fmt, ...);
static void mark(Compiler *compiler, const Token *ref_token, const char *fmt, ...);
static void jmp(Compiler *compiler, const Token *ref_token, const char *fmt, ...);
static void jif(Compiler *compiler, const Token *ref_token, const char *fmt, ...);
static void jit(Compiler *compiler, const Token *ref_token, const char *fmt, ...);
static void or(Compiler *compiler, const Token *ref_token, const char *fmt, ...);
static void and(Compiler *compiler, const Token *ref_token, const char *fmt, ...);
static void get_local(Compiler *compiler, Token *identifier_token);
static void set_local(Compiler *compiler, Token *identifier_token);

static void compile_expr(Compiler *compiler, Expr *expr);
static void propagate_return(Compiler *compiler, Scope *scope);
static int compile_if_branch(
    Compiler *compiler,
    IfStmtBranch *if_branch,
    ScopeKind type,
    int32_t id,
    int32_t which
);
static int import_native(Compiler *compiler, const Token *name_token, const Token *alt_name_token);
static StatusCode resolve_import_names(
    const Allocator *allocator,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    Token *import_token,
    DynArr *names,
    DStr **out_main_search_pathname,
    char **out_source_pathname
);
static StatusCode import_module(
    LZOHTable *keywords,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    LZOHTable *default_natives,
    LZOHTable *modules,
    LZArena *compiler_arena,
    const Allocator *ctallocator,
    const Allocator *rtallocator,
    const Allocator *pass_allocator,
    Allocator *compiler_arena_allocator,
    const char *pathname,
    const char *name,
    Module **out_module
);
static Symbol *clone_symbol(const Symbol *symbol, const Allocator *allocator);

static void expr_stmt(Compiler *compiler, ExprStmt *expr_stmt);
static void var_decl_stmt(Compiler *compiler, VarDeclStmt *var_decl_stmt);
static void block_stmt(Compiler *compiler, BlockStmt *block_stmt);
static void if_stmt(Compiler *compiler, IfStmt *if_stmt);
static void stop_stmt(Compiler *compiler, StopStmt *stop_stmt);
static void continue_stmt(Compiler *compiler, ContinueStmt *continue_stmt);
static void while_stmt(Compiler *compiler, WhileStmt *while_stmt);
static void for_stmt(Compiler *compiler, ForStmt *for_stmt);
static void throw_stmt(Compiler *compiler, ThrowStmt *throw_stmt);
static void try_stmt(Compiler *compiler, TryStmt *try_stmt);
static void return_stmt(Compiler *compiler, ReturnStmt *ret_stmt);
static void proc_stmt(Compiler *compiler, ProcStmt *proc_stmt);
static void import_stmt(Compiler *compiler, ImportStmt *import_stmt);
static void export_stmt(Compiler *compiler, ExportStmt *export_stmt);
static void compile_stmt(Compiler *compiler, Stmt *stmt);

static void declare_defaults(Compiler *compiler);
static void declare_proc_prototypes(Compiler *compiler, DynArr *procs_prototypes);

void error(Compiler *compiler, const Token *token, const char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    fprintf(
    	stderr,
     	"ERROR at line %d in file '%s':\n\t",
      	token->line,
       	token->pathname
    );
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);

    longjmp(compiler->err_buf, 1);
}

void internal_error(Compiler *compiler, const char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, "ERROR:\n\t");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);

    longjmp(compiler->err_buf, 1);
}

CompilationUnit *create_unit(Compiler *compiler, Fn *fn){
    LZArena *compiler_arena = compiler->compiler_arena;
    Allocator *compiler_arena_allocator = compiler->arena_allocator;
    void *compiler_arena_state = lzarena_save(compiler_arena);

    LZPool *labels_pool = MEMORY_LZPOOL(compiler_arena_allocator, Label);
    LZPool *jmps_pool = MEMORY_LZPOOL(compiler_arena_allocator, Jmp);
    LZPool *marks_pool = MEMORY_LZPOOL(compiler_arena_allocator, Mark);
    LZPool *loops_pool = MEMORY_LZPOOL(compiler_arena_allocator, Loop);
    LZPool *blocks_pool = MEMORY_LZPOOL(compiler_arena_allocator, Block);
	LZOHTable *labels = MEMORY_LZOHTABLE(compiler_arena_allocator);
	DynArr *jmps = MEMORY_DYNARR_PTR(compiler_arena_allocator);
    DynArr *marks = MEMORY_DYNARR_PTR(compiler_arena_allocator);
    LZOHTable *captured_symbols = MEMORY_LZOHTABLE(compiler_arena_allocator);
	CompilationUnit *unit = MEMORY_ALLOC(compiler_arena_allocator, CompilationUnit, 1);

    *unit = (CompilationUnit){
        .labels = labels,
        .jmps = jmps,
        .marks = marks,
        .captured_symbols = captured_symbols,

        .fn = fn,

        .labels_pool = labels_pool,
        .jmps_pool = jmps_pool,
        .marks_pool = marks_pool,
        .loops_pool = loops_pool,
        .blocks_pool = blocks_pool,

        .arena_state = compiler_arena_state
    };

	return unit;
}

inline CompilationUnit *peek_unit(Compiler *compiler){
	return compiler->units_stack;
}

inline CompilationUnit *push_unit(Compiler *compiler, Fn *fn){
	CompilationUnit *unit = create_unit(compiler, fn);

	unit->prev = compiler->units_stack;
	compiler->units_stack = unit;

	return unit;
}

CompilationUnit *pop_unit(Compiler *compiler){
	CompilationUnit *unit = compiler->units_stack;
    LZOHTable *labels = unit->labels;
    DynArr *jmps = unit->jmps;
    DynArr *marks = unit->marks;

    size_t jmps_len = dynarr_len(jmps);

    for (size_t i = 0; i < jmps_len; i++){
        Label *label = NULL;
        Jmp *jmp = (Jmp *)dynarr_get_ptr(jmps, i);

        if(!lzohtable_lookup(
            jmp->label_name_len,
            jmp->label_name,
            labels,
            (void **)(&label)
        )){
            internal_error(
                compiler,
                "Unknown label '%s'",
                jmp->label_name
            );
        }

        update_i16(compiler, jmp->update_offset, (uint16_t)(label->offset - jmp->jump_offset));
    }

    size_t marks_len = dynarr_len(marks);

    for (size_t i = 0; i < marks_len; i++){
        Label *label = NULL;
        Mark *mark = dynarr_get_ptr(marks, i);

        if(!lzohtable_lookup(
            mark->label_name_len,
            mark->label_name,
            labels,
            (void **)(&label)
        )){
            internal_error(
                compiler,
                "Unknown label '%s'",
                mark->label_name
            );
        }

        update_i16(compiler, mark->update_offset, (uint16_t)(label->offset - mark->jump_offset));
    }

    compiler->units_stack = unit->prev;

    lzarena_restore(compiler->compiler_arena, unit->arena_state);

	return compiler->units_stack;
}

inline void pop_scope_locals(Compiler *compiler, Scope *scope){
    size_t locals_count = scope->symbols->n;

    for (size_t i = 0; i < locals_count; i++){
        write_chunk(compiler, OP_POP);
    }
}

inline void pop_locals(Compiler *compiler){
    size_t locals_count = scope_manager_locals_count(compiler->manager);

    assert(locals_count <= UINT8_MAX);

    if(locals_count > 0){
        write_chunk(compiler, OP_OFFSET);
        write_chunk(compiler, (uint8_t)locals_count);
    }
}

inline Block *peek_block(Compiler *compiler){
	CompilationUnit *unit = peek_unit(compiler);
	Block *block = unit->blocks;

	assert(block && "Blocks stack is empty");

	return block;
}

inline Block *push_block(Compiler *compiler){
	CompilationUnit *unit = peek_unit(compiler);
	Block *block = lzpool_alloc_backup_count(unit->blocks_pool, 64);

	block->stmts_len = 0;
	block->current_stmt = 0;
	block->prev = unit->blocks;

	unit->blocks = block;

	return block;
}

inline void pop_block(Compiler *compiler){
	CompilationUnit *unit = peek_unit(compiler);
	Block *block = unit->blocks;

	assert(block != NULL && "Blocks stack is empty");

	unit->blocks = block->prev;

	lzpool_dealloc(block);
}

inline Loop *peek_loop(Compiler *compiler){
    CompilationUnit *unit = peek_unit(compiler);
    Loop *loop = unit->loops;

    assert(loop && "Loops stack is empty");

    return loop;
}

inline void push_loop(Compiler *compiler, int32_t loop_id){
    CompilationUnit *unit = peek_unit(compiler);
    Loop *loop = lzpool_alloc_backup_count(unit->loops_pool, 8);

    loop->id = loop_id;
    loop->prev = unit->loops;
    unit->loops = loop;
}

inline void pop_loop(Compiler *compiler){
    CompilationUnit *unit = peek_unit(compiler);
    Loop *loop = unit->loops;

    assert(loop && "Loops stack is empty");

    unit->loops = loop->prev;

    lzpool_dealloc(loop);
}

inline Module *current_module(Compiler *compiler){
	return compiler->module;
}

inline int32_t generate_id(Compiler *compiler){
	return peek_unit(compiler)->counter++;
}

inline Fn *current_fn(Compiler *compiler){
	return peek_unit(compiler)->fn;
}

inline DynArr *current_chunks(Compiler *compiler){
	return current_fn(compiler)->chunks;
}

inline DynArr *current_locations(Compiler *compiler){
	return current_fn(compiler)->locations;
}

inline DynArr *current_iconsts(Compiler *compiler){
	return current_fn(compiler)->iconsts;
}

inline DynArr *current_fconsts(Compiler *compiler){
	return current_fn(compiler)->fconsts;
}

inline void descompose_i16(int16_t value, uint8_t *bytes){
	bytes[0] = (value >> 8) & 0xff;
	bytes[1] = value & 0xff;
}

inline void descompose_i32(int32_t value, uint8_t *bytes){
	bytes[0] = (value >> 24) & 0xff;
	bytes[1] = (value >> 16) & 0xff;
	bytes[2] = (value >> 8) & 0xff;
	bytes[3] = value & 0xff;
}

inline size_t chunks_len(Compiler *compiler){
	return dynarr_len(current_chunks(compiler));
}

inline size_t write_chunk(Compiler *compiler, uint8_t chunk){
	DynArr *chunks = current_chunks(compiler);

	dynarr_insert(chunks, &chunk);

	return dynarr_len(chunks) - 1;
}

size_t write_i16(Compiler *compiler, int16_t value){
	DynArr *chunks = current_chunks(compiler);
	uint8_t bytes[2];

	descompose_i16(value, bytes);
	dynarr_insert(chunks, &bytes[0]);
	dynarr_insert(chunks, &bytes[1]);

	return dynarr_len(chunks) - 2;
}

size_t write_i32(Compiler *compiler, int32_t value){
	DynArr *chunks = current_chunks(compiler);
	uint8_t bytes[4];

	descompose_i32(value, bytes);
	dynarr_insert(chunks, &bytes[0]);
	dynarr_insert(chunks, &bytes[1]);
	dynarr_insert(chunks, &bytes[2]);
	dynarr_insert(chunks, &bytes[3]);

	return dynarr_len(chunks) - 4;
}

size_t write_iconst(Compiler *compiler, int64_t value){
	DynArr *iconsts = current_iconsts(compiler);
	size_t iconsts_len = dynarr_len(iconsts);

	if(iconsts_len >= UINT16_MAX){
		internal_error(
			compiler,
			"Number of constants exceeded in '%s' procedure",
			current_fn(compiler)->name
		);
	}

	dynarr_insert(iconsts, &value);

	return write_i16(compiler, (int16_t)(dynarr_len(iconsts) - 1));
}

size_t write_fconst(Compiler *compiler, double value){
	DynArr *fconsts = current_fconsts(compiler);
	size_t fconsts_len = dynarr_len(fconsts);

	if(fconsts_len >= UINT16_MAX){
		internal_error(
			compiler,
			"Number of constants exceeded in '%s' procedure",
			current_fn(compiler)->name
		);
	}

	dynarr_insert(fconsts, &value);

	return write_i16(compiler, (int16_t)(dynarr_len(fconsts) - 1));
}

void update_i16(Compiler *compiler, size_t offset, uint16_t value){
    DynArr *chunks = current_chunks(compiler);
    size_t chunks_len = dynarr_len(chunks);
    uint8_t bytes[2];

    if(offset >= chunks_len){
        internal_error(
			compiler,
			"Index out of bounds while updating chunks in '%s' procedure",
			current_fn(compiler)->name
		);
    }

    descompose_i16(value, bytes);
    dynarr_set_at(chunks, offset, &bytes[0]);
    dynarr_set_at(chunks, offset + 1, &bytes[1]);
}

void write_str(Compiler *compiler, size_t raw_str_len, char *raw_str){
    assert(raw_str != NULL);

    Module *module = current_module(compiler);
	DynArr *str_consts = MODULE_STRINGS(module);
    size_t static_strs_len = dynarr_len(str_consts);

    if(static_strs_len >= UINT16_MAX){}

    DStr str = (DStr){
        .len = raw_str_len,
        .buff = raw_str
    };

    dynarr_insert(str_consts, &str);
    write_i16(compiler, (int16_t)static_strs_len);
}

void write_str_alloc(Compiler *compiler, size_t raw_str_len, char *raw_str){
    assert(raw_str != NULL);

    Module *module = current_module(compiler);
    DynArr *str_consts = MODULE_STRINGS(module);
    size_t static_strs_len = dynarr_len(str_consts);

    char *new_raw_str = MEMORY_ALLOC(compiler->rtallocator, char, raw_str_len + 1);

    memcpy(new_raw_str, raw_str, raw_str_len);

    new_raw_str[raw_str_len] = 0;

    if(static_strs_len >= UINT16_MAX){}

    DStr str = (DStr){
        .len = raw_str_len,
        .buff = new_raw_str
    };

    dynarr_insert(str_consts, &str);
    write_i16(compiler, (int16_t)static_strs_len);
}

void write_location(Compiler *compiler, const Token *token){
	DynArr *chunks = current_chunks(compiler);
	DynArr *locations = current_locations(compiler);
	OpCodeInfo location = {0};

	location.offset = dynarr_len(chunks) - 1;
	location.line = token->line;
    location.filepath = memory_clone_cstr(
    	compiler->rtallocator,
        token->pathname,
      	NULL
    );

	dynarr_insert(locations, &location);
}

void label(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
	CompilationUnit *unit = peek_unit(compiler);
    LZPool *labels_pool = unit->labels_pool;
    Allocator *arena_allocator = compiler->arena_allocator;
    LZOHTable *labels = unit->labels;

    va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(arena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Label *label = lzpool_alloc_backup_count(labels_pool, 64);

    *label = (Label){
        .offset = chunks_len(compiler),
        .name_len = name_len,
        .name = cloned_name
    };

	if(lzohtable_lookup(name_len, cloned_name, labels, NULL)){
		internal_error(
			compiler,
			"Already exists label '%s' in current unit",
			cloned_name
		);
	}

	lzohtable_put(name_len, cloned_name, label, labels, NULL);
}

void mark(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
    CompilationUnit *unit = peek_unit(compiler);
    LZPool *marks_pool = unit->marks_pool;
    Allocator *arena_allocator = compiler->arena_allocator;

	size_t update_offset = write_i16(compiler, 0);
    size_t jmp_offset = chunks_len(compiler);

	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(arena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Mark *mark = lzpool_alloc_backup_count(marks_pool, 64);

    *mark = (Mark){
        .update_offset = update_offset,
        .jump_offset = jmp_offset,
	    .label_name_len = name_len,
	    .label_name = cloned_name
    };

	dynarr_insert_ptr(unit->marks, mark);
}

void jmp(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
    CompilationUnit *unit = peek_unit(compiler);
    LZPool *jmps_pool = unit->jmps_pool;
    Allocator *arena_allocator = compiler->arena_allocator;

	write_chunk(compiler, OP_JMP);
	write_location(compiler, ref_token);
	size_t update_offset = write_i16(compiler, 0);
	size_t jmp_offset = chunks_len(compiler);

	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(arena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_backup_count(jmps_pool, 64);

    *jmp = (Jmp){
        .update_offset = update_offset,
	    .jump_offset = jmp_offset,
	    .label_name_len = name_len,
	    .label_name = cloned_name
    };

	dynarr_insert_ptr(unit->jmps, jmp);
}

void jif(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
    CompilationUnit *unit = peek_unit(compiler);
    LZPool *jmps_pool = unit->jmps_pool;
    Allocator *arena_allocator = compiler->arena_allocator;

	write_chunk(compiler, OP_JIF);
	write_location(compiler, ref_token);
	size_t update_offset = write_i16(compiler, 0);
	size_t jmp_offset = chunks_len(compiler);

	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(arena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_backup_count(jmps_pool, 1024);

    *jmp = (Jmp){
        .update_offset = update_offset,
	    .jump_offset = jmp_offset,
	    .label_name_len = name_len,
	    .label_name = cloned_name
    };

	dynarr_insert_ptr(unit->jmps, jmp);
}

void jit(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
	write_chunk(compiler, OP_JIT);
	write_location(compiler, ref_token);
	size_t update_offset = write_i16(compiler, 0);
	size_t jmp_offset = chunks_len(compiler);

	CompilationUnit *unit = peek_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(compiler->arena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_backup_count(unit->jmps_pool, 1024);

	jmp->update_offset = update_offset;
	jmp->jump_offset = jmp_offset;
	jmp->label_name_len = name_len;
	jmp->label_name = cloned_name;

	dynarr_insert_ptr(unit->jmps, jmp);
}

void or(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
	write_chunk(compiler, OP_OR);
	write_location(compiler, ref_token);
	size_t update_offset = write_i16(compiler, 0);
	size_t jmp_offset = chunks_len(compiler);

	CompilationUnit *unit = peek_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(compiler->arena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_backup_count(unit->jmps_pool, 1024);

	jmp->update_offset = update_offset;
	jmp->jump_offset = jmp_offset;
	jmp->label_name_len = name_len;
	jmp->label_name = cloned_name;

	dynarr_insert_ptr(unit->jmps, jmp);
}

void and(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
	write_chunk(compiler, OP_AND);
	write_location(compiler, ref_token);
	size_t update_offset = write_i16(compiler, 0);
	size_t jmp_offset = chunks_len(compiler);

	CompilationUnit *unit = peek_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(compiler->arena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_backup_count(unit->jmps_pool, 1024);

	jmp->update_offset = update_offset;
	jmp->jump_offset = jmp_offset;
	jmp->label_name_len = name_len;
	jmp->label_name = cloned_name;

	dynarr_insert_ptr(unit->jmps, jmp);
}

void get_local(Compiler *compiler, Token *identifier_token){
    ScopeManager *manager = compiler->manager;

    Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);
    LocalSymbol *local_symbol = (LocalSymbol *)symbol;

    const Scope *symbol_scope = symbol->scope;
    Scope *current_scope = scope_manager_peek(manager);

    assert(IS_LOCAL_SCOPE(current_scope));
    assert(IS_LOCAL_SCOPE(symbol_scope));

    LocalScope *local_current_scope = AS_LOCAL_SCOPE(current_scope);
    const LocalScope *local_symbol_scope = AS_LOCAL_SCOPE(symbol_scope);

    if(local_current_scope->depth > local_symbol_scope->depth){
        depth_t depth_diff = local_current_scope->depth - local_symbol_scope->depth;

        if(depth_diff > 1){
            error(
                compiler,
                identifier_token,
                "Cannot capture locals with more than one jump"
            );
        }

        CompilationUnit *unit = peek_unit(compiler);
        LZOHTable *captured_symbols = unit->captured_symbols;

        lzohtable_put(
            identifier_token->lexeme_len,
            identifier_token->lexeme,
            symbol,
            captured_symbols,
            NULL
        );

        write_chunk(compiler, OP_FOREIGN_GET);
        write_location(compiler, identifier_token);
        write_chunk(compiler, local_symbol->offset);
    }else{
        write_chunk(compiler, OP_LOCAL_GET);
        write_location(compiler, identifier_token);
        write_chunk(compiler, local_symbol->offset);
    }
}

void set_local(Compiler *compiler, Token *identifier_token){
    ScopeManager *manager = compiler->manager;

    Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);
    LocalSymbol *local_symbol = (LocalSymbol *)symbol;

    const Scope *symbol_scope = symbol->scope;
    Scope *current_scope = scope_manager_peek(manager);

    assert(IS_LOCAL_SCOPE(current_scope));
    assert(IS_LOCAL_SCOPE(symbol_scope));

    LocalScope *local_current_scope = AS_LOCAL_SCOPE(current_scope);
    const LocalScope *local_symbol_scope = AS_LOCAL_SCOPE(symbol_scope);

    if(local_current_scope->depth > local_symbol_scope->depth){
        depth_t depth_diff = local_current_scope->depth - local_symbol_scope->depth;

        if(depth_diff > 1){
            error(
                compiler,
                identifier_token,
                "Cannot capture locals with more than one jump"
            );
        }

        CompilationUnit *unit = peek_unit(compiler);
        LZOHTable *captured_symbols = unit->captured_symbols;

        lzohtable_put(
            identifier_token->lexeme_len,
            identifier_token->lexeme,
            symbol,
            captured_symbols,
            NULL
        );

        write_chunk(compiler, OP_FOREIGN_SET);
        write_location(compiler, identifier_token);
        write_chunk(compiler, local_symbol->offset);
    }else{
        write_chunk(compiler, OP_LOCAL_SET);
        write_location(compiler, identifier_token);
        write_chunk(compiler, local_symbol->offset);
    }
}

void compile_expr(Compiler *compiler, Expr *expr){
    ScopeManager *manager = compiler->manager;

    switch(expr->type){
        case EMPTY_EXPR_TYPE:{
			EmptyExpr *empty_expr = expr->sub_expr;

			write_chunk(compiler, OP_EMPTY);
			write_location(compiler, empty_expr->empty_token);

			break;
        }case BOOL_EXPR_TYPE:{
            BoolExpr *bool_expr = expr->sub_expr;

            write_chunk(compiler, bool_expr->value ? OP_TRUE : OP_FALSE);
			write_location(compiler, bool_expr->bool_token);

            break;
        }case INT_EXPR_TYPE:{
            IntExpr *int_expr = expr->sub_expr;
            Token *int_token = int_expr->token;
            int64_t value = *(int64_t *)int_token->literal;

            write_chunk(compiler, OP_INT);
			write_location(compiler, int_token);
            write_iconst(compiler, value);

            break;
        }case FLOAT_EXPR_TYPE:{
			FloatExpr *float_expr = expr->sub_expr;
			Token *float_token = float_expr->token;
			double value = *(double *)float_token->literal;

			write_chunk(compiler, OP_FLOAT);
			write_location(compiler, float_token);
			write_fconst(compiler, value);

			break;
		}case STRING_EXPR_TYPE:{
			StrExpr *str_expr = expr->sub_expr;
            Token *str_token = str_expr->str_token;

			write_chunk(compiler, OP_STRING);
			write_location(compiler, str_token);
			write_str(compiler, str_token->literal_size, str_token->literal);

			break;
		}case TEMPLATE_EXPR_TYPE:{
            TemplateExpr *template_expr = expr->sub_expr;
            Token *template_token = template_expr->template_token;
            DynArr *exprs = template_expr->exprs;

            write_chunk(compiler, OP_START_TEMPLATE);
            write_location(compiler, template_token);

            if(exprs){
                size_t len = dynarr_len(exprs);

                for (size_t i = 0; i < len; i++){
                    Expr *expr = (Expr *)dynarr_get_ptr(exprs, i);

                    compile_expr(compiler, expr);

                    write_chunk(compiler, OP_WRITE_TEMPLATE);
                    write_location(compiler, template_token);
                }
            }

            write_chunk(compiler, OP_END_TEMPLATE);
            write_location(compiler, template_token);

            break;
        }case ANON_EXPR_TYPE:{
            AnonExpr *anon_expr = expr->sub_expr;
            Token *anon_token = anon_expr->anon_token;
            DynArr *params = anon_expr->params;
            DynArr *stmts = anon_expr->stmts;

            size_t params_len = params ? dynarr_len(params) : 0;
            size_t stmts_len = stmts ? dynarr_len(stmts) : 0;

            uint8_t chunk;
            size_t fn_idx;

            Fn *fn = vm_factory_module_fn_create(
                compiler->rtallocator,
                compiler->module,
                "anonymous",
                params_len,
                &fn_idx
            );

            scope_manager_push(manager, FN_SCOPE_KIND);
            CompilationUnit *unit = push_unit(compiler, fn);
            Block *block = push_block(compiler);

            for (size_t i = 0; i < params_len; i++){
                ProcParam *param = DYNARR_GET_PTR_AS(params, ProcParam, i);

                scope_manager_define_local(
                    manager,
                    param->is_mutable,
                    1,
                    param->identifier
                );
            }

            block->stmts_len = stmts_len;

            for (size_t i = 0; i < stmts_len; i++){
                Stmt *stmt = dynarr_get_ptr(stmts, i);

                block->current_stmt = i + 1;

                compile_stmt(compiler, stmt);
            }

            Stmt *last_stmt = stmts_len > 0 ? DYNARR_GET_PTR_AS(stmts, Stmt, stmts_len - 1) : NULL;

            if(last_stmt && last_stmt->type != RETURN_STMT_TYPE){
                write_chunk(compiler, OP_EMPTY);
                write_chunk(compiler, OP_RET);
            }

            LZOHTable *captured_symbols = unit->captured_symbols;
            size_t captured_symbols_len = captured_symbols->n;

            if(captured_symbols_len > 0){
                size_t captured_symbols_counter = 0;
                size_t captured_symbols_m = captured_symbols->m;
                ClosureInf *closure = vm_factory_module_closure_inf_create(
                    compiler->rtallocator,
                    compiler->module,
                    captured_symbols_len,
                    &fn_idx
                );
                uint8_t *locals = closure->locals;

                closure->fn = fn;

                for (size_t i = 0; i < captured_symbols_m && captured_symbols_counter < captured_symbols_len; i++){
                    LZOHTableSlot *slot = &captured_symbols->slots[i];

                    if(!slot->used){
                        continue;
                    }

                    Symbol *symbol = (Symbol *)slot->value;

                    assert(symbol->kind == LOCAL_SYMBOL_KIND);

                    LocalSymbol *local_symbol = (LocalSymbol *)symbol;

                    locals[captured_symbols_counter++] = local_symbol->offset;
                }

                chunk = OP_CLOSURE;
            }else{
                chunk = OP_FN;
            }

            pop_unit(compiler);
            scope_manager_pop(manager);

            write_chunk(compiler, chunk);
            write_location(compiler, anon_token);
            write_i32(compiler, (int32_t)fn_idx);

            break;
        }case IDENTIFIER_EXPR_TYPE:{
            IdentifierExpr *identifier_expr = expr->sub_expr;
            Token *identifier_token = identifier_expr->identifier_token;
            Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

            switch (symbol->kind){
                case LOCAL_SYMBOL_KIND:{
                    get_local(compiler, identifier_token);
                    break;
                }case GLOBAL_SYMBOL_KIND:
                 case FN_SYMBOL_KIND:
                 case MODULE_SYMBOL_KIND:{
                    write_chunk(compiler, OP_GLOBAL_GET);
                    write_location(compiler, identifier_token);
                    write_str_alloc(
                        compiler,
                        identifier_token->lexeme_len,
                        identifier_token->lexeme
                    );

                    break;
                }case NATIVE_FN_SYMBOL_KIND:{
                    write_chunk(compiler, OP_NATIVE_GET);
                    write_location(compiler, identifier_expr->identifier_token);
                    write_str_alloc(
                        compiler,
                        identifier_token->lexeme_len,
                        identifier_token->lexeme
                    );

                    break;
                }default:{
                    assert(0 && "Illegal symbol type");
                    break;
                }
            }

            break;
        }case GROUP_EXPR_TYPE:{
            GroupExpr *group_expr = expr->sub_expr;
            compile_expr(compiler, group_expr->expr);
            break;
        }case CALL_EXPR_TYPE:{
            CallExpr *call_expr = expr->sub_expr;
            Expr *left_expr = call_expr->left_expr;
            DynArr *args = call_expr->args;
            size_t args_count = args ? dynarr_len(args) : 0;

            if(left_expr->type == IDENTIFIER_EXPR_TYPE){
                IdentifierExpr *identifier_expr = left_expr->sub_expr;
                Token *identifier_token = identifier_expr->identifier_token;
                Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

                switch(symbol->kind){
                    case NATIVE_FN_SYMBOL_KIND:{
                        NativeFnSymbol *native_fn_symbol = (NativeFnSymbol *)symbol;

                        if(native_fn_symbol->params_count != args_count){
                            error(
                                compiler,
                                identifier_expr->identifier_token,
                                "Native procedure '%s' declares %"PRIu8" parameter(s), but got %zu argument(s)",
                                native_fn_symbol->name,
                                native_fn_symbol->params_count,
                                args_count
                            );
                        }

                        break;
                    }case FN_SYMBOL_KIND:{
                        FnSymbol *fn_symbol = (FnSymbol *)symbol;

                        if(fn_symbol->params_count != args_count){
                            error(
                                compiler,
                                identifier_expr->identifier_token,
                                "Procedure '%s' declares %"PRIu8" parameter(s), but got %zu argument(s)",
                                symbol->identifier->lexeme,
                                fn_symbol->params_count,
                                args_count
                            );
                        }

                        break;
                    }default:{
                        break;
                    }
                }
            }

            compile_expr(compiler, left_expr);

            for (size_t i = 0; i < args_count; i++){
                Expr *expr = (Expr *)dynarr_get_ptr(args, i);

                compile_expr(compiler, expr);
            }

            write_chunk(compiler, OP_CALL);
            write_location(compiler, call_expr->left_paren);
            write_chunk(compiler, args_count);

            break;
        }case ACCESS_EXPR_TYPE:{
            AccessExpr *access_expr = expr->sub_expr;
            Expr *left_expr = access_expr->left_expr;
            Token *dot_token =  access_expr->dot_token;
            Token *symbol_token = access_expr->symbol_token;

            compile_expr(compiler, left_expr);

            write_chunk(compiler, OP_ACCESS);
			write_location(compiler, dot_token);
            write_str_alloc(compiler, symbol_token->lexeme_len, symbol_token->lexeme);

            break;
        }case INDEX_EXPR_TYPE:{
            IndexExpr *index_expr = expr->sub_expr;

            compile_expr(compiler, index_expr->index_expr);
            compile_expr(compiler, index_expr->target_expr);

            write_chunk(compiler, OP_INDEX);
            write_location(compiler, index_expr->left_square_token);

            break;
        }case UNARY_EXPR_TYPE:{
            UnaryExpr *unary_expr = expr->sub_expr;
            Token *operator_token = unary_expr->operator_token;

            compile_expr(compiler, unary_expr->right);

            switch(operator_token->type){
                case MINUS_TOKTYPE:{
                    write_chunk(compiler, OP_NNOT);

                    break;
                }case EXCLAMATION_TOKTYPE:{
                    write_chunk(compiler, OP_NOT);

                    break;
                }case NOT_BITWISE_TOKTYPE:{
                    write_chunk(compiler, OP_BNOT);

                    break;
                }default:{
                    assert("Illegal token type");

                    break;
                }
            }

			write_location(compiler, operator_token);

            break;
        }case BINARY_EXPR_TYPE:{
            BinaryExpr *binary_expr = expr->sub_expr;
            Token *operator = binary_expr->operator;

            compile_expr(compiler, binary_expr->left);
            compile_expr(compiler, binary_expr->right);

            switch (operator->type){
                case PLUS_TOKTYPE:{
                    write_chunk(compiler, OP_ADD);
                    break;
                }case MINUS_TOKTYPE:{
                    write_chunk(compiler, OP_SUB);
                    break;
                }case ASTERISK_TOKTYPE:{
                    write_chunk(compiler, OP_MUL);
                    break;
                }case SLASH_TOKTYPE:{
                    write_chunk(compiler, OP_DIV);
                    break;
                }case MOD_TOKTYPE:{
					write_chunk(compiler, OP_MOD);
					break;
				}default:{
                    assert("Illegal token type");
                    break;
                }
            }

			write_location(compiler, operator);

            break;
        }case MULSTR_EXPR_TYPE:{
            MulStrExpr *mulstr_expr = expr->sub_expr;

            compile_expr(compiler, mulstr_expr->left);
            compile_expr(compiler, mulstr_expr->right);

            write_chunk(compiler, OP_MULSTR);
            write_location(compiler, mulstr_expr->operator_token);

            break;
        } case CONCAT_EXPR_TYPE:{
            ConcatExpr *concat_expr = expr->sub_expr;

            compile_expr(compiler, concat_expr->left);
            compile_expr(compiler, concat_expr->right);

            write_chunk(compiler, OP_CONCAT);
            write_location(compiler, concat_expr->operator_token);

            break;
        }case BITWISE_EXPR_TYPE:{
            BitWiseExpr *bitwise_expr = expr->sub_expr;
            Token *operator_token = bitwise_expr->operator_token;

            compile_expr(compiler, bitwise_expr->left);
            compile_expr(compiler, bitwise_expr->right);

            switch (operator_token->type){
                case LEFT_SHIFT_TOKTYPE:{
                    write_chunk(compiler, OP_LSH);
                    break;
                }case RIGHT_SHIFT_TOKTYPE:{
                    write_chunk(compiler, OP_RSH);
                    break;
                }case AND_BITWISE_TOKTYPE:{
                    write_chunk(compiler, OP_BAND);
                    break;
                }case XOR_BITWISE_TOKTYPE:{
                    write_chunk(compiler, OP_BXOR);
                    break;
                }case OR_BITWISE_TOKTYPE:{
                    write_chunk(compiler, OP_BOR);
                    break;
                }default:{
                    assert("Illegal token type");
                    break;
                }
            }

            write_location(compiler, operator_token);

            break;
        }case COMPARISON_EXPR_TYPE:{
            ComparisonExpr *comparison_expr = expr->sub_expr;
            Token *operator_token = comparison_expr->operator_token;

            compile_expr(compiler, comparison_expr->left);
            compile_expr(compiler, comparison_expr->right);

            switch (operator_token->type){
                case LESS_TOKTYPE:{
                    write_chunk(compiler, OP_LT);
                    break;
                }case GREATER_TOKTYPE:{
                    write_chunk(compiler, OP_GT);
                    break;
                }case LESS_EQUALS_TOKTYPE:{
                    write_chunk(compiler, OP_LE);
                    break;
                }case GREATER_EQUALS_TOKTYPE:{
                    write_chunk(compiler, OP_GE);
                    break;
                }case EQUALS_EQUALS_TOKTYPE:{
                    write_chunk(compiler, OP_EQ);
                    break;
                }case NOT_EQUALS_TOKTYPE:{
                    write_chunk(compiler, OP_NE);
                    break;
                }default:{
                    assert("Illegal token type");
                    break;
                }
            }

			write_location(compiler, operator_token);

            break;
        }case LOGICAL_EXPR_TYPE:{
            LogicalExpr *logical_expr = expr->sub_expr;
            Token *operator_token = logical_expr->operator;
            Expr *right_expr = logical_expr->right;

            compile_expr(compiler, logical_expr->left);

            switch (operator_token->type){
                case OR_TOKTYPE:{
                    uint32_t id = generate_id(compiler);

                    or(compiler, operator_token, "OR_END_%"PRId32, id);
                    compile_expr(compiler, right_expr);
                    label(compiler, operator_token, "OR_END_%"PRId32, id);

                    break;
                }case AND_TOKTYPE:{
                    uint32_t id = generate_id(compiler);

                    and(compiler, operator_token, "AND_END_%"PRId32, id);
                    compile_expr(compiler, right_expr);
                    label(compiler, operator_token, "AND_END_%"PRId32, id);

                    break;
                }default:{
                    assert("Illegal token type");
                    break;
                }
            }

			write_location(compiler, operator_token);

            break;
        }case ASSIGN_EXPR_TYPE:{
            AssignExpr *assign_expr = expr->sub_expr;
            Expr *left_expr = assign_expr->left_expr;
            Token *equals_token = assign_expr->equals_token;
            Expr *value_expr = assign_expr->value_expr;

            if(value_expr->type == IDENTIFIER_EXPR_TYPE){
                IdentifierExpr *identifier_expr = value_expr->sub_expr;
                Token *identifier_token = identifier_expr->identifier_token;
                Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

                if(symbol->kind == MODULE_SYMBOL_KIND){
                    error(
                        compiler,
                        equals_token,
                        "Cannot assign modules to variables"
                    );
                }
            }

            switch (left_expr->type){
                case IDENTIFIER_EXPR_TYPE:{
                    IdentifierExpr *identifier_expr = left_expr->sub_expr;
                    Token *identifier_token = identifier_expr->identifier_token;
                    Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

                    switch (symbol->kind){
                        case LOCAL_SYMBOL_KIND:{
                            LocalSymbol *local_symbol = (LocalSymbol *)symbol;

                            if(!local_symbol->is_mutable && local_symbol->is_initialized){
                                error(
                                    compiler,
                                    assign_expr->equals_token,
                                    "Local symbol '%s' is immutable and already initialized",
                                    identifier_token->lexeme
                                );
                            }

                            compile_expr(compiler, value_expr);
                            set_local(compiler, identifier_token);

                            break;
                        }case GLOBAL_SYMBOL_KIND:{
                            GlobalSymbol *global_symbol = (GlobalSymbol *)symbol;

                            if(!global_symbol->is_mutable){
                                error(
                                    compiler,
                                    assign_expr->equals_token,
                                    "Global variable '%s' declared as immutable",
                                    identifier_token->lexeme
                                );
                            }

                            compile_expr(compiler, value_expr);

                            write_chunk(compiler, OP_GLOBAL_SET);
                            write_location(compiler, equals_token);
                            write_str_alloc(compiler, identifier_token->lexeme_len, identifier_token->lexeme);

                            break;
                        }case FN_SYMBOL_KIND:{
                            error(
                                compiler,
                                assign_expr->equals_token,
                                "Procedures name cannot be re-assigned",
                                identifier_token->lexeme
                            );

                            break;
                        }case MODULE_SYMBOL_KIND:{
                            error(
                                compiler,
                                assign_expr->equals_token,
                                "Modules name cannot be re-assigned",
                                identifier_token->lexeme
                            );

                            break;
                        }default:{
                            error(
                                compiler,
                                assign_expr->equals_token,
                                "Illegal assignation target"
                            );

                            break;
                        }
                    }

                    break;
                }case INDEX_EXPR_TYPE:{
               		IndexExpr *index_expr = left_expr->sub_expr;

                	compile_expr(compiler, value_expr);
                	compile_expr(compiler, index_expr->index_expr);
                 	compile_expr(compiler, index_expr->target_expr);

                 	write_chunk(compiler, OP_ARRAY_SET);
                 	write_location(compiler, equals_token);

                  	break;
                }case ACCESS_EXPR_TYPE:{
               		AccessExpr *access_expr = left_expr->sub_expr;
                	Token *symbol_token = access_expr->symbol_token;

                 	compile_expr(compiler, value_expr);
                  	compile_expr(compiler, access_expr->left_expr);

                 	write_chunk(compiler, OP_RECORD_SET);
                 	write_location(compiler, equals_token);
                 	write_str_alloc(
                  		compiler,
                   		symbol_token->lexeme_len,
                    	symbol_token->lexeme
                  	);

                  	break;
                }default:{
                    error(
                        compiler,
                        assign_expr->equals_token,
                        "Illegal assignment target"
                    );

                    break;
                }
            }

            break;
        }case COMPOUND_EXPR_TYPE:{
       		CompoundExpr *compound_expr = (CompoundExpr *)expr->sub_expr;
            Expr *left_expr = compound_expr->left_expr;
			Token *operator_token = compound_expr->operator_token;
			Expr *right_expr = compound_expr->right_expr;

            switch (left_expr->type){
                case IDENTIFIER_EXPR_TYPE:{
                    IdentifierExpr *identifier_expr = (IdentifierExpr *)left_expr->sub_expr;
                    Token *identifier_token = identifier_expr->identifier_token;
                    Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

                    switch (symbol->kind) {
                   		case LOCAL_SYMBOL_KIND:{
                   			LocalSymbol *local_symbol = (LocalSymbol *)symbol;

                    		if(!local_symbol->is_mutable && local_symbol->is_initialized){
	                        	error(
	                            	compiler,
	                              	operator_token,
		                            "Local symbol '%s' is immutable and already initialized",
	                              	identifier_token->lexeme
	                          	);
	                      	}

                      		write_chunk(compiler, OP_LOCAL_GET);
		                    write_location(compiler, identifier_token);
		                    write_chunk(compiler, local_symbol->offset);

                     		break;
                     	}case GLOBAL_SYMBOL_KIND:{
                      		GlobalSymbol *global_symbol = (GlobalSymbol *)symbol;

                       		if(!global_symbol->is_mutable){
	                        	error(
	                            	compiler,
	                              	operator_token,
		                            "Local symbol '%s' declared as immutable",
	                              	identifier_token->lexeme
	                          	);
	                      	}

                        	write_chunk(compiler, OP_GLOBAL_GET);
	                        write_location(compiler, identifier_token);
	                        write_str_alloc(
								compiler,
								identifier_token->lexeme_len,
								identifier_token->lexeme
							);

                      		break;
                      	}default:{
                       		assert(0 && "Illegal symbol type");
                       		break;
                       	}
                    }

                    compile_expr(compiler, right_expr);

                    switch(operator_token->type){
                        case COMPOUND_ADD_TOKTYPE:{
                            write_chunk(compiler, OP_ADD);

                            break;
                        }case COMPOUND_SUB_TOKTYPE:{
                            write_chunk(compiler, OP_SUB);

                            break;
                        }case COMPOUND_MUL_TOKTYPE:{
                            write_chunk(compiler, OP_MUL);

                            break;
                        }case COMPOUND_DIV_TOKTYPE:{
                            write_chunk(compiler, OP_DIV);

                            break;
                        }default:{
                            assert("Illegal compound type");

                            break;
                        }
                    }

                    write_location(compiler, operator_token);

                    switch (symbol->kind) {
                   		case LOCAL_SYMBOL_KIND:{
                   			LocalSymbol *local_symbol = (LocalSymbol *)symbol;

                    		write_chunk(compiler, OP_LOCAL_SET);
                      		write_location(compiler, identifier_token);
                        	write_chunk(compiler, local_symbol->offset);

                     		break;
                     	}case GLOBAL_SYMBOL_KIND:{
                        	write_chunk(compiler, OP_GLOBAL_SET);
                         	write_location(compiler, identifier_token);
                         	write_str_alloc(
                          		compiler,
                            	identifier_token->lexeme_len,
                             	identifier_token->lexeme
                          	);

                      		break;
                      	}default:{
                       		assert(0 && "Illegal symbol type");
                       		break;
                       	}
                    }

                    break;
                }case INDEX_EXPR_TYPE:{
                	compile_expr(compiler, left_expr);

                 	IndexExpr *index_expr = left_expr->sub_expr;

                  	compile_expr(compiler, right_expr);

                 	switch(operator_token->type){
                    	case COMPOUND_ADD_TOKTYPE:{
                        	write_chunk(compiler, OP_ADD);

                            break;
                      	}case COMPOUND_SUB_TOKTYPE:{
                          	write_chunk(compiler, OP_SUB);

                            break;
                       	}case COMPOUND_MUL_TOKTYPE:{
                          	write_chunk(compiler, OP_MUL);

                            break;
                        }case COMPOUND_DIV_TOKTYPE:{
                          	write_chunk(compiler, OP_DIV);

                            break;
                        }default:{
                          	assert("Illegal compound type");

                            break;
                        }
                  	}

                  	write_location(compiler, operator_token);

                  	compile_expr(compiler, index_expr->index_expr);
                   	compile_expr(compiler, index_expr->target_expr);

                   	write_chunk(compiler, OP_ARRAY_SET);
                   	write_location(compiler, operator_token);

                 	break;
                }case ACCESS_EXPR_TYPE:{
                    compile_expr(compiler, left_expr);

                    AccessExpr *access_expr = left_expr->sub_expr;
                    Expr *left_expr = access_expr->left_expr;
                    Token *dot_token = access_expr->dot_token;
                    Token *symbol_token = access_expr->symbol_token;

                    compile_expr(compiler, right_expr);

                    switch(operator_token->type){
                        case COMPOUND_ADD_TOKTYPE:{
                            write_chunk(compiler, OP_ADD);

                            break;
                        }case COMPOUND_SUB_TOKTYPE:{
                            write_chunk(compiler, OP_SUB);

                            break;
                        }case COMPOUND_MUL_TOKTYPE:{
                            write_chunk(compiler, OP_MUL);

                            break;
                        }case COMPOUND_DIV_TOKTYPE:{
                            write_chunk(compiler, OP_DIV);

                            break;
                        }default:{
                            assert("Illegal compound type");

                            break;
                        }
                    }

                    write_location(compiler, operator_token);

                    compile_expr(compiler, left_expr);

                    write_chunk(compiler, OP_RECORD_SET);
                    write_location(compiler, dot_token);
                    write_str_alloc(
                    	compiler,
                     	symbol_token->lexeme_len,
                      	symbol_token->lexeme
                    );

                    break;
                }default:{
                    error(
                    	compiler,
                     	operator_token,
                      	"Illegal compound operator left operand"
                    );
                    break;
                }
            }

           	break;
		}case ARRAY_EXPR_TYPE:{
            ArrayExpr *array_expr = expr->sub_expr;
            Token *array_token = array_expr->array_token;
            Expr *len_expr = array_expr->len_expr;
            DynArr *values = array_expr->values;

            if(len_expr){
                compile_expr(compiler, len_expr);
                write_chunk(compiler, OP_ARRAY);
                write_location(compiler, array_token);
            }else{
                const size_t array_len = values ? dynarr_len(values) : 0;

                write_chunk(compiler, OP_INT);
                write_location(compiler, array_token);
                write_iconst(compiler, (int64_t)array_len);

                write_chunk(compiler, OP_ARRAY);
                write_location(compiler, array_token);

                for (size_t i = 0 ; i < array_len; i++){
                    Expr *expr = (Expr *)dynarr_get_ptr(values, i);

                    compile_expr(compiler, expr);

                    write_chunk(compiler, OP_INIT_ARRAY);
                    write_location(compiler, array_token);
                    write_i16(compiler, (int16_t)i);
                }
            }

            break;
        }case LIST_EXPR_TYPE:{
			ListExpr *list_expr = expr->sub_expr;
			Token *list_token = list_expr->list_token;
			DynArr *exprs = list_expr->exprs;

            write_chunk(compiler, OP_LIST);
			write_location(compiler, list_token);

            if(exprs){
                size_t len = dynarr_len(exprs);

				for(size_t i = 0; i < len; i++){
                    Expr *expr = dynarr_get_ptr(exprs, i);

                    compile_expr(compiler, expr);

                    write_chunk(compiler, OP_INIT_LIST);
                    write_location(compiler, list_token);
				}
			}

			break;
		}case DICT_EXPR_TYPE:{
            DictExpr *dict_expr = expr->sub_expr;
            DynArr *key_values = dict_expr->key_values;

            write_chunk(compiler, OP_DICT);
			write_location(compiler, dict_expr->dict_token);

            if(key_values){
                size_t len = dynarr_len(key_values);

                for (size_t i = 0; i < len; i++){
                    DictKeyValue *key_value = dynarr_get_ptr(key_values, i);
                    Expr *key = key_value->key;
                    Expr *value = key_value->value;

                    compile_expr(compiler, key);
                    compile_expr(compiler, value);

                    write_chunk(compiler, OP_INIT_DICT);
			        write_location(compiler, dict_expr->dict_token);
                }
            }

            break;
        }case RECORD_EXPR_TYPE:{
			RecordExpr *record_expr = expr->sub_expr;
            Token *record_token = record_expr->record_token;
			DynArr *key_values = record_expr->key_values;
            size_t key_values_len = key_values ? dynarr_len(key_values) : 0;

            write_chunk(compiler, OP_RECORD);
			write_location(compiler, record_token);
            write_i16(compiler, (int16_t)key_values_len);

			for(size_t i = 0; i < key_values_len; i++){
                RecordExprValue *key_value = dynarr_get_ptr(key_values, i);
                Token *key = key_value->key;
                Expr *value = key_value->value;

                compile_expr(compiler, value);

                write_chunk(compiler, OP_INIT_RECORD);
                write_location(compiler, record_token);
                write_str_alloc(compiler, key->lexeme_len, key->lexeme);
            }

			break;
		}case IS_EXPR_TYPE:{
			IsExpr *is_expr = expr->sub_expr;

			compile_expr(compiler, is_expr->left_expr);

			write_chunk(compiler, OP_IS);
			write_location(compiler, is_expr->is_token);

			switch(is_expr->type_token->type){
				case EMPTY_TOKTYPE:{
					write_chunk(compiler, 0);

                    break;
				}case BOOL_TOKTYPE:{
					write_chunk(compiler, 1);

                    break;
				}case INT_TOKTYPE:{
					write_chunk(compiler, 2);

                    break;
				}case FLOAT_TOKTYPE:{
					write_chunk(compiler, 3);

                    break;
				}case STR_TOKTYPE:{
					write_chunk(compiler, 4);

                    break;
				}case ARRAY_TOKTYPE:{
					write_chunk(compiler, 5);

                    break;
				}case LIST_TOKTYPE:{
					write_chunk(compiler, 6);

                    break;
				}case DICT_TOKTYPE:{
					write_chunk(compiler, 7);

                    break;
				}case RECORD_TOKTYPE:{
                    write_chunk(compiler, 8);

                    break;
                }case PROC_TOKTYPE:{
                    write_chunk(compiler, 9);

                    break;
                }default:{
					assert("Illegal type value");

                    break;
				}
			}

			break;
		}case TENARY_EXPR_TYPE:{
		    TenaryExpr *tenary_expr = expr->sub_expr;
		    Token *mark_token = tenary_expr->mark_token;
            int32_t id = generate_id(compiler);

		    compile_expr(compiler, tenary_expr->condition);
            jif(compiler, mark_token, "TENARY_RIGHT_%"PRId32, id);

            compile_expr(compiler, tenary_expr->left);
            jmp(compiler, mark_token, "TENARY_END_%"PRId32, id);

            label(compiler, mark_token, "TENARY_RIGHT_%"PRId32, id);
            compile_expr(compiler, tenary_expr->right);

            label(compiler, mark_token, "TENARY_END_%"PRId32, id);

		    break;
		}default:{
            assert("Illegal expression type");
            break;
        }
    }
}

void propagate_return(Compiler *compiler, Scope *scope){
	Scope *current = scope->prev;

    while(current && IS_BLOCK_SCOPE(current)){
    	AS_LOCAL_SCOPE(current)->returned = 1;
       	current = current->prev;
    }

    if(IS_LOCAL_SCOPE(current)){
  		AS_LOCAL_SCOPE(current)->returned = 1;
    }
}

int compile_if_branch(
    Compiler *compiler,
    IfStmtBranch *if_branch,
    ScopeKind type,
    int32_t id,
    int32_t which
){
    Token *branch_token = if_branch->branch_token;
    Expr *condition = if_branch->condition_expr;
    DynArr *stmts = if_branch->stmts;
    size_t stmts_len = stmts ? dynarr_len(stmts) : 0;

    compile_expr(compiler, condition);
    jif(compiler, branch_token, ".IFB(%"PRId32")_END_%"PRId32, id, which);

    Scope *scope = scope_manager_push(compiler->manager, type);
    Block *block = push_block(compiler);
    uint8_t returned = 0;

    block->stmts_len = stmts_len;

    for (size_t i = 0; i < stmts_len; i++){
   		if(AS_LOCAL_SCOPE(scope)->returned){
  			error(
     			compiler,
        		branch_token,
          		"Cannot exists statements after the scope returned"
    		);
    	}

        Stmt *stmt = dynarr_get_ptr(stmts, i);
        block->current_stmt = i + 1;

        compile_stmt(compiler, stmt);
    }

    pop_locals(compiler);

    jmp(compiler, branch_token, ".IF(%"PRId32")_END", id);
    label(compiler, branch_token, ".IFB(%"PRId32")_END_%"PRId32, id, which);

    returned = AS_LOCAL_SCOPE(scope)->returned;

    pop_block(compiler);
    scope_manager_pop(compiler->manager);

    return returned;
}

int import_native(Compiler *compiler, const Token *name_token, const Token *alt_name_token){
	if(strcmp("os", name_token->lexeme) == 0){
		if(!os_native_module){
			os_module_init(compiler->rtallocator);

			vm_factory_module_globals_add_obj(
				current_module(compiler),
				(Obj *)vm_factory_native_module_obj_create(
					compiler->rtallocator,
					os_native_module
				),
				alt_name_token ? alt_name_token->lexeme : name_token->lexeme,
				PRIVATE_GLOBAL_VALUE_TYPE
			);
		}

		return 1;
	}

	if(strcmp("math", name_token->lexeme) == 0){
		if(!math_native_module){
			math_module_init(compiler->rtallocator);

			vm_factory_module_globals_add_obj(
				current_module(compiler),
				(Obj *)vm_factory_native_module_obj_create(
					compiler->rtallocator,
					math_native_module
				),
				alt_name_token ? alt_name_token->lexeme : name_token->lexeme,
				PRIVATE_GLOBAL_VALUE_TYPE
			);
		}

		return 1;
	}

	if(strcmp("time", name_token->lexeme) == 0){
		if(!time_native_module){
			time_module_init(compiler->rtallocator);

			vm_factory_module_globals_add_obj(
				current_module(compiler),
				(Obj *)vm_factory_native_module_obj_create(
					compiler->rtallocator,
					time_native_module
				),
				alt_name_token ? alt_name_token->lexeme : name_token->lexeme,
				PRIVATE_GLOBAL_VALUE_TYPE
			);
		}

		return 1;
	}

	return 0;
}

StatusCode resolve_import_names(
    const Allocator *allocator,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    Token *import_token,
    DynArr *names_tokens,
    DStr **out_main_search_pathname,
    char **out_source_pathname
){
    StatusCode result = OK_STATUS_CODE;
    LZBStr *lzbstr = MEMORY_LZBSTR(allocator);
    size_t names_len = dynarr_len(names_tokens);

    lzbstr_grow_by(lzbstr, 1024);

    for (size_t i = 0; i < names_len; i++){
        Token *name_token = dynarr_get_ptr(names_tokens, i);

        lzbstr_append(lzbstr, name_token->lexeme);

        if(i + 1 < names_len){
            lzbstr_append(lzbstr, "/");
        }
    }

    lzbstr_append(lzbstr, ".ze");

    char *partial_pathname = NULL;

    lzbstr_rclone_buff(lzbstr, (LZBStrAllocator *)allocator, NULL, &partial_pathname);
    lzbstr_reset(lzbstr);

    size_t search_pathnames_len = dynarr_len(search_pathnames);
    DStr *search_pathname = NULL;
    char *source_pathname = NULL;

    for (size_t i = 0; i < search_pathnames_len + 1; i++){
        search_pathname = i == 0 ? main_search_pathname : DYNARR_GET_PTR_AS(search_pathnames, DStr, i - 1);

        lzbstr_append(lzbstr, search_pathname->buff);

        if(search_pathname->buff[search_pathname->len - 1] == '/'){
            lzbstr_append(lzbstr, partial_pathname);
        }else{
            lzbstr_append_args(lzbstr, "/%s", partial_pathname);
        }

        const char *complete_pathname = lzbstr_value(lzbstr);

        if(utils_files_exists(complete_pathname)){
            if(utils_files_is_directory(complete_pathname)){
                result = FILE_IS_DIRECTORY_ERR_STATUS_CODE;

                goto END;
            }

            lzbstr_rclone_buff(lzbstr, (LZBStrAllocator *)allocator, NULL, &source_pathname);

            char *parent_pathname = utils_files_parent_pathname(allocator, source_pathname);
            DStr *new_main_search_pathname = MEMORY_ALLOC(allocator, DStr, 1);

            *new_main_search_pathname = (DStr){
                .len = strlen(parent_pathname),
                .buff = parent_pathname
            };

            main_search_pathname = new_main_search_pathname;

            break;
        }

        lzbstr_reset(lzbstr);
    }

    if(!source_pathname){
        result = MODULE_NOT_FOUND_ERR_STATUS_CODE;

        goto END;
    }

    *out_main_search_pathname = main_search_pathname;
    *out_source_pathname = source_pathname;

END:
    lzbstr_destroy(lzbstr);

    return result;
}

StatusCode import_module(
    LZOHTable *keywords,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    LZOHTable *default_natives,
    LZOHTable *modules,
    LZArena *compiler_arena,
    const Allocator *ctallocator,
    const Allocator *rtallocator,
    const Allocator *pass_allocator,
    Allocator *compiler_arena_allocator,
    const char *pathname,
    const char *name,
    Module **out_module
){
    DStr *source = utils_read_source(pathname, ctallocator);
    DynArr *tokens = MEMORY_DYNARR_PTR(ctallocator);
    DynArr *stmts = MEMORY_DYNARR_PTR(ctallocator);
    DynArr *procs_prototypes = MEMORY_DYNARR_TYPE(ctallocator, ProcPrototype);
    ScopeManager *manager = scope_manager_create(ctallocator);
    Lexer *lexer = lexer_create(ctallocator, rtallocator);
    Parser *parser = parser_create(ctallocator);
    Compiler *compiler = compiler_create(ctallocator, rtallocator);

    if(lexer_lex(lexer, source, pathname, keywords, tokens)){
        return IMPORT_FAILED_ERR_STATUS_CODE;
    }

    if(parser_parse(tokens, procs_prototypes, stmts, parser)){
        return IMPORT_FAILED_ERR_STATUS_CODE;
    }

    Module *module = compiler_import(
        compiler,
        keywords,
        main_search_pathname,
        search_pathnames,
        default_natives,
        procs_prototypes,
        manager,
        modules,
        compiler_arena,
        pass_allocator,
        compiler_arena_allocator,
        stmts,
        pathname,
        name
    );

    if(!module){
        return IMPORT_FAILED_ERR_STATUS_CODE;
    }

    if(out_module){
        *out_module = module;
    }

    return OK_STATUS_CODE;
}

Token *clone_token(const Token *token, const Allocator *allocator){
    char *cloned_lexeme = memory_clone_cstr(allocator, token->lexeme, NULL);
    Token *cloned_token = MEMORY_ALLOC(allocator, Token, 1);

    *cloned_token = *token;
    cloned_token->lexeme = cloned_lexeme;
    cloned_token->literal = NULL;
    cloned_token->pathname = NULL;
    cloned_token->extra = NULL;

    return cloned_token;
}

Symbol *clone_symbol(const Symbol *symbol, const Allocator *allocator){
    switch (symbol->kind){
        case GLOBAL_SYMBOL_KIND:{
            const GlobalSymbol *global_symbol = (GlobalSymbol *)symbol;
            GlobalSymbol *cloned_global_symbol = MEMORY_ALLOC(allocator, GlobalSymbol, 1);

            *cloned_global_symbol = *global_symbol;
            cloned_global_symbol->symbol.identifier = clone_token(global_symbol->symbol.identifier, allocator);
            cloned_global_symbol->symbol.scope = NULL;

            return (Symbol *)cloned_global_symbol;
        }case FN_SYMBOL_KIND:{
            const FnSymbol *fn_symbol = (FnSymbol *)symbol;
            FnSymbol *cloned_fn_symbol = MEMORY_ALLOC(allocator, FnSymbol, 1);

            *cloned_fn_symbol = *fn_symbol;
            cloned_fn_symbol->symbol.identifier = clone_token(fn_symbol->symbol.identifier, allocator);
            cloned_fn_symbol->symbol.scope = NULL;

            return (Symbol *)cloned_fn_symbol;
        }default:{
            return NULL;
        }
    }
}

void expr_stmt(Compiler *compiler, ExprStmt *expr_stmt){
    Expr *sub_expr = expr_stmt->expr;

    compile_expr(compiler, sub_expr);
    write_chunk(compiler, OP_POP);
}

void var_decl_stmt(Compiler *compiler, VarDeclStmt *var_decl_stmt){
    ScopeManager *manager = compiler->manager;

    uint8_t is_mutable = var_decl_stmt->is_mutable;
    uint8_t is_initialized = var_decl_stmt->is_initialized;
    Token *identifier_token = var_decl_stmt->identifier_token;
    Expr *initial_value_expr = var_decl_stmt->initial_value_expr;

    if(scope_manager_exists_procedure_name(
        manager,
        identifier_token->lexeme_len,
        identifier_token->lexeme
    )){
        error(
            compiler,
            identifier_token,
            "Cannot shadow procedures name"
        );
    }

    if(initial_value_expr){
        compile_expr(compiler, initial_value_expr);
    }else{
        write_chunk(compiler, OP_EMPTY);
    }

    if(scope_manager_is_global_scope(manager)){
        if(!is_mutable && !is_initialized){
            error(
                compiler,
                identifier_token,
                "Immutable global variables must be initialized in declaration place"
            );
        }

        scope_manager_define_global(
            manager,
            is_mutable,
            identifier_token
        );

        write_chunk(compiler, OP_GLOBAL_DEF);
        write_location(compiler, identifier_token);
        write_str_alloc(
            compiler,
            identifier_token->lexeme_len,
            identifier_token->lexeme
        );
    }else{
        scope_manager_define_local(
            manager,
            is_mutable,
            is_initialized,
            identifier_token
        );
    }
}

void block_stmt(Compiler *compiler, BlockStmt *block_stmt){
    ScopeManager *manager = compiler->manager;

    DynArr *stmts = block_stmt->stmts;

    size_t stmts_len = stmts ? dynarr_len(stmts) : 0;

    if(scope_manager_is_global_scope(manager)){
        error(
            compiler,
            block_stmt->left_bracket_token,
            "Block statements not allowed in global scope"
        );
    }

    Scope *scope = scope_manager_push(manager, BLOCK_SCOPE_KIND);
    Block *block = push_block(compiler);

    block->stmts_len = stmts_len;

    for (size_t i = 0; i < stmts_len; i++){
        if(AS_LOCAL_SCOPE(scope)->returned){
            error(
                compiler,
                block_stmt->left_bracket_token,
                "Cannot exists statements after the scope returned"
            );
        }

        Stmt *stmt = dynarr_get_ptr(stmts, i);
        block->current_stmt = i + 1;

        compile_stmt(compiler, stmt);
    }

    propagate_return(compiler, scope);

    pop_locals(compiler);
    pop_block(compiler);
    scope_manager_pop(manager);
}

void if_stmt(Compiler *compiler, IfStmt *if_stmt){
    ScopeManager *manager = compiler->manager;

    IfStmtBranch *if_branch = if_stmt->if_branch;
    DynArr *elif_branches = if_stmt->elif_branches;
    DynArr *else_stmts = if_stmt->else_stmts;
    size_t elif_branches_len = elif_branches ? dynarr_len(elif_branches) : 0;
    int32_t if_id = generate_id(compiler);

    size_t branches_len = 1 + (elif_branches ? dynarr_len(elif_branches) : 0) + (else_stmts ? 1 : 0);
    uint16_t returns = compile_if_branch(compiler, if_branch, IF_SCOPE_KIND, if_id, 0) ? 1 : 0;

    for (size_t i = 0; i < elif_branches_len; i++){
        IfStmtBranch *elif_branch = dynarr_get_ptr(elif_branches, i);

        if(compile_if_branch(
            compiler,
            elif_branch,
            ELIF_SCOPE_KIND,
            if_id,
            i + 1
        )){
            returns++;
        }
    }

    if(else_stmts){
        size_t else_stmts_len = dynarr_len(else_stmts);

        Scope *scope = scope_manager_push(manager, ELSE_SCOPE_KIND);
        Block *block = push_block(compiler);

        block->current_stmt = else_stmts_len;

        for (size_t i = 0; i < else_stmts_len; i++){
            if(AS_LOCAL_SCOPE(scope)->returned){
                error(
                    compiler,
                    if_branch->branch_token,
                    "Cannot exists statements after the scope returned"
                );
            }

            Stmt *stmt = dynarr_get_ptr(else_stmts, i);
            block->current_stmt = i + 1;

            compile_stmt(compiler, stmt);
        }

        if(AS_LOCAL_SCOPE(scope)->returned){
            returns++;
        }

        if(returns == branches_len){
            propagate_return(compiler, scope);
        }

        pop_block(compiler);
        pop_locals(compiler);
        scope_manager_pop(manager);
    }

    label(compiler, if_branch->branch_token, ".IF(%"PRId32")_END", if_id);
}

void stop_stmt(Compiler *compiler, StopStmt *stop_stmt){
    ScopeManager *manager = compiler->manager;

    Token *stop_token = stop_stmt->stop_token;

    Scope *scope = scope_manager_is_loop(manager);

    if(scope){
        size_t locals_count = scope->symbols->n;

        assert(locals_count <= UINT8_MAX);

        write_chunk(compiler, OP_OFFSET);
        write_location(compiler, stop_token);
        write_chunk(compiler, (uint8_t)locals_count);

        if(scope->type == WHILE_SCOPE_KIND){
            jmp(compiler, stop_token, ".WHILE_%"PRId32":END", peek_loop(compiler)->id);

            return;
        }

        if(scope->type == FOR_SCOPE_KIND){
            jmp(compiler, stop_token, ".FOR_%"PRId32":END", peek_loop(compiler)->id);

            return;
        }

        assert(0 && "Illegal scope kind");
    }

    error(
        compiler,
        stop_token,
        "Stop statements only allowed in while and for loops"
    );
}

void continue_stmt(Compiler *compiler, ContinueStmt *continue_stmt){
    ScopeManager *manager = compiler->manager;

    Token *continue_token = continue_stmt->continue_token;

    Scope *scope = scope_manager_is_loop(manager);

    if(scope){
        size_t locals_count = scope->symbols->n;

        assert(locals_count <= UINT8_MAX);

        write_chunk(compiler, OP_OFFSET);
        write_location(compiler, continue_token);
        write_chunk(compiler, (uint8_t)locals_count);

        if(scope->type == WHILE_SCOPE_KIND){
            jmp(compiler, continue_token, ".WHILE_%"PRId32":CONDITION", peek_loop(compiler)->id);

            return;
        }

        if(scope->type == FOR_SCOPE_KIND){
            jmp(compiler, continue_token, ".FOR_%"PRId32":CONDITION", peek_loop(compiler)->id);

            return;
        }

        assert(0 && "Illegal scope kind");
    }

    error(
        compiler,
        continue_token,
        "Continue statements only allowed in while and for loops"
    );
}

void while_stmt(Compiler *compiler, WhileStmt *while_stmt){
    ScopeManager *manager = compiler->manager;

    Token *while_token = while_stmt->while_token;
    Expr *condition_expr = while_stmt->condition_expr;
    DynArr *stmts = while_stmt->stmts;

    size_t stmts_len = stmts ? dynarr_len(stmts) : 0;
    int32_t while_id = generate_id(compiler);

    label(compiler, while_token, ".WHILE_%"PRId32":CONDITION", while_id);

    compile_expr(compiler, condition_expr);

    jif(compiler, while_token, ".WHILE_%"PRId32":END", while_id);

    scope_manager_push(manager, WHILE_SCOPE_KIND);
    push_loop(compiler, while_id);

    Block *block = push_block(compiler);

    block->stmts_len = stmts_len;

    for (size_t i = 0; i < stmts_len; i++){
        Stmt *stmt = dynarr_get_ptr(stmts, i);

        block->current_stmt = i + 1;

        compile_stmt(compiler, stmt);
    }

    pop_locals(compiler);

    jmp(compiler, while_token, ".WHILE_%"PRId32":CONDITION", while_id);
    label(compiler, while_token, ".WHILE_%"PRId32":END", while_id);

    pop_block(compiler);
    pop_loop(compiler);
    scope_manager_pop(manager);
}

void for_stmt(Compiler *compiler, ForStmt *for_stmt){
    ScopeManager *manager = compiler->manager;

    Token *for_token = for_stmt->for_token;
    DynArr *initializations = for_stmt->initializations;
    Expr *condition_expr = for_stmt->condition_expr;
    DynArr *update_exprs = for_stmt->update_exprs;
    DynArr *stmts = for_stmt->stmts;

    int32_t for_id = generate_id(compiler);
    size_t initializations_len = initializations ? dynarr_len(initializations) : 0;
    size_t update_exprs_len = update_exprs ? dynarr_len(update_exprs) : 0;
    size_t stmts_len = stmts ? dynarr_len(stmts) : 0;

    scope_manager_push(manager, BLOCK_SCOPE_KIND);

    for (size_t i = 0; i < initializations_len; i++){
        VarDeclStmt *var_decl_stmt = DYNARR_GET_PTR_AS(initializations, VarDeclStmt, i);

        compile_expr(compiler, var_decl_stmt->initial_value_expr);
        scope_manager_define_local(manager, 1, 1, var_decl_stmt->identifier_token);
    }

    label(compiler, for_token, ".FOR_%"PRId32":CONDITION", for_id);

    if(condition_expr){
        compile_expr(compiler, condition_expr);
        jif(compiler, for_token, ".FOR_%"PRId32":EXIT", for_id);
    }

    scope_manager_push(manager, FOR_SCOPE_KIND);
    push_loop(compiler, for_id);

    Block *block = push_block(compiler);

    for (size_t i = 0; i < stmts_len; i++){
        Stmt *stmt = DYNARR_GET_PTR_AS(stmts, Stmt, i);

        block->current_stmt = i + 1;

        compile_stmt(compiler, stmt);
    }

    pop_locals(compiler);

    for (size_t i = 0; i < update_exprs_len; i++){
        Expr *update_expr = DYNARR_GET_PTR_AS(update_exprs, Expr, i);

        compile_expr(compiler, update_expr);
        write_chunk(compiler, OP_POP);
        write_location(compiler, for_token);
    }

    jmp(compiler, for_token, ".FOR_%"PRId32":CONDITION", for_id);
    label(compiler, for_token, ".FOR_%"PRId32":END", for_id);

    pop_locals(compiler);
    pop_block(compiler);
    pop_loop(compiler);
    scope_manager_pop(manager);

    label(compiler, for_token, ".FOR_%"PRId32":EXIT", for_id);

    pop_locals(compiler);
    scope_manager_pop(manager);
}

void throw_stmt(Compiler *compiler, ThrowStmt *throw_stmt){
    ScopeManager *manager = compiler->manager;

    Token *throw_token = throw_stmt->throw_token;
    Expr *throw_value_expr = throw_stmt->value_expr;

    if(IS_GLOBAL_SCOPE(scope_manager_peek(manager))){
        error(
            compiler,
            throw_token,
            "Cannot use throw statements in global scope"
        );
    }

    if(throw_value_expr){
        compile_expr(compiler, throw_value_expr);
    }else{
        write_chunk(compiler, OP_EMPTY);
        write_location(compiler, throw_token);
    }

    write_chunk(compiler, OP_THROW);
    write_location(compiler, throw_token);
}

void try_stmt(Compiler *compiler, TryStmt *try_stmt){
    ScopeManager *manager = compiler->manager;

    Token *try_token = try_stmt->try_token;
    DynArr *try_stmts = try_stmt->try_stmts;
    Token *catch_token = try_stmt->catch_token;
    Token *err_identifier = try_stmt->err_identifier;
    DynArr *catch_stmts = try_stmt->catch_stmts;

    size_t try_stmts_len = dynarr_len(try_stmts);
    size_t catch_stmts_len = dynarr_len(catch_stmts);

    if(scope_manager_peek(manager)->type == CATCH_SCOPE_KIND){
        error(
            compiler,
            try_token,
            "Cannot use try statements inside catch blocks"
        );
    }

    uint32_t try_id = generate_id(compiler);

    //---------- TRY BLOCK ----------//
    Scope *try_scope = scope_manager_push(manager, TRY_SCOPE_KIND);
    Block *try_block = push_block(compiler);

    write_chunk(compiler, OP_TRY_IN);
    write_location(compiler, try_token);
    mark(compiler, try_token, "CATCH(%"PRId32")", try_id);

    try_block->stmts_len = try_stmts_len;

    for (size_t i = 0; i < try_stmts_len; i++){
        if(AS_LOCAL_SCOPE(try_scope)->returned){
            error(
                compiler,
                try_token,
                "Cannot exists statements after the scope returned"
            );
        }

        try_block->current_stmt = i + 1;

        compile_stmt(compiler, dynarr_get_ptr(try_stmts, i));
    }

    pop_locals(compiler);
    write_chunk(compiler, OP_TRY_OUT);
    write_location(compiler, try_token);

    jmp(compiler, try_token, "CATCH(%"PRId32")_END", try_id);

    pop_block(compiler);
    scope_manager_pop(manager);

    //---------- CATCH BLOCK ----------//
    Scope *catch_scope = scope_manager_push(manager, CATCH_SCOPE_KIND);

    if(err_identifier){
        scope_manager_define_local(manager, 0, 1, err_identifier);
    }

    Block *catch_block = push_block(compiler);

    label(compiler, catch_token, "CATCH(%"PRId32")", try_id);

    catch_block->stmts_len = catch_stmts_len;

    for (size_t i = 0; i < catch_stmts_len; i++){
        if(AS_LOCAL_SCOPE(catch_scope)->returned){
            error(
                compiler,
                catch_token,
                "Cannot exists statements after the scope returned"
            );
        }

        catch_block->current_stmt = i + 1;

        compile_stmt(compiler, dynarr_get_ptr(catch_stmts, i));
    }

    pop_locals(compiler);

    label(compiler, catch_token, "CATCH(%"PRId32")_END", try_id);

    pop_block(compiler);
    scope_manager_pop(manager);
}

void return_stmt(Compiler *compiler, ReturnStmt *ret_stmt){
    ScopeManager *manager = compiler->manager;

    Token *ret_token = ret_stmt->return_token;
    Expr *ret_expr = ret_stmt->ret_expr;

    if(scope_manager_is_global_scope(manager)){
        error(
            compiler,
            ret_token,
            "Return statements not allowed in global scope"
        );
    }

    Block *block = peek_block(compiler);
    Scope *scope = scope_manager_peek(compiler->manager);

    if(block->current_stmt < block->stmts_len){
        error(
            compiler,
            ret_token,
            "Return statements must be the last in the scope"
        );
    }

    assert(IS_LOCAL_SCOPE(scope) && "Scope must be local");
    AS_LOCAL_SCOPE(scope)->returned = 1;

    if(ret_expr){
        if(ret_expr->type == IDENTIFIER_EXPR_TYPE){
            IdentifierExpr *identifier_expr = ret_expr->sub_expr;
            Token *identifier_token = identifier_expr->identifier_token;
            Symbol *symbol = scope_manager_get_symbol(compiler->manager, identifier_token);

            if(symbol->kind == MODULE_SYMBOL_KIND){
                error(
                    compiler,
                    identifier_token,
                    "Cannot return modules"
                );
            }
        }

        compile_expr(compiler, ret_expr);
    }

    write_chunk(compiler, OP_RET);
    write_location(compiler, ret_token);
}

void proc_stmt(Compiler *compiler, ProcStmt *proc_stmt){
    ScopeManager *manager = compiler->manager;

    Token *identifier = proc_stmt->identifier;
    DynArr *params = proc_stmt->params;
    DynArr *stmts = proc_stmt->stmts;

    if(!scope_manager_is_global_scope(manager)){
        error(
            compiler,
            identifier,
            "Procedures declarations only allowed in global scope"
        );
    }

    size_t params_len = params ? dynarr_len(params) : 0;
    size_t stmts_len = stmts ? dynarr_len(stmts) : 0;

    Module *module = current_module(compiler);
    Fn *fn = vm_factory_module_fn_create(
        compiler->rtallocator,
        module,
        identifier->lexeme,
        params_len,
        NULL
    );
    FnObj *fn_obj = vm_factory_fn_obj_create(compiler->rtallocator, fn);
    FnSymbol *fn_symbol = (FnSymbol *)scope_manager_get_symbol(manager, identifier);

    fn_symbol->params_count = (uint8_t)params_len;

    vm_factory_module_globals_add_obj(
        module,
        (Obj *)fn_obj,
        identifier->lexeme,
        PRIVATE_GLOBAL_VALUE_TYPE
    );

    scope_manager_push_scopes(manager);
    scope_manager_push(manager, FN_SCOPE_KIND);
    push_unit(compiler, fn);
    Block *block = push_block(compiler);

    for (size_t i = 0; i < params_len; i++){
        ProcParam *param = DYNARR_GET_PTR_AS(params, ProcParam, i);

        scope_manager_define_local(
            manager,
            param->is_mutable,
            1,
            param->identifier
        );
    }

    block->stmts_len = stmts_len;

    for (size_t i = 0; i < stmts_len; i++){
        Stmt *stmt = dynarr_get_ptr(stmts, i);
        block->current_stmt = i + 1;

        compile_stmt(compiler, stmt);
    }

    Stmt *last_stmt = stmts_len > 0 ? DYNARR_GET_PTR_AS(stmts, Stmt, stmts_len - 1) : NULL;

    if(last_stmt && last_stmt->type != RETURN_STMT_TYPE){
        write_chunk(compiler, OP_EMPTY);
        write_location(compiler, identifier);
        write_chunk(compiler, OP_RET);
        write_location(compiler, identifier);
    }

    pop_block(compiler);
    pop_unit(compiler);
    scope_manager_pop(manager);
    scope_manager_pop_scopes(manager, NULL);
}

void import_stmt(Compiler *compiler, ImportStmt *import_stmt){
    ScopeManager *manager = compiler->manager;

    Token *import_token = import_stmt->import_token;
    DynArr *names = import_stmt->names;
    Token *alt_name_token = import_stmt->alt_name;

    size_t names_len = dynarr_len(names);
    Token *name_token = dynarr_get_ptr(names, names_len - 1);
    Token *definitive_name_token = alt_name_token ? alt_name_token : name_token;

    if(!scope_manager_is_global_scope(manager)){
        error(
            compiler,
            import_token,
            "Import statements only allowed in global scope"
        );
    }

    if(names_len == 1 && import_native(compiler, name_token, definitive_name_token)){
        scope_manager_define_module(manager, definitive_name_token);

        return;
    }

    DStr *main_search_pathname = compiler->main_search_pathname;
    DynArr *search_pathnames = compiler->search_pathnames;

    LZArena *compiler_arena = compiler->compiler_arena;
    Allocator *arena_allocator = compiler->arena_allocator;
    void *compiler_arena_state = lzarena_save(compiler_arena);

    Allocator *ctallocator = memory_lzflist_allocator(arena_allocator, NULL);
    const Allocator *rtallocator = compiler->rtallocator;
    const Allocator *pass_allocator = compiler->pass_allocator;

    DStr *new_main_search_pathname = NULL;
    char *source_pathname = NULL;
    Module *imported_module = NULL;

    switch (resolve_import_names(
        arena_allocator,
        main_search_pathname,
        search_pathnames,
        import_token,
        names,
        &new_main_search_pathname,
        &source_pathname
    )){
        case OK_STATUS_CODE:{
            break;
        }case FILE_IS_DIRECTORY_ERR_STATUS_CODE:{
            error(
                compiler,
                import_token,
                "File '%s' is a directory",
                name_token->lexeme
            );

            break;
        }case MODULE_NOT_FOUND_ERR_STATUS_CODE:{
            error(
                compiler,
                import_token,
                "Module '%s' not found",
                name_token->lexeme
            );

            break;
        }default:{
            assert(0 && "Illegal status code");

            break;
        }
    }

    size_t source_pathname_len = strlen(source_pathname);
    ModuleContext *out_imported_module_context = NULL;

    if(lzohtable_lookup(
        source_pathname_len,
        source_pathname,
        compiler->modules,
        (void **)(&out_imported_module_context))
    ){
        imported_module = vm_factory_module_sole_create(
            rtallocator,
            out_imported_module_context,
            name_token->lexeme,
            source_pathname
        );

        goto ACKNOWLEDGE;
    }

    bool_t save_search_pathname = true;
    size_t search_pathnames_len = dynarr_len(search_pathnames);

    for (size_t i = 0; i < search_pathnames_len; i++){
        DStr *search_pathname = DYNARR_GET_PTR_AS(search_pathnames, DStr, i);

        if(strcmp(search_pathname->buff, new_main_search_pathname->buff) == 0){
            save_search_pathname = false;

            break;
        }
    }

    if(save_search_pathname){
        size_t new_main_search_pathname_buff_len = new_main_search_pathname->len;
        char *cloned_new_main_search_pathname_buff = memory_clone_cstr(pass_allocator, new_main_search_pathname->buff, NULL);
        DStr *cloned_new_main_search_pathname = MEMORY_ALLOC(pass_allocator, DStr, 1);

        *cloned_new_main_search_pathname = (DStr){
            .len = new_main_search_pathname_buff_len,
            .buff = cloned_new_main_search_pathname_buff
        };

        dynarr_insert_ptr(search_pathnames, cloned_new_main_search_pathname);

        new_main_search_pathname = cloned_new_main_search_pathname;
    }

    switch (import_module(
        compiler->keywords,
        new_main_search_pathname,
        search_pathnames,
        compiler->default_natives,
        compiler->modules,
        compiler_arena,
        ctallocator,
        rtallocator,
        pass_allocator,
        arena_allocator,
        source_pathname,
        name_token->lexeme,
        &imported_module
    )){
        case OK_STATUS_CODE:{
            break;
        }case IMPORT_FAILED_ERR_STATUS_CODE:{
            error(
                compiler,
                import_token,
                "Failed to import module '%s'",
                name_token->lexeme
            );

            break;
        }default:{
            assert(0 && "Illegal status code");

            break;
        }
    }

    lzohtable_put(
        source_pathname_len,
        source_pathname,
        imported_module->context,
        compiler->modules,
        NULL
    );

    ModuleObj *imported_module_obj = NULL;
    Module *actual_module = NULL;

ACKNOWLEDGE:
    imported_module_obj = vm_factory_module_obj_create(rtallocator, imported_module);
    actual_module = current_module(compiler);

    scope_manager_define_module(manager, definitive_name_token);
    vm_factory_module_globals_add_obj(
        actual_module,
        (Obj *)imported_module_obj,
        definitive_name_token->lexeme,
        PRIVATE_GLOBAL_VALUE_TYPE
    );

    write_chunk(compiler, OP_IMPORT);
    write_location(compiler, import_token);
    write_str_alloc(compiler, definitive_name_token->lexeme_len, definitive_name_token->lexeme);

    lzarena_restore(compiler_arena, compiler_arena_state);
}

void export_stmt(Compiler *compiler, ExportStmt *export_stmt){
    Token *export_token = export_stmt->export_token;
    DynArr *symbols = export_stmt->symbols;
    size_t symbols_len = symbols ? dynarr_len(symbols) : 0;

    for (size_t i = 0; i < symbols_len; i++){
        Token *symbol_token = dynarr_get_ptr(symbols, i);

        write_chunk(compiler, OP_GLOBAL_ACCESS_SET);
        write_location(compiler, export_token);
        write_str_alloc(compiler, symbol_token->lexeme_len, symbol_token->lexeme);
        write_chunk(compiler, 1);
    }
}

void compile_stmt(Compiler *compiler, Stmt *stmt){
    switch (stmt->type){
        case EXPR_STMT_TYPE:{
            expr_stmt(compiler, (ExprStmt *)stmt->sub_stmt);

            break;
        }case VAR_DECL_STMT_TYPE:{
            var_decl_stmt(compiler, (VarDeclStmt *)stmt->sub_stmt);

            break;
        }case BLOCK_STMT_TYPE:{
            block_stmt(compiler, (BlockStmt *)stmt->sub_stmt);

            break;
        }case IF_STMT_TYPE:{
            if_stmt(compiler, (IfStmt *)stmt->sub_stmt);

            break;
        }case STOP_STMT_TYPE:{
            stop_stmt(compiler, (StopStmt *)stmt->sub_stmt);

            break;
        }case CONTINUE_STMT_TYPE:{
            continue_stmt(compiler, (ContinueStmt *)stmt->sub_stmt);

            break;
        }case WHILE_STMT_TYPE:{
            while_stmt(compiler, (WhileStmt *)stmt->sub_stmt);

            break;
        }case FOR_STMT_TYPE:{
            for_stmt(compiler, (ForStmt *)stmt->sub_stmt);

            break;
        }case THROW_STMT_TYPE:{
            throw_stmt(compiler, (ThrowStmt *)stmt->sub_stmt);

            break;
        }case TRY_STMT_TYPE:{
            try_stmt(compiler, (TryStmt *)stmt->sub_stmt);

            break;
        }case RETURN_STMT_TYPE:{
            return_stmt(compiler, (ReturnStmt *)stmt->sub_stmt);

            break;
        }case PROC_STMT_TYPE:{
            proc_stmt(compiler, (ProcStmt *)stmt->sub_stmt);

            break;
        }case IMPORT_STMT_TYPE:{
         	import_stmt(compiler, (ImportStmt *)stmt->sub_stmt);

            break;
        }case EXPORT_STMT_TYPE:{
            export_stmt(compiler, (ExportStmt *)stmt->sub_stmt);

            break;
        }default:{
            assert(0 && "Illegal token type");

            break;
        }
    }
}

void declare_defaults(Compiler *compiler){
    LZOHTable *default_natives = compiler->default_natives;
    size_t len = default_natives->m;

    for (size_t i = 0; i < len; i++){
        LZOHTableSlot slot = default_natives->slots[i];

        if(!slot.used){
            continue;
        }

        NativeFnObj *native_fn_obj = (NativeFnObj *)(((Value *)slot.value)->content.obj_val);
        NativeFn *native_fn = native_fn_obj->native_fn;

        scope_manager_define_native_fn(
            compiler->manager,
            native_fn->arity,
            native_fn->name
        );
    }
}

void declare_proc_prototypes(Compiler *compiler, DynArr *procs_prototypes){
    size_t len = dynarr_len(procs_prototypes);
    ScopeManager *manager = compiler->manager;

    for (size_t i = 0; i < len; i++){
        ProcPrototype prototype = DYNARR_GET_AS(procs_prototypes, ProcPrototype, i);
        scope_manager_define_fn(
            manager,
            prototype.arity,
            prototype.identifier_token
        );
    }
}

Compiler *compiler_create(const Allocator *ctallocator, const Allocator *rtallocator){
    Compiler *compiler = MEMORY_ALLOC(ctallocator, Compiler, 1);

    if(!compiler){
        return NULL;
    }

    *compiler = (Compiler){
        .ctallocator = ctallocator,
        .rtallocator = rtallocator
    };

    return compiler;
}

void compiler_destroy(Compiler *compiler){
    if(!compiler){
        return;
    }

    LZOHTABLE_DESTROY(compiler->modules);
    MEMORY_DEALLOC(compiler->ctallocator, Compiler, 1, compiler);
}

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
){
    if(setjmp(compiler->err_buf) == 0){
        const Allocator *ctallocator = compiler->ctallocator;
        const Allocator *rtallocator = compiler->rtallocator;

        LZArena *compiler_arena = NULL;
        Allocator *arena_allocator = memory_arena_allocator(ctallocator, &compiler_arena);
        Module *module = vm_factory_module_create(rtallocator, "main", pathname);
        Fn *entry_fn = vm_factory_module_fn_create(rtallocator, module, ".entry", 0, NULL);

        module->context->entry_fn = entry_fn;
        manager->err_buf = &compiler->err_buf;

        compiler->keywords = keywords;
        compiler->main_search_pathname = main_search_pathname;
        compiler->search_pathnames = search_pathnames;
        compiler->default_natives = default_natives;
        compiler->manager = manager;
        compiler->modules = modules;
        compiler->module = module;
        compiler->compiler_arena = compiler_arena;
        compiler->pass_allocator = compiler->ctallocator;
        compiler->arena_allocator = arena_allocator;

        declare_defaults(compiler);
        declare_proc_prototypes(compiler, proc_prototypes);
        push_unit(compiler, entry_fn);

        size_t stmts_len = dynarr_len(stmts);

        for (size_t i = 0; i < stmts_len; i++){
            Stmt *stmt = dynarr_get_ptr(stmts, i);

            compile_stmt(compiler, stmt);
        }

        write_chunk(compiler, OP_EXIT);
        pop_unit(compiler);

        return module;
    }else{
        return NULL;
    }

    return NULL;
}

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
){
    if(setjmp(compiler->err_buf) == 0){
        const Allocator *rtallocator = compiler->rtallocator;
        Module *module = vm_factory_module_create(rtallocator, name, pathname);
        Fn *entry_fn = vm_factory_module_fn_create(rtallocator, module, ".entry.import", 0, NULL);

        module->context->entry_fn = entry_fn;
        manager->err_buf = &compiler->err_buf;

        compiler->keywords = keywords;
        compiler->main_search_pathname = main_search_pathname;
        compiler->search_pathnames = search_pathnames;
        compiler->default_natives = default_natives;
        compiler->manager = manager;
        compiler->modules = modules;
        compiler->module = module;
        compiler->compiler_arena = compiler_arena;
        compiler->pass_allocator = pass_allocator;
        compiler->arena_allocator = arena_allocator;

        declare_defaults(compiler);
        declare_proc_prototypes(compiler, proc_prototypes);
        push_unit(compiler, entry_fn);

        size_t stmts_len = dynarr_len(stmts);

        for (size_t i = 0; i < stmts_len; i++){
            Stmt *stmt = dynarr_get_ptr(stmts, i);

            compile_stmt(compiler, stmt);
        }

        write_chunk(compiler, OP_EXIT);
        pop_unit(compiler);

        return module;
    }else{
        return NULL;
    }

    return NULL;
}
