#include "compiler.h"

#include "essentials/lzbstr.h"
#include "essentials/dynarr.h"
#include "essentials/lzohtable.h"
#include "essentials/lzpool.h"
#include "essentials/memory.h"

#include "scope_manager/scope.h"
#include "scope_manager/scope_manager.h"
#include "scope_manager/symbol.h"

#include "value.h"
#include "native_module/native_module_os.h"
#include "native_module/native_module_math.h"
#include "native_module/native_module_random.h"
#include "native_module/native_module_time.h"
#include "native_module/native_module_io.h"
#include "native_module/native_module_nbarray.h"
#include "native_module/native_module_raylib.h"

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

static void error(Compiler *compiler, const Token *token, const char *fmt, ...);
static void internal_error(Compiler *compiler, const char *fmt, ...);

static Unit *create_unit(Compiler *compiler, Fn *fn);
static Unit *push_unit(Compiler *compiler, Fn *fn);
static Fn *pop_unit(Compiler *compiler);

static void pop_scope_locals(Compiler *compiler, LocalScope *scope);
static void pop_locals(Compiler *compiler);

static Loop *current_loop(Compiler *compiler);
static void push_loop(Compiler *compiler, int32_t loop_id);
static void pop_loop(Compiler *compiler);

static Block *peek_block(Compiler *compiler);
static Block *push_block(Compiler *compiler);
static void pop_block(Compiler *compiler);

static Module *current_module(Compiler *compiler);
static Unit *current_unit(Compiler *compiler);
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

static void compile_expr(Compiler *compiler, Expr *expr);
static void propagate_return(Compiler *compiler, Scope *scope);
static int compile_if_branch(
    Compiler *compiler,
    IfStmtBranch *if_branch,
    ScopeType type,
    int32_t id,
    int32_t which
);
static int import_native(Compiler *compiler, const Token *name_token);
static DStr *add_new_search_path(
    Compiler *compiler,
    DynArr *search_pathnames,
    const char *source_pathname
);
static char *resolve_import_names(
    Compiler *compiler,
    DynArr *names,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    Token *import_token,
    DStr **out_main_search_pathname
);
static Module *import_module(
    Compiler *compiler,
    const Allocator *ctallocator,
    const Allocator *pssallocator,
    const Token *import_token,
    DStr *main_search_pathname,
    const char *pathname,
    const char *name,
    ScopeManager **out_manager
);
static Symbol *clone_symbol(const Symbol *symbol, const Allocator *allocator);
static void compile_stmt(Compiler *compiler, Stmt *stmt);
static void declare_defaults(Compiler *compiler);

void error(Compiler *compiler, const Token *token, const char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    fprintf(
    	stderr,
     	"COMPILER ERROR at line %d in file '%s':\n\t",
      	token->line,
       	token->pathname
    );
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);

    longjmp(compiler->buf, 1);
}

void internal_error(Compiler *compiler, const char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, "INTERNAL COMPILER ERROR:\n\t");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);

    longjmp(compiler->buf, 1);
}

Unit *create_unit(Compiler *compiler, Fn *fn){
    LZArena *arena = compiler->compiler_arena;
    Allocator *arena_allocator = compiler->arena_allocator;

    void *arena_state = lzarena_save(arena);
    LZPool *labels_pool = MEMORY_LZPOOL(arena_allocator, Label);
    LZPool *jmps_pool = MEMORY_LZPOOL(arena_allocator, Jmp);
    LZPool *marks_pool = MEMORY_LZPOOL(arena_allocator, Mark);
    LZPool *loops_pool = MEMORY_LZPOOL(arena_allocator, Loop);
    LZPool *blocks_pool = MEMORY_LZPOOL(arena_allocator, Block);
    Allocator *lzflist_allocator = memory_lzflist_allocator(arena_allocator, NULL);

	LZOHTable *labels = MEMORY_LZOHTABLE(lzflist_allocator);
	DynArr *jmps = MEMORY_DYNARR_PTR(lzflist_allocator);
    DynArr *marks = MEMORY_DYNARR_PTR(lzflist_allocator);
    LZOHTable *captured_symbols = MEMORY_LZOHTABLE(lzflist_allocator);
	Unit *unit = lzpool_alloc_x(16, compiler->units_pool);

	unit->counter = 0;

    unit->labels = labels;
	unit->jmps = jmps;
    unit->marks = marks;
    unit->loops = NULL;
    unit->blocks = NULL;
    unit->captured_symbols = captured_symbols;

    unit->fn = fn;

    unit->labels_pool = labels_pool;
    unit->jmps_pool = jmps_pool;
    unit->marks_pool = marks_pool;
    unit->loops_pool = loops_pool;
    unit->blocks_pool = blocks_pool;

    unit->arena_state = arena_state;
    unit->lzarena_allocator = arena_allocator;
    unit->lzflist_allocator = lzflist_allocator;

	unit->prev = NULL;

	return unit;
}

static Unit *push_unit(Compiler *compiler, Fn *fn){
	Unit *unit = create_unit(compiler, fn);

	unit->prev = compiler->units_stack;
	compiler->units_stack = unit;

	return unit;
}

Fn *pop_unit(Compiler *compiler){
	Unit *unit = compiler->units_stack;
    LZOHTable *labels = unit->labels;
    DynArr *jmps = unit->jmps;
    DynArr *marks = unit->marks;
	Fn *fn = unit->fn;

    size_t jmps_len = dynarr_len(jmps);

    for (size_t i = 0; i < jmps_len; i++){
        Label *label = NULL;
        Jmp *jmp = dynarr_get_ptr(jmps, i);

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

        size_t jmp_value = label->offset - jmp->jump_offset;

        update_i16(compiler, jmp->update_offset, (uint16_t)jmp_value);
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

        update_i16(compiler, mark->update_offset, (uint16_t)label->offset);
    }

	compiler->units_stack = unit->prev;

    lzpool_dealloc(unit);
    lzarena_restore(compiler->compiler_arena, unit->arena_state);

	return fn;
}

inline void pop_scope_locals(Compiler *compiler, LocalScope *scope){
    local_t locals_count = LOCAL_SCOPE_LOCALS_COUNT(scope);

    for (size_t i = 0; i < locals_count; i++){
        write_chunk(compiler, OP_POP);
    }
}

inline void pop_locals(Compiler *compiler){
    size_t len = scope_manager_locals_count(compiler->manager);

    for (size_t i = 0; i < len; i++){
        write_chunk(compiler, OP_POP);
    }
}

inline Loop *current_loop(Compiler *compiler){
    Unit *unit = current_unit(compiler);
    Loop *loop = unit->loops;

    assert(loop && "Loops stack is empty");

    return loop;
}

inline void push_loop(Compiler *compiler, int32_t loop_id){
    Unit *unit = current_unit(compiler);
    Loop *loop = lzpool_alloc_x(8, unit->loops_pool);

    loop->id = loop_id;
    loop->prev = unit->loops;
    unit->loops = loop;
}

inline void pop_loop(Compiler *compiler){
    Unit *unit = current_unit(compiler);
    Loop *loop = unit->loops;

    assert(loop && "Loops stack is empty");

    unit->loops = loop->prev;

    lzpool_dealloc(loop);
}

static Block *peek_block(Compiler *compiler){
	Unit *unit = current_unit(compiler);
	Block *block = unit->blocks;

	assert(block && "Blocks stack is empty");

	return block;
}

inline Block *push_block(Compiler *compiler){
	Unit *unit = current_unit(compiler);
	Block *block = lzpool_alloc_x(64, unit->blocks_pool);

	block->stmts_len = 0;
	block->current_stmt = 0;
	block->prev = unit->blocks;

	unit->blocks = block;

	return block;
}

inline void pop_block(Compiler *compiler){
	Unit *unit = current_unit(compiler);
	Block *block = unit->blocks;

	assert(block != NULL && "Blocks stack is empty");

	unit->blocks = block->prev;

	lzpool_dealloc(block);
}

inline Module *current_module(Compiler *compiler){
	return compiler->module;
}

inline Unit *current_unit(Compiler *compiler){
	return compiler->units_stack;
}

inline int32_t generate_id(Compiler *compiler){
	return current_unit(compiler)->counter++;
}

inline Fn *current_fn(Compiler *compiler){
	return current_unit(compiler)->fn;
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
	Module *module = current_module(compiler);
	DynArr *static_strs = MODULE_STRINGS(module);
    size_t static_strs_len = dynarr_len(static_strs);

    if(static_strs_len >= UINT16_MAX){}

    DStr str = (DStr){
        .len = raw_str_len,
        .buff = raw_str
    };

    dynarr_insert(static_strs, &str);
    write_i16(compiler, (int16_t)static_strs_len);
}

void write_str_alloc(Compiler *compiler, size_t raw_str_len, char *raw_str){
	Module *module = current_module(compiler);
	char *new_raw_str = MEMORY_ALLOC(compiler->rtallocator, char, raw_str_len + 1);
    DynArr *static_strs = MODULE_STRINGS(module);
    size_t static_strs_len = dynarr_len(static_strs);

    memcpy(new_raw_str, raw_str, raw_str_len);
    new_raw_str[raw_str_len] = 0;

    if(static_strs_len >= UINT16_MAX){}

    DStr str = (DStr){
        .len = raw_str_len,
        .buff = raw_str
    };

    dynarr_insert(static_strs, &str);
    write_i16(compiler, (int16_t)static_strs_len);
}

void write_location(Compiler *compiler, const Token *token){
	DynArr *chunks = current_chunks(compiler);
	DynArr *locations = current_locations(compiler);
	OPCodeLocation location = {0};

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
	Unit *unit = current_unit(compiler);
	LZOHTable *labels = unit->labels;
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(unit->lzarena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Label *label = lzpool_alloc_x(1024, unit->labels_pool);

	label->offset = chunks_len(compiler);
	label->name_len = name_len;
	label->name = cloned_name;

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
	size_t update_offset = write_i16(compiler, 0);
	Unit *unit = current_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(unit->lzarena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Mark *mark = lzpool_alloc_x(1024, unit->marks_pool);

	mark->update_offset = update_offset;
	mark->label_name_len = name_len;
	mark->label_name = cloned_name;

	dynarr_insert_ptr(unit->marks, mark);
}

void jmp(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
	write_chunk(compiler, OP_JMP);
	write_location(compiler, ref_token);
	size_t update_offset = write_i16(compiler, 0);
	size_t jmp_offset = chunks_len(compiler);

	Unit *unit = current_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(unit->lzarena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_x(1024, unit->jmps_pool);

	jmp->update_offset = update_offset;
	jmp->jump_offset = jmp_offset;
	jmp->label_name_len = name_len;
	jmp->label_name = cloned_name;

	dynarr_insert_ptr(unit->jmps, jmp);
}

void jif(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
	write_chunk(compiler, OP_JIF);
	write_location(compiler, ref_token);
	size_t update_offset = write_i16(compiler, 0);
	size_t jmp_offset = chunks_len(compiler);

	Unit *unit = current_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(unit->lzarena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_x(1024, unit->jmps_pool);

	jmp->update_offset = update_offset;
	jmp->jump_offset = jmp_offset;
	jmp->label_name_len = name_len;
	jmp->label_name = cloned_name;

	dynarr_insert_ptr(unit->jmps, jmp);
}

void jit(Compiler *compiler, const Token *ref_token, const char *fmt, ...){
	write_chunk(compiler, OP_JIT);
	write_location(compiler, ref_token);
	size_t update_offset = write_i16(compiler, 0);
	size_t jmp_offset = chunks_len(compiler);

	Unit *unit = current_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(unit->lzarena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_x(1024, unit->jmps_pool);

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

	Unit *unit = current_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(unit->lzarena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_x(1024, unit->jmps_pool);

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

	Unit *unit = current_unit(compiler);
	va_list args;

	va_start(args, fmt);

	size_t name_len = (size_t)(vsnprintf(NULL, 0, fmt, args) + 1);
	char *cloned_name = MEMORY_ALLOC(unit->lzarena_allocator, char, name_len);

	va_end(args);

	va_start(args, fmt);
	vsnprintf(cloned_name, name_len, fmt, args);
	va_end(args);

	Jmp *jmp = lzpool_alloc_x(1024, unit->jmps_pool);

	jmp->update_offset = update_offset;
	jmp->jump_offset = jmp_offset;
	jmp->label_name_len = name_len;
	jmp->label_name = cloned_name;

	dynarr_insert_ptr(unit->jmps, jmp);
}

void compile_expr(Compiler *compiler, Expr *expr){
    ScopeManager *manager = compiler->manager;

    switch (expr->type){
        case EMPTY_EXPRTYPE:{
			EmptyExpr *empty_expr = expr->sub_expr;

			write_chunk(compiler, OP_EMPTY);
			write_location(compiler, empty_expr->empty_token);

			break;
        }case BOOL_EXPRTYPE:{
            BoolExpr *bool_expr = expr->sub_expr;

            write_chunk(compiler, bool_expr->value ? OP_TRUE : OP_FALSE);
			write_location(compiler, bool_expr->bool_token);

            break;
        }case INT_EXPRTYPE:{
            IntExpr *int_expr = expr->sub_expr;
            Token *int_token = int_expr->token;
            int64_t value = *(int64_t *)int_token->literal;

            write_chunk(compiler, OP_INT);
			write_location(compiler, int_token);
            write_iconst(compiler, value);

            break;
        }case FLOAT_EXPRTYPE:{
			FloatExpr *float_expr = expr->sub_expr;
			Token *float_token = float_expr->token;
			double value = *(double *)float_token->literal;

			write_chunk(compiler, OP_FLOAT);
			write_location(compiler, float_token);
			write_fconst(compiler, value);

			break;
		}case STRING_EXPRTYPE:{
			StrExpr *str_expr = expr->sub_expr;
            Token *str_token = str_expr->str_token;

			write_chunk(compiler, OP_STRING);
			write_location(compiler, str_token);
			write_str(compiler, str_token->literal_size, str_token->literal);

			break;
		}case TEMPLATE_EXPRTYPE:{
            TemplateExpr *template_expr = expr->sub_expr;
            Token *template_token = template_expr->template_token;
            DynArr *exprs = template_expr->exprs;

            write_chunk(compiler, OP_STTE);
            write_location(compiler, template_token);

            if(exprs){
                size_t len = dynarr_len(exprs);

                for (size_t i = 0; i < len; i++){
                    Expr *expr = (Expr *)dynarr_get_ptr(exprs, i);

                    compile_expr(compiler, expr);

                    write_chunk(compiler, OP_WTTE);
                    write_location(compiler, template_token);
                }
            }

            write_chunk(compiler, OP_ETTE);
            write_location(compiler, template_token);

            break;
        }case ANON_EXPRTYPE:{
            AnonExpr *anon_expr = expr->sub_expr;
            DynArr *params = anon_expr->params;
            DynArr *stmts = anon_expr->stmts;

            size_t params_len = params ? dynarr_len(params) : 0;
            size_t stmts_len = stmts ? dynarr_len(stmts) : 0;

            Fn *fn = vm_factory_fn_create(
                compiler->rtallocator,
                "anonymous",
                params_len
            );
            size_t symbol_idx;

            vm_factory_module_add_fn(current_module(compiler), fn, &symbol_idx);
            scope_manager_push(manager, FN_SCOPE_TYPE);
            push_unit(compiler, fn);

            for (size_t i = 0; i < params_len; i++){
                Token *param_identifier_token = dynarr_get_ptr(params, i);

                scope_manager_define_local(
                    manager,
                    1,
                    1,
                    param_identifier_token
                );
            }

            uint8_t must_return = 1;

            for (size_t i = 0; i < stmts_len; i++){
                Stmt *stmt = dynarr_get_ptr(stmts, i);
                compile_stmt(compiler, stmt);

                if(i + 1 >= stmts_len && stmt->type == RETURN_STMT_TYPE){
                    must_return = 0;
                }
            }

            if(must_return){
                write_chunk(compiler, OP_EMPTY);
                write_chunk(compiler, OP_RET);
            }

            Unit *unit = current_unit(compiler);
            LZOHTable *outs = unit->captured_symbols;
            size_t outs_len = outs->n;
            size_t outs_m = outs->m;

            if(outs_len > 0){
                size_t outs_counter = 0;
                MetaClosure *closure = MEMORY_ALLOC(compiler->rtallocator, MetaClosure, 1);
                MetaOutValue *meta_outs = closure->meta_out_values;

                closure->meta_out_values_len = outs_len;
                closure->fn = fn;

                for (size_t i = 0; i < outs_m && outs_counter < outs_len; i++){
                    LZOHTableSlot *slot = &outs->slots[i];

                    if(!slot->used){
                        continue;
                    }

                    Symbol *symbol = slot->value;

                    assert(symbol->type == LOCAL_SYMBOL_TYPE);

                    meta_outs[outs_counter++].at = ((LocalSymbol *)symbol)->offset;
                }

                vm_factory_module_add_closure(
                    current_module(compiler),
                    closure,
                    &symbol_idx
                );
            }

            pop_unit(compiler);
            scope_manager_pop(manager);

            write_chunk(compiler, OP_SGET);
            write_location(compiler, anon_expr->anon_token);
            write_i32(compiler, (int32_t)symbol_idx);

            break;
        }case IDENTIFIER_EXPRTYPE:{
            IdentifierExpr *identifier_expr = expr->sub_expr;
            Token *identifier_token = identifier_expr->identifier_token;
            Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

            switch (symbol->type){
                case LOCAL_SYMBOL_TYPE:{
                    LocalSymbol *local_symbol = (LocalSymbol *)symbol;
                    Scope *current_scope = scope_manager_peek(manager);
                    const Scope *symbol_scope = symbol->scope;

                    if(IS_LOCAL_SCOPE(current_scope) && IS_LOCAL_SCOPE(symbol_scope)){
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

                            Unit *unit = current_unit(compiler);
                            LZOHTable *captured_symbols = unit->captured_symbols;

                            lzohtable_put(
                                identifier_token->lexeme_len,
                                identifier_token->lexeme,
                                symbol,
                                captured_symbols,
                                NULL
                            );

                            write_chunk(compiler, OP_OGET);
                            write_location(compiler, identifier_token);
                            write_chunk(compiler, local_symbol->offset);

                            break;
                        }
                    }

                    write_chunk(compiler, OP_LGET);
                    write_location(compiler, identifier_token);
                    write_chunk(compiler, local_symbol->offset);

                    break;
                }case GLOBAL_SYMBOL_TYPE:
                 case FN_SYMBOL_TYPE:
                 case MODULE_SYMBOL_TYPE:{
                    write_chunk(compiler, OP_GGET);
                    write_location(compiler, identifier_token);
                    write_str_alloc(
                        compiler,
                        identifier_token->lexeme_len,
                        identifier_token->lexeme
                    );

                    break;
                }case NATIVE_FN_SYMBOL_TYPE:{
                    write_chunk(compiler, OP_NGET);
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
        }case GROUP_EXPRTYPE:{
            GroupExpr *group_expr = expr->sub_expr;
            compile_expr(compiler, group_expr->expr);
            break;
        }case CALL_EXPRTYPE:{
            CallExpr *call_expr = expr->sub_expr;
            Expr *left_expr = call_expr->left_expr;
            DynArr *args = call_expr->args;
            size_t args_count = args ? dynarr_len(args) : 0;

            if(left_expr->type == IDENTIFIER_EXPRTYPE){
                IdentifierExpr *identifier_expr = left_expr->sub_expr;
                Token *identifier_token = identifier_expr->identifier_token;
                Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

                switch(symbol->type){
                    case NATIVE_FN_SYMBOL_TYPE:{
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
                    }case FN_SYMBOL_TYPE:{
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

            compile_expr(compiler, call_expr->left_expr);

            for (size_t i = 0; i < args_count; i++){
                compile_expr(compiler, dynarr_get_ptr(args, i));
            }

            write_chunk(compiler, OP_CALL);
            write_location(compiler, call_expr->left_paren);
            write_chunk(compiler, args_count);

            break;
        }case ACCESS_EXPRTYPE:{
            AccessExpr *access_expr = expr->sub_expr;
            Expr *left_expr = access_expr->left_expr;
            Token *dot_token =  access_expr->dot_token;
            Token *symbol_token = access_expr->symbol_token;

            compile_expr(compiler, left_expr);

            write_chunk(compiler, OP_ACCESS);
			write_location(compiler, dot_token);
            write_str_alloc(compiler, symbol_token->lexeme_len, symbol_token->lexeme);

            break;
        }case INDEX_EXPRTYPE:{
            IndexExpr *index_expr = expr->sub_expr;

            compile_expr(compiler, index_expr->index_expr);
            compile_expr(compiler, index_expr->target_expr);

            write_chunk(compiler, OP_INDEX);
            write_location(compiler, index_expr->left_square_token);

            break;
        }case UNARY_EXPRTYPE:{
            UnaryExpr *unary_expr = expr->sub_expr;
            Token *operator_token = unary_expr->operator_token;

            compile_expr(compiler, unary_expr->right);

            switch (operator_token->type){
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
        }case BINARY_EXPRTYPE:{
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
        }case MULSTR_EXPRTYPE:{
            MulStrExpr *mulstr_expr = expr->sub_expr;

            compile_expr(compiler, mulstr_expr->left);
            compile_expr(compiler, mulstr_expr->right);

            write_chunk(compiler, OP_MULSTR);
            write_location(compiler, mulstr_expr->operator_token);

            break;
        } case CONCAT_EXPRTYPE:{
            ConcatExpr *concat_expr = expr->sub_expr;

            compile_expr(compiler, concat_expr->left);
            compile_expr(compiler, concat_expr->right);

            write_chunk(compiler, OP_CONCAT);
            write_location(compiler, concat_expr->operator_token);

            break;
        }case BITWISE_EXPRTYPE:{
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
        }case COMPARISON_EXPRTYPE:{
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
        }case LOGICAL_EXPRTYPE:{
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
        }case ASSIGN_EXPRTYPE:{
            AssignExpr *assign_expr = expr->sub_expr;
            Expr *left_expr = assign_expr->left_expr;
            Token *equals_token = assign_expr->equals_token;
            Expr *value_expr = assign_expr->value_expr;

            if(value_expr->type == IDENTIFIER_EXPRTYPE){
                IdentifierExpr *identifier_expr = value_expr->sub_expr;
                Token *identifier_token = identifier_expr->identifier_token;
                Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

                if(symbol->type == MODULE_SYMBOL_TYPE){
                    error(
                        compiler,
                        equals_token,
                        "Cannot assign modules to variables"
                    );
                }
            }

            switch (left_expr->type){
                case IDENTIFIER_EXPRTYPE:{
                    IdentifierExpr *identifier_expr = left_expr->sub_expr;
                    Token *identifier_token = identifier_expr->identifier_token;
                    Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

                    switch (symbol->type){
                        case LOCAL_SYMBOL_TYPE:{
                            LocalSymbol *local_symbol = (LocalSymbol *)symbol;

                            if(!local_symbol->is_mutable && local_symbol->is_initialized){
                                error(
                                    compiler,
                                    assign_expr->equals_token,
                                    "Local symbol '%s' declared as immutable and already initialized",
                                    identifier_token->lexeme
                                );
                            }

                            compile_expr(compiler, value_expr);

                            write_chunk(compiler, OP_LSET);
                            write_location(compiler, equals_token);
                            write_chunk(compiler, local_symbol->offset);

                            if(local_symbol->is_initialized){
                                break;
                            }

                            break;
                        }case GLOBAL_SYMBOL_TYPE:{
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

                            write_chunk(compiler, OP_GSET);
                            write_location(compiler, equals_token);
                            write_str_alloc(compiler, identifier_token->lexeme_len, identifier_token->lexeme);

                            break;
                        }case FN_SYMBOL_TYPE:{
                            error(
                                compiler,
                                assign_expr->equals_token,
                                "Procedures name cannot be re-assigned",
                                identifier_token->lexeme
                            );

                            break;
                        }case MODULE_SYMBOL_TYPE:{
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
                }case INDEX_EXPRTYPE:{
               		IndexExpr *index_expr = left_expr->sub_expr;

                	compile_expr(compiler, value_expr);
                	compile_expr(compiler, index_expr->index_expr);
                 	compile_expr(compiler, index_expr->target_expr);

                 	write_chunk(compiler, OP_ASET);
                 	write_location(compiler, equals_token);

                  	break;
                }case ACCESS_EXPRTYPE:{
               		AccessExpr *access_expr = left_expr->sub_expr;
                	Token *symbol_token = access_expr->symbol_token;

                 	compile_expr(compiler, value_expr);
                  	compile_expr(compiler, access_expr->left_expr);

                 	write_chunk(compiler, OP_RSET);
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
        }case COMPOUND_EXPRTYPE:{
       		CompoundExpr *compound_expr = (CompoundExpr *)expr->sub_expr;
            Expr *left_expr = compound_expr->left_expr;
			Token *operator_token = compound_expr->operator_token;
			Expr *right_expr = compound_expr->right_expr;

            switch (left_expr->type){
                case IDENTIFIER_EXPRTYPE:{
                    IdentifierExpr *identifier_expr = (IdentifierExpr *)left_expr->sub_expr;
                    Token *identifier_token = identifier_expr->identifier_token;
                    Symbol *symbol = scope_manager_get_symbol(manager, identifier_token);

                    switch (symbol->type) {
                   		case LOCAL_SYMBOL_TYPE:{
                   			LocalSymbol *local_symbol = (LocalSymbol *)symbol;

                    		if(!local_symbol->is_mutable && local_symbol->is_initialized){
	                        	error(
	                            	compiler,
	                              	operator_token,
		                            "Local symbol '%s' declared as immutable and already initialized",
	                              	identifier_token->lexeme
	                          	);
	                      	}

                      		write_chunk(compiler, OP_LGET);
		                    write_location(compiler, identifier_token);
		                    write_chunk(compiler, local_symbol->offset);

                     		break;
                     	}case GLOBAL_SYMBOL_TYPE:{
                      		GlobalSymbol *global_symbol = (GlobalSymbol *)symbol;

                       		if(!global_symbol->is_mutable){
	                        	error(
	                            	compiler,
	                              	operator_token,
		                            "Local symbol '%s' declared as immutable",
	                              	identifier_token->lexeme
	                          	);
	                      	}

                        	write_chunk(compiler, OP_GGET);
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

                    switch (symbol->type) {
                   		case LOCAL_SYMBOL_TYPE:{
                   			LocalSymbol *local_symbol = (LocalSymbol *)symbol;

                    		write_chunk(compiler, OP_LSET);
                      		write_location(compiler, identifier_token);
                        	write_chunk(compiler, local_symbol->offset);

                     		break;
                     	}case GLOBAL_SYMBOL_TYPE:{
                        	write_chunk(compiler, OP_GSET);
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
                }case INDEX_EXPRTYPE:{
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

                   	write_chunk(compiler, OP_ASET);
                   	write_location(compiler, operator_token);

                 	break;
                }case ACCESS_EXPRTYPE:{
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

                    write_chunk(compiler, OP_RSET);
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
		}case ARRAY_EXPRTYPE:{
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

                    write_chunk(compiler, OP_IARRAY);
                    write_location(compiler, array_token);
                    write_i16(compiler, (int16_t)i);
                }
            }

            break;
        }case LIST_EXPRTYPE:{
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

                    write_chunk(compiler, OP_ILIST);
                    write_location(compiler, list_token);
				}
			}

			break;
		}case DICT_EXPRTYPE:{
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

                    write_chunk(compiler, OP_IDICT);
			        write_location(compiler, dict_expr->dict_token);
                }
            }

            break;
        }case RECORD_EXPRTYPE:{
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

                write_chunk(compiler, OP_IRECORD);
                write_location(compiler, record_token);
                write_str_alloc(compiler, key->lexeme_len, key->lexeme);
            }

			break;
		}case IS_EXPRTYPE:{
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
		}case TENARY_EXPRTYPE:{
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
    ScopeType type,
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

int import_native(Compiler *compiler, const Token *name_token){
	if(strcmp("os", name_token->lexeme) == 0){
		if(!os_native_module){
			os_module_init(compiler->rtallocator);

			vm_factory_module_globals_add_obj(
				current_module(compiler),
				(Obj *)vm_factory_native_module_obj_create(
					compiler->rtallocator,
					os_native_module
				),
				"os",
				PRIVATE_GLOVAL_VALUE_TYPE
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
				"math",
				PRIVATE_GLOVAL_VALUE_TYPE
			);
		}

		return 1;
	}

	if(strcmp("random", name_token->lexeme) == 0){
		if(!random_native_module){
			random_module_init(compiler->rtallocator);

			vm_factory_module_globals_add_obj(
				current_module(compiler),
				(Obj *)vm_factory_native_module_obj_create(
					compiler->rtallocator,
					random_native_module
				),
				"random",
				PRIVATE_GLOVAL_VALUE_TYPE
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
				"time",
				PRIVATE_GLOVAL_VALUE_TYPE
			);
		}

		return 1;
	}

	if(strcmp("io", name_token->lexeme) == 0){
		if(!io_native_module){
			io_module_init(compiler->rtallocator);

			vm_factory_module_globals_add_obj(
				current_module(compiler),
				(Obj *)vm_factory_native_module_obj_create(
					compiler->rtallocator,
					io_native_module
				),
				"io",
				PRIVATE_GLOVAL_VALUE_TYPE
			);
		}

		return 1;
	}

    if(strcmp("nbarray", name_token->lexeme) == 0){
		if(!nbarray_native_module){
			nbarray_module_init(compiler->rtallocator);

			vm_factory_module_globals_add_obj(
				current_module(compiler),
				(Obj *)vm_factory_native_module_obj_create(
					compiler->rtallocator,
					nbarray_native_module
				),
				"nbarray",
				PRIVATE_GLOVAL_VALUE_TYPE
			);
		}

		return 1;
	}

#ifdef RAYLIB
    if(strcmp("raylib", name_token->lexeme) == 0){
		if(!raylib_native_module){
			raylib_module_init(compiler->rtallocator);

			vm_factory_module_globals_add_obj(
				current_module(compiler),
				(Obj *)vm_factory_native_module_obj_create(
					compiler->rtallocator,
					raylib_native_module
				),
				"raylib",
				PRIVATE_GLOVAL_VALUE_TYPE
			);
		}

		return 1;
	}
#endif

	return 0;
}

DStr *add_new_search_path(Compiler *compiler, DynArr *search_pathnames, const char *source_pathname){
    const Allocator *pssallocator = compiler->pssallocator;
    size_t search_pathnames_len = dynarr_len(search_pathnames);
    char *parent_pathname = utils_files_parent_pathname(pssallocator, source_pathname);

    for (size_t i = 0; i < search_pathnames_len; i++){
        DStr *search_pathname = dynarr_get_raw(search_pathnames, i);

        if(strcmp(parent_pathname, search_pathname->buff) == 0){
            memory_destroy_cstr(pssallocator, parent_pathname);
            return search_pathname;
        }
    }

    dynarr_insert(
        search_pathnames,
        &((DStr){
            .len = strlen(parent_pathname),
            .buff = parent_pathname
        })
    );

    return dynarr_get_raw(search_pathnames, dynarr_len(search_pathnames) - 1);
}

char *resolve_import_names(
    Compiler *compiler,
    DynArr *names,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    Token *import_token,
    DStr **out_main_search_pathname
){
    const Allocator *arena_allocator = compiler->arena_allocator;
    void *arena_state = lzarena_save(arena_allocator->ctx);
    LZBStr *lzbstr = MEMORY_LZBSTR(arena_allocator);
    size_t names_len = dynarr_len(names);

    lzbstr_grow_by(1024, lzbstr);

    for (size_t i = 0; i < names_len; i++){
        Token *name_token = dynarr_get_ptr(names, i);
        lzbstr_append(name_token->lexeme, lzbstr);

        if(i + 1 < names_len){
            lzbstr_append("/", lzbstr);
        }
    }

    lzbstr_append(".ze", lzbstr);

    char *target_pathname = lzbstr_rclone_buff((LZBStrAllocator *)arena_allocator, lzbstr, NULL);
    char *module_name = ((Token *)dynarr_get_ptr(names, names_len - 1))->lexeme;

    lzbstr_reset(lzbstr);

    size_t search_pathnames_len = dynarr_len(search_pathnames);
    DStr *search_pathname = main_search_pathname;
    char *source_pathname = NULL;
    size_t i = 0;

    do{
        lzbstr_append(search_pathname->buff, lzbstr);

        if(search_pathname->buff[search_pathname->len - 1] == '/'){
            lzbstr_append(target_pathname, lzbstr);
        }else{
            lzbstr_append_args(lzbstr, "/%s", target_pathname);
        }

        if(utils_files_exists(lzbstr->buff)){
            lzarena_restore(arena_allocator->ctx, arena_state);

            source_pathname = lzbstr_rclone_buff((LZBStrAllocator *)arena_allocator, lzbstr, NULL);
            search_pathname = add_new_search_path(compiler, search_pathnames, source_pathname);

            if(utils_files_is_directory(source_pathname)){
                error(
                    compiler,
                    import_token,
                    "file with name '%s' found at '%s' but is a directory",
                    module_name,
                    source_pathname
                );
            }

            break;
        }

        search_pathname = dynarr_get_raw(search_pathnames, i);

        lzbstr_reset(lzbstr);
    }while(i++ < search_pathnames_len);

    if(!source_pathname){
        error(
            compiler,
            import_token,
            "Module '%s' not found",
            module_name
        );
    }

    if(out_main_search_pathname){
        *out_main_search_pathname = search_pathname;
    }

    return source_pathname;
}

Module *import_module(
    Compiler *compiler,
    const Allocator *ctallocator,
    const Allocator *pssallocator,
    const Token *import_token,
    DStr *main_search_pathname,
    const char *pathname,
    const char *name,
    ScopeManager **out_manager
){
    LZArena *compiler_arena = compiler->compiler_arena;
    Allocator *compiler_arena_allocator = compiler->arena_allocator;
    const Allocator *rtallocator = compiler->rtallocator;
    DynArr *search_pathnames = compiler->search_pathnames;
    LZOHTable *default_natives = compiler->default_natives;
    LZOHTable *keywords = compiler->keywords;

    DStr *source = utils_read_source(pathname, compiler_arena_allocator);
    DynArr *tokens = MEMORY_DYNARR_PTR(ctallocator);
    DynArr *stmts = MEMORY_DYNARR_PTR(ctallocator);
    DynArr *fns_prototypes = MEMORY_DYNARR_PTR(ctallocator);
    ScopeManager *manager = scope_manager_create(ctallocator);
    Lexer *lexer = lexer_create(ctallocator, rtallocator);
    Parser *parser = parser_create(ctallocator);
    Compiler *import_compiler = compiler_create(ctallocator, rtallocator);

    if(lexer_scan(source, tokens, keywords, pathname, lexer)){
        error(
            compiler,
            import_token,
            "Failed to import module '%s'",
            pathname
        );
    }

    if(parser_parse(tokens, fns_prototypes, stmts, parser)){
        error(
            compiler,
            import_token,
            "Failed to import module '%s'",
            pathname
        );
    }

    Module *imported_module = compiler_import(
        import_compiler,
        compiler_arena,
        compiler_arena_allocator,
        pssallocator,
        keywords,
        main_search_pathname,
        search_pathnames,
        default_natives,
        manager,
        stmts,
        pathname,
        name
    );

    if(!imported_module){
        error(
            compiler,
            import_token,
            "Failed to import module '%s'",
            pathname
        );
    }

    if(out_manager){
        *out_manager = manager;
    }

    return imported_module;
}

static Token *clone_token(const Token *token, const Allocator *allocator){
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
    switch (symbol->type){
        case GLOBAL_SYMBOL_TYPE:{
            const GlobalSymbol *global_symbol = (GlobalSymbol *)symbol;
            GlobalSymbol *cloned_global_symbol = MEMORY_ALLOC(allocator, GlobalSymbol, 1);

            *cloned_global_symbol = *global_symbol;
            cloned_global_symbol->symbol.identifier = clone_token(global_symbol->symbol.identifier, allocator);
            cloned_global_symbol->symbol.scope = NULL;

            return (Symbol *)cloned_global_symbol;
        }case FN_SYMBOL_TYPE:{
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

void compile_stmt(Compiler *compiler, Stmt *stmt){
    ScopeManager *manager = compiler->manager;

    switch (stmt->type){
        case EXPR_STMT_TYPE:{
            ExprStmt *expr_stmt = stmt->sub_stmt;
            Expr *sub_expr = expr_stmt->expr;

            compile_expr(compiler, sub_expr);
            write_chunk(compiler, OP_POP);

            break;
        }case VAR_DECL_STMT_TYPE:{
            VarDeclStmt *var_decl_stmt = stmt->sub_stmt;
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

                write_chunk(compiler, OP_GDEF);
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

            break;
        }case BLOCK_STMT_TYPE:{
            BlockStmt *block_stmt = stmt->sub_stmt;
            DynArr *stmts = block_stmt->stmts;
            size_t stmts_len = stmts ? dynarr_len(stmts) : 0;

            Scope *scope = scope_manager_push(manager, BLOCK_SCOPE_TYPE);
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

            break;
        }case IF_STMT_TYPE:{
            IfStmt *if_stmt = stmt->sub_stmt;
            IfStmtBranch *if_branch = if_stmt->if_branch;
            DynArr *elif_branches = if_stmt->elif_branches;
            DynArr *else_stmts = if_stmt->else_stmts;
            size_t elif_branches_len = elif_branches ? dynarr_len(elif_branches) : 0;
            int32_t if_id = generate_id(compiler);

            size_t branches_len = 1 + (elif_branches ? dynarr_len(elif_branches) : 0) + (else_stmts ? 1 : 0);
            uint16_t returns = compile_if_branch(compiler, if_branch, IF_SCOPE_TYPE, if_id, 0) ? 1 : 0;

            for (size_t i = 0; i < elif_branches_len; i++){
                IfStmtBranch *elif_branch = dynarr_get_ptr(elif_branches, i);

                if(compile_if_branch(
                	compiler,
                 	elif_branch,
                  	ELIF_SCOPE_TYPE,
                   	if_id,
                    i + 1
                )){
               		returns++;
                }
            }

            if(else_stmts){
                size_t else_stmts_len = dynarr_len(else_stmts);

                Scope *scope = scope_manager_push(manager, ELSE_SCOPE_TYPE);
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

            break;
        }case STOP_STMT_TYPE:{
            StopStmt *stop_stmt = stmt->sub_stmt;
            Token *stop_token = stop_stmt->stop_token;

            if(scope_manager_is_scope_type(manager, WHILE_SCOPE_TYPE)){
                jmp(compiler, stop_token, ".WHILE(%"PRId32")_END", current_loop(compiler)->id);
                break;
            }

            if(scope_manager_is_scope_type(manager, FOR_SCOPE_TYPE)){
                jmp(compiler, stop_token, ".FOR(%"PRId32")_END", current_loop(compiler)->id);
                break;
            }

            error(
                compiler,
                stop_token,
                "Stop statements only allowed in while and for loops"
            );

            break;
        }case CONTINUE_STMT_TYPE:{
            ContinueStmt *continue_stmt = stmt->sub_stmt;
            Token *continue_token = continue_stmt->continue_token;

            if(scope_manager_is_scope_type(manager, WHILE_SCOPE_TYPE)){
                jmp(
                	compiler,
                 	continue_token,
                  	".WHILE(%"PRId32")_TEST",
                   	current_loop(compiler)->id
                );
                break;
            }

            if(scope_manager_is_scope_type(manager, FOR_SCOPE_TYPE)){
                jmp(
                	compiler,
                 	continue_token,
                  	".FOR(%"PRId32")_TEST",
                   	current_loop(compiler)->id
                );
                break;
            }

            error(
                compiler,
                continue_token,
                "Continue statements only allowed in while and for loops"
            );

            break;
        }case WHILE_STMT_TYPE:{
            WhileStmt *while_stmt = stmt->sub_stmt;
            Token *while_token = while_stmt->while_token;
            Expr *condition_expr = while_stmt->condition_expr;
            DynArr *stmts = while_stmt->stmts;
            size_t stmts_len = dynarr_len(stmts);
            int32_t while_id = generate_id(compiler);

            label(compiler, while_token, ".WHILE(%"PRId32")_TEST", while_id);

            compile_expr(compiler, condition_expr);

            jif(compiler, while_token, ".WHILE(%"PRId32")_END", while_id);

            Scope *scope = scope_manager_push(manager, WHILE_SCOPE_TYPE);
            push_loop(compiler, while_id);
            Block *block = push_block(compiler);

            block->stmts_len = stmts_len;

            for (size_t i = 0; i < stmts_len; i++){
	           	if(AS_LOCAL_SCOPE(scope)->returned){
	         		error(
	            		compiler,
	               		while_token,
	                 	"Cannot exists statements after the scope returned"
	           		);
	           	}

                Stmt *stmt = dynarr_get_ptr(stmts, i);
                block->current_stmt = i + 1;

                compile_stmt(compiler, stmt);
            }

            pop_locals(compiler);

            jmp(compiler, while_token, ".WHILE(%"PRId32")_TEST", while_id);
            label(compiler, while_token, ".WHILE(%"PRId32")_END", while_id);

            pop_block(compiler);
            pop_loop(compiler);
            scope_manager_pop(manager);

            break;
        }case FOR_RANGE_STMT_TYPE:{
            ForRangeStmt *for_range_stmt = stmt->sub_stmt;
            Token *for_token = for_range_stmt->for_token;
            Token *symbol_token = for_range_stmt->symbol_token;
            Expr *left_expr = for_range_stmt->left_expr;
            Token *for_type_token = for_range_stmt->for_type_token;
            Expr *right_expr = for_range_stmt->right_expr;
            DynArr *stmts = for_range_stmt->stmts;
            size_t stmts_len = dynarr_len(stmts);
            int32_t for_id = generate_id(compiler);

            // BLOCK_SCOPE
            scope_manager_push(manager, BLOCK_SCOPE_TYPE);
            LocalSymbol *local_symbol = scope_manager_define_local(manager, 0, 1, symbol_token);

            // FOR RANGE SCOPE
            Scope *scope = scope_manager_push(manager, FOR_SCOPE_TYPE);
            push_loop(compiler, for_id);
            Block *block = push_block(compiler);

            block->stmts_len = stmts_len;

            // INITIALIZATION SECTION
            compile_expr(compiler, left_expr);

            // TEST SECTION
            label(compiler, for_token, ".FOR(%"PRId32")_TEST", for_id);

            write_chunk(compiler, OP_LGET);
            write_location(compiler, for_token);
            write_chunk(compiler, local_symbol->offset);

            compile_expr(compiler, right_expr);

            if(for_type_token->type == UPTO_TOKTYPE){
                write_chunk(compiler, OP_GE);
                write_location(compiler, for_token);
            }else{
                write_chunk(compiler, OP_LT);
                write_location(compiler, for_token);
            }

            jit(compiler, for_token, ".FOR_RANGE(%"PRId32")_END", for_id);

            for (size_t i = 0; i < stmts_len; i++){
           		if(AS_LOCAL_SCOPE(scope)->returned){
	         		error(
	            		compiler,
	               		for_token,
	                 	"Cannot exists statements after the scope returned"
	           		);
	           	}

                Stmt *stmt = dynarr_get_ptr(stmts, i);
                block->current_stmt = i + 1;

                compile_stmt(compiler, stmt);
            }

            pop_locals(compiler);

            // INCREMENT SECTION
            write_chunk(compiler, OP_LGET);
            write_location(compiler, for_token);
            write_chunk(compiler, local_symbol->offset);

            write_chunk(compiler, OP_CINT);
            write_location(compiler, for_token);
            write_chunk(compiler, 1);

            if(for_type_token->type == UPTO_TOKTYPE){
                write_chunk(compiler, OP_ADD);
                write_location(compiler, for_token);
            }else{
                write_chunk(compiler, OP_SUB);
                write_location(compiler, for_token);
            }

            write_chunk(compiler, OP_LSET);
            write_location(compiler, for_token);
            write_chunk(compiler, local_symbol->offset);

            write_chunk(compiler, OP_POP);
            write_location(compiler, for_token);

            // JUMP TO TEST SECTION
            jmp(compiler, for_token, ".FOR(%"PRId32")_TEST", for_id);

            // END OF THE FOR RANGE STATEMENT
            label(compiler, for_token, ".FOR(%"PRId32")_END", for_id);

            // FOR RANGE SCOPE
            pop_block(compiler);
            pop_loop(compiler);
            pop_locals(compiler);
            scope_manager_pop(manager);

            // BLOCK SCOPE
            label(compiler, for_token, ".FOR_RANGE(%"PRId32")_END", for_id);
            pop_locals(compiler);
            scope_manager_pop(manager);

            break;
        }case THROW_STMT_TYPE:{
            ThrowStmt *throw_stmt = stmt->sub_stmt;
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
            }

            write_chunk(compiler, OP_THROW);
            write_location(compiler, throw_token);
            write_chunk(compiler, throw_value_expr != NULL);

            break;
        }case TRY_STMT_TYPE:{
            TryStmt *try_stmt = stmt->sub_stmt;
            Token *try_token = try_stmt->try_token;
            DynArr *try_stmts = try_stmt->try_stmts;
            Token *catch_token = try_stmt->catch_token;
            DynArr *catch_stmts = try_stmt->catch_stmts;

            if(scope_manager_peek(manager)->type == CATCH_SCOPE_TYPE){
                error(
                    compiler,
                    try_token,
                    "Cannot use try statements inside catch scopes"
                );
            }

            uint32_t try_id = generate_id(compiler);
            Scope *try_scope = scope_manager_push(manager, TRY_SCOPE_TYPE);

            if(try_stmts){
          		Block *block = push_block(compiler);

            	write_chunk(compiler, OP_TRYO);
                write_location(compiler, try_token);
                mark(compiler, try_token, "CATCH(%"PRId32")", try_id);

                size_t len = dynarr_len(try_stmts);
                block->stmts_len = len;

                for (size_t i = 0; i < len; i++){
               		if(AS_LOCAL_SCOPE(try_scope)->returned){
              			error(
                 			compiler,
                  			try_token,
                   			"Cannot exists statements after the scope returned"
                 		);
                	}

                    Stmt *try_stmt = dynarr_get_ptr(try_stmts, i);
                    block->current_stmt = i + 1;

                    compile_stmt(compiler, try_stmt);
                }

                pop_locals(compiler);
                write_chunk(compiler, OP_TRYC);
                write_location(compiler, try_token);

                if(catch_stmts){
                    jmp(compiler, try_token, "CATCH(%"PRId32")_END", try_id);
                }

                pop_block(compiler);
            }

            scope_manager_pop(manager);
            Scope *catch_scope = scope_manager_push(manager, CATCH_SCOPE_TYPE);

            if(catch_stmts){
           		Block *block = push_block(compiler);

             	label(compiler, catch_token, "CATCH(%"PRId32")", try_id);
                pop_scope_locals(compiler, AS_LOCAL_SCOPE(try_scope));

                size_t len = dynarr_len(catch_stmts);
                block->stmts_len = len;

                for (size_t i = 0; i < len; i++){
                	if(AS_LOCAL_SCOPE(catch_scope)->returned){
               			error(
                  			compiler,
                   			catch_token,
                    		"Cannot exists statements after the scope returned"
                  		);
                 	}

                 	Stmt *catch_stmt = dynarr_get_ptr(catch_stmts, i);
                    block->current_stmt = i + 1;

                    compile_stmt(compiler, catch_stmt);
                }

                pop_locals(compiler);
                label(compiler, catch_token, "CATCH(%"PRId32")_END", try_id);

                pop_block(compiler);
            }else{
                label(compiler, catch_token, "CATCH(%"PRId32")", try_id);
                pop_scope_locals(compiler, AS_LOCAL_SCOPE(try_scope));
            }

            scope_manager_pop(manager);

            break;
        }case RETURN_STMT_TYPE:{
            ReturnStmt *ret_stmt = stmt->sub_stmt;
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
                if(ret_expr->type == IDENTIFIER_EXPRTYPE){
                    IdentifierExpr *identifier_expr = ret_expr->sub_expr;
                    Token *identifier_token = identifier_expr->identifier_token;
                    Symbol *symbol = scope_manager_get_symbol(compiler->manager, identifier_token);

                    if(symbol->type == MODULE_SYMBOL_TYPE){
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

            break;
        }case FUNCTION_STMT_TYPE:{
            FunctionStmt *fn_stmt = stmt->sub_stmt;
            Token *identifier_token = fn_stmt->identifier_token;
            DynArr *params = fn_stmt->params;
            DynArr *stmts = fn_stmt->stmts;
            size_t params_len = params ? dynarr_len(params) : 0;
            size_t stmts_len = stmts ? dynarr_len(stmts) : 0;

            if(!scope_manager_is_global_scope(manager)){
                error(
                    compiler,
                    identifier_token,
                    "Procedures declarations only allowed in global scope"
                );
            }

            Fn *fn = vm_factory_fn_create(
                compiler->rtallocator,
                identifier_token->lexeme,
                params_len
            );

            vm_factory_module_add_fn(compiler->module, fn, NULL);
            vm_factory_module_globals_add_obj(
            	current_module(compiler),
             	(Obj *)vm_factory_fn_obj_create(compiler->rtallocator, fn),
              	identifier_token->lexeme,
               	PRIVATE_GLOVAL_VALUE_TYPE
            );

            scope_manager_define_fn(manager, params_len, identifier_token);
            Scope *scope = scope_manager_push(manager, FN_SCOPE_TYPE);
            push_unit(compiler, fn);
            Block *block = push_block(compiler);

            for (size_t i = 0; i < params_len; i++){
                Token *param_identifier = dynarr_get_ptr(params, i);

                scope_manager_define_local(
                    manager,
                    1,
                    1,
                    param_identifier
                );
            }

            block->stmts_len = stmts_len;
            uint8_t must_return = 1;

            for (size_t i = 0; i < stmts_len; i++){
            	if(AS_LOCAL_SCOPE(scope)->returned){
           			error(
              			compiler,
                 		identifier_token,
                 		"Cannot exists statements after the scope returned"
             		);
             	}

             	Stmt *stmt = dynarr_get_ptr(stmts, i);
                block->current_stmt = i + 1;

                compile_stmt(compiler, stmt);

                if(i + 1 >= stmts_len && stmt->type == RETURN_STMT_TYPE){
                    must_return = 0;
                }
            }

            if(must_return){
                write_chunk(compiler, OP_EMPTY);
                write_chunk(compiler, OP_RET);
            }

            pop_block(compiler);
            pop_unit(compiler);
            scope_manager_pop(manager);

            break;
        }case IMPORT_STMT_TYPE:{
         	ImportStmt *import_stmt = stmt->sub_stmt;
            Token *import_token = import_stmt->import_token;
            DynArr *names = import_stmt->names;
            Token *alt_name_token = import_stmt->alt_name;

            size_t names_len = dynarr_len(names);
            Token *search_name_token = dynarr_get_ptr(names, names_len - 1);
            Token *declaration_name_token = alt_name_token ? alt_name_token : search_name_token;

            if(!scope_manager_is_global_scope(manager)){
          		error(
            		compiler,
              		import_token,
                	"Import statements only allowed in global scope"
            	);
            }

            if(names_len == 1){
           		if(import_native(compiler, search_name_token)){
           			scope_manager_define_module(manager, declaration_name_token);
            		return;
             	}
            }

            LZArena *compiler_arena = compiler->compiler_arena;
            void *compiler_arena_state = lzarena_save(compiler_arena);
            Allocator *import_ctallocator = memory_lzflist_allocator(compiler->arena_allocator, NULL);

            DStr *main_search_pathname = NULL;
            char *pathname = resolve_import_names(
                compiler,
                names,
                compiler->main_search_pathname,
                compiler->search_pathnames,
                import_token,
                &main_search_pathname
            );
            ScopeManager *imported_manager = NULL;
            Module *imported_module = import_module(
                compiler,
                import_ctallocator,
                compiler->ctallocator,
                import_token,
                main_search_pathname,
                pathname,
                search_name_token->lexeme,
                &imported_manager
            );

            Module *actual_module = current_module(compiler);
            ModuleObj *imported_module_obj = vm_factory_module_obj_create(
                compiler->rtallocator,
                imported_module
            );

            scope_manager_define_module(manager, declaration_name_token);

            vm_factory_module_add_module(actual_module, imported_module);
            vm_factory_module_globals_add_obj(
            	actual_module,
             	(Obj *)imported_module_obj,
              	declaration_name_token->lexeme,
               	PRIVATE_GLOVAL_VALUE_TYPE
            );

            lzarena_restore(compiler_arena, compiler_arena_state);

            break;
        }case EXPORT_STMT_TYPE:{
            ExportStmt *export_stmt = stmt->sub_stmt;
            Token *export_token = export_stmt->export_token;
            DynArr *symbols = export_stmt->symbols;
            size_t symbols_len = symbols ? dynarr_len(symbols) : 0;

            for (size_t i = 0; i < symbols_len; i++){
                Token *symbol_token = dynarr_get_ptr(symbols, i);

                write_chunk(compiler, OP_GASET);
                write_location(compiler, export_token);
                write_str_alloc(compiler, symbol_token->lexeme_len, symbol_token->lexeme);
                write_chunk(compiler, 1);
            }

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

        NativeFnObj *native_fn_obj = ((Value *)slot.value)->content.obj_val;
        NativeFn *native_fn = native_fn_obj->native_fn;

        scope_manager_define_native_fn(
            compiler->manager,
            native_fn->arity,
            native_fn->name
        );
    }
}

Compiler *compiler_create(const Allocator *ctallocator, const Allocator *rtallocator){
    LZPool *units_pool = MEMORY_LZPOOL(ctallocator, Unit);
    Compiler *compiler = MEMORY_ALLOC(ctallocator, Compiler, 1);

    if(!units_pool || !compiler){
        lzpool_dealloc(units_pool);
        MEMORY_DEALLOC(ctallocator, Compiler, 1, compiler);

        return NULL;
    }

    *compiler = (Compiler){
        .units_pool = units_pool,
        .ctallocator = ctallocator,
        .rtallocator = rtallocator
    };

    return compiler;
}

void compiler_destroy(Compiler *compiler){
    if(!compiler){
        return;
    }

    lzpool_destroy(compiler->units_pool);
}

Module *compiler_compile(
    Compiler *compiler,
    LZOHTable *keywords,
    DStr *main_search_pathname,
    DynArr *seatch_pathnames,
    LZOHTable *default_natives,
    ScopeManager *manager,
    DynArr *stmts,
    const char *pathname
){
    if(setjmp(compiler->buf) == 0){
        LZArena *compiler_arena = NULL;
        Allocator *arena_allocator = memory_arena_allocator(compiler->ctallocator, &compiler_arena);
        Module *main_module = vm_factory_module_create(compiler->rtallocator, "main", pathname);

        manager->buf = &compiler->buf;
        compiler->keywords = keywords;
        compiler->main_search_pathname = main_search_pathname;
        compiler->search_pathnames = seatch_pathnames;
        compiler->default_natives = default_natives;
        compiler->manager = manager;
        compiler->module = main_module;
        compiler->compiler_arena = compiler_arena;
        compiler->arena_allocator = arena_allocator;
        compiler->pssallocator = compiler->ctallocator;

        declare_defaults(compiler);

        size_t len = dynarr_len(stmts);
        Fn *entry_fn = vm_factory_fn_create(compiler->rtallocator, "entry", 0);

        main_module->entry_fn = entry_fn;

        vm_factory_module_add_fn(main_module, entry_fn, NULL);
        push_unit(compiler, entry_fn);

        for (size_t i = 0; i < len; i++){
            Stmt *stmt = dynarr_get_ptr(stmts, i);
            compile_stmt(compiler, stmt);
        }

        write_chunk(compiler, OP_EMPTY);
        write_chunk(compiler, OP_RET);
        pop_unit(compiler);

        return main_module;
    }else{
        return NULL;
    }

    return NULL;
}

Module *compiler_import(
    Compiler *compiler,
    LZArena *compiler_arena,
    Allocator *arena_allocator,
    const Allocator *pssallocator,
    LZOHTable *keywords,
    DStr *main_search_pathname,
    DynArr *search_pathnames,
    LZOHTable *default_natives,
    ScopeManager *manager,
    DynArr *stmts,
    const char *pathname,
    const char *name
){
    if(setjmp(compiler->buf) == 0){
        Module *import_module = vm_factory_module_create(compiler->rtallocator, name, pathname);

        manager->buf = &compiler->buf;
        compiler->keywords = keywords;
        compiler->main_search_pathname = main_search_pathname;
        compiler->search_pathnames = search_pathnames;
        compiler->default_natives = default_natives;
        compiler->manager = manager;
        compiler->module = import_module;
        compiler->compiler_arena = compiler_arena;
        compiler->arena_allocator = arena_allocator;
        compiler->pssallocator = pssallocator;

        declare_defaults(compiler);

        size_t len = dynarr_len(stmts);
        Fn *import_entry_fn = vm_factory_fn_create(compiler->rtallocator, "import entry", 0);

        import_module->entry_fn = import_entry_fn;

        vm_factory_module_add_fn(import_module, import_entry_fn, NULL);
        push_unit(compiler, import_entry_fn);

        for (size_t i = 0; i < len; i++){
            Stmt *stmt = dynarr_get_ptr(stmts, i);
            compile_stmt(compiler, stmt);
        }

        write_chunk(compiler, OP_EMPTY);
        write_chunk(compiler, OP_RET);
        pop_unit(compiler);

        return import_module;
    }else{
        return NULL;
    }

    return NULL;
}
