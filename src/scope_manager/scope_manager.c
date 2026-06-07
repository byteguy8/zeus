#include "scope_manager.h"
#include "scope.h"

#include "essentials/lzohtable.h"
#include "essentials/lzpool.h"
#include "essentials/memory.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static void error(ScopeManager *manager, const Token *token, const char *fmt, ...);
static void internal_error(ScopeManager *manager, const Token *token, const char *fmt, ...);
static void internal_error_nctx(ScopeManager *manager, const char *fmt, ...);

static Symbol *exists(Scope *scope, const Token *identifier);
static Symbol *exists_local(Scope *scope, const Token *identifier);

static int init_scope(ScopeManager *manager, Scope *scope, ScopeKind type);
static local_t create_locals_counter(ScopeManager *manager);
static local_t generate_local_offset(ScopeManager *manager, Scope *scope, const Token *ref_token);
static Scope *create_global_scope(const Allocator *allocator);

void error(ScopeManager *manager, const Token *token, const char *fmt, ...){
    va_list args;
	va_start(args, fmt);

	fprintf(stderr, "SCOPE ERROR at line %d:\n\t", token->line);
	vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

	va_end(args);

	longjmp(*manager->err_buf, 1);
}

void internal_error(ScopeManager *manager, const Token *token, const char *fmt, ...){
    va_list args;
	va_start(args, fmt);

	fprintf(stderr, "INTERNAL SCOPE ERROR at line %d:\n\t", token->line);
	vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

	va_end(args);

	longjmp(*manager->err_buf, 1);
}

void internal_error_nctx(ScopeManager *manager, const char *fmt, ...){
    va_list args;
	va_start(args, fmt);

	fprintf(stderr, "INTERNAL SCOPE ERROR:\n\t");
	vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

	va_end(args);

	longjmp(*manager->err_buf, 1);
}

inline Symbol *exists(Scope *scope, const Token *identifier){
    Symbol *symbol = NULL;

	if(lzohtable_lookup(
		identifier->lexeme_len,
		identifier->lexeme,
		scope->symbols,
		(void **)(&symbol)
	)){
		return symbol;
	}

    Scope *prev = scope->prev;

    if(prev){
        return exists(prev, identifier);
    }

	return NULL;
}

inline Symbol *exists_local(Scope *scope, const Token *identifier){
	Symbol *symbol = NULL;

	if(lzohtable_lookup(
		identifier->lexeme_len,
		identifier->lexeme,
		scope->symbols,
		(void **)(&symbol)
	)){
		return symbol;
	}

	return NULL;
}


inline int init_scope(ScopeManager *manager, Scope *scope, ScopeKind type){
    LZOHTable *symbols = MEMORY_LZOHTABLE_LEN(manager->allocator, 256);

    scope->type = type;
	scope->symbols = symbols;
    scope->prev = NULL;

    return 0;
}

inline local_t create_locals_counter(ScopeManager *manager){
    Scope *current = scope_manager_peek(manager);
    return IS_LOCAL_SCOPE(current) ? current->content.local_scope.locals : 0;
}

inline local_t generate_local_offset(ScopeManager *manager, Scope *scope, const Token *ref_token){
    assert(IS_LOCAL_SCOPE(scope) && "Expect local scope");
    LocalScope *local_scope = AS_LOCAL_SCOPE(scope);

    if(local_scope->locals == LOCAL_T_MAX){
        internal_error(
            manager,
            ref_token,
            "Cannot declare more than %"LOCAL_T_PRINT" locals per scope",
            local_scope->locals
        );
    }

    return local_scope->locals++;
}

Scope *create_global_scope(const Allocator *allocator){
    LZOHTable *global_symbols = MEMORY_LZOHTABLE_LEN(allocator, 64);
    Scope *global_scope = MEMORY_ALLOC(allocator, Scope, 1);

    MEMORY_CHECK(global_symbols);
    MEMORY_CHECK(global_scope);

    *global_scope = (Scope){
        .type = GLOBAL_SCOPE_KIND,
        .symbols = global_symbols
    };

    goto OK;

ERROR:
    LZOHTABLE_DESTROY(global_symbols);
    MEMORY_DEALLOC(allocator, Scope, 1, global_scope);

    return NULL;

OK:
    return global_scope;
}

ScopeManager *scope_manager_create(const Allocator *allocator){
    Scope *global_scope = create_global_scope(allocator);
    ScopeManager *manager = MEMORY_ALLOC(allocator, ScopeManager, 1);

    MEMORY_CHECK(global_scope);
    MEMORY_CHECK(manager);

    *manager = (ScopeManager){
        .depth = 0,
        .scope_stack = global_scope,
        .global_scope = global_scope,
        .allocator = allocator
    };

    goto OK;

ERROR:
    MEMORY_DEALLOC(allocator, Scope, 1, global_scope);
    MEMORY_DEALLOC(allocator, ScopeManager, 1, manager);

    return NULL;

OK:
    return manager;
}

void scope_manager_destroy(ScopeManager *manager){
    if(!manager){
        return;
    }

    const Allocator *allocator = manager->allocator;

    MEMORY_DEALLOC(allocator, Scope, 1, manager->global_scope);
    MEMORY_DEALLOC(allocator, ScopeManager, 1, manager);
}

inline DynArr *scope_manager_push_scopes(ScopeManager *manager){
    DynArr *old_scopes = manager->current_scopes;
    DynArr *new_scopes = MEMORY_DYNARR_PTR(manager->allocator);

    manager->current_scopes = new_scopes;

    return old_scopes;
}

inline DynArr *scope_manager_pop_scopes(ScopeManager *manager, DynArr *old_scopes){
    assert(manager->current_scopes);

    DynArr *new_scopes = manager->current_scopes;

    manager->current_scopes = old_scopes;

    return new_scopes;
}

inline Scope *scope_manager_peek(const ScopeManager *manager){
    assert(manager->global_scope);
    assert(manager->scope_stack);

    return manager->scope_stack;
}

Scope *scope_manager_push(ScopeManager *manager, ScopeKind type){
    assert(type != GLOBAL_SCOPE_KIND);

    if(manager->depth == DEPTH_T_MAX){
        internal_error_nctx(
            manager,
            "Scopes depth exceeded max capacity (%"DEPTH_T_PRINT")",
            DEPTH_T_MAX
        );
    }

    Scope *new_scope = MEMORY_ALLOC(manager->allocator, Scope, 1);

    switch (type){
        case BLOCK_SCOPE_KIND:
        case IF_SCOPE_KIND:
        case ELIF_SCOPE_KIND:
        case ELSE_SCOPE_KIND:
        case WHILE_SCOPE_KIND:
        case FOR_SCOPE_KIND:
        case TRY_SCOPE_KIND:
        case CATCH_SCOPE_KIND:{
            new_scope->content.local_scope = (LocalScope){
            	.depth = manager->depth,
                .locals = create_locals_counter(manager),
                .returned = 0,
                .fn_scope = manager->fn_scope_stack
            };

            break;
        }case FN_SCOPE_KIND:{
            new_scope->content.fn_scope = (FnScope){
            	.depth = manager->depth += 1,
                .locals = 0,
                .returned = 0,
                .prev_fn = manager->fn_scope_stack
            };

            manager->fn_scope_stack = new_scope;
        }case GLOBAL_SCOPE_KIND:{
            break;
        }default:{
            assert(0 && "Illegal scope type");
            break;
        }
    }

    init_scope(manager, new_scope, type);

    new_scope->prev = scope_manager_peek(manager);
    manager->scope_stack = new_scope;

    if(manager->current_scopes){
        dynarr_insert_ptr(manager->current_scopes, new_scope);
    }

	return new_scope;
}

void scope_manager_pop(ScopeManager *manager){
	assert(manager->scope_stack);

	Scope *current_scope = manager->scope_stack;

    manager->scope_stack = current_scope->prev;

    if(IS_FN_SCOPE(current_scope)){
        assert(manager->depth > 0);

        manager->depth--;
        manager->fn_scope_stack = AS_FN_SCOPE(current_scope)->prev_fn;
    }

    if(manager->current_scopes){
        dynarr_insert_ptr(manager->current_scopes, current_scope);
    }
}

inline int scope_manager_is_global_scope(const ScopeManager *manager){
    assert(manager->scope_stack);

    return IS_GLOBAL_SCOPE(manager->scope_stack);
}

int scope_manager_is_scope_type(const ScopeManager *manager, ScopeKind type){
    Scope *current = scope_manager_peek(manager);

    do{
    	if(current->type == FN_SCOPE_KIND){
   			return 0;
     	}

     	if(current->type == type){
            return 1;
        }

        current = current->prev;
    }while(current);

    return 0;
}

inline int scope_manager_exists_procedure_name(
    const ScopeManager *manager,
    size_t name_len,
    const char *name
){
    Scope *global_scope = manager->global_scope;
    LZOHTable *symbols = global_scope->symbols;

    return lzohtable_lookup(
        name_len,
        name,
        symbols,
        NULL
    );
}

Scope *scope_manager_is_loop(const ScopeManager *manager){
    Scope *current = scope_manager_peek(manager);

    do{
        if(current->type == WHILE_SCOPE_KIND || current->type == FOR_SCOPE_KIND){
            return current;
        }

        current = current->prev;
    }while(current->type != GLOBAL_SCOPE_KIND);

    return NULL;
}

inline size_t scope_manager_locals_count(const ScopeManager *manager){
    Scope *current = scope_manager_peek(manager);
    assert(!IS_GLOBAL_SCOPE(current));
    return current->symbols->n;
}

Symbol *scope_manager_get_symbol(ScopeManager *manager, Token *identifier){
	Scope *current = scope_manager_peek(manager);
    Symbol *symbol = exists(current, identifier);

    if(!symbol){
        error(
            manager,
            identifier,
            "Symbol '%s' doesn't exists",
            identifier->lexeme
        );
    }

    return symbol;
}

LocalSymbol *scope_manager_define_local(
    ScopeManager *manager,
    uint8_t is_mutable,
    uint8_t is_initialized,
    const Token *identifier_token
){
    Scope *scope = scope_manager_peek(manager);

    assert(IS_LOCAL_SCOPE(scope) && "Scope must be local");

	if(exists_local(scope, identifier_token)){
		error(
			manager,
			identifier_token,
			"Already exists symbol '%s' in current scope",
			identifier_token->lexeme
		);
	}

	LocalSymbol *local_symbol = MEMORY_ALLOC(manager->allocator, LocalSymbol, 1);
	Symbol *symbol = &local_symbol->symbol;

	symbol->kind = LOCAL_SYMBOL_KIND;
	symbol->identifier = identifier_token;
    symbol->scope = scope;

    local_symbol->offset = generate_local_offset(manager, scope, identifier_token);
	local_symbol->is_mutable = is_mutable;
	local_symbol->is_initialized = is_initialized;

    lzohtable_put_ck(
        identifier_token->lexeme_len,
        identifier_token->lexeme,
        symbol,
        scope->symbols,
        NULL
    );

	return local_symbol;
}

GlobalSymbol *scope_manager_define_global(
    ScopeManager *manager,
    uint8_t is_mutable,
    const Token *identifier_token
){
    Scope *scope = scope_manager_peek(manager);

    assert(scope->type == GLOBAL_SCOPE_KIND && "Scope must be global");

	if(exists_local(scope, identifier_token)){
		error(
			manager,
			identifier_token,
			"Already exists symbol '%s' in current scope",
			identifier_token->lexeme
		);
	}

	GlobalSymbol *global_symbol = MEMORY_ALLOC(manager->allocator, GlobalSymbol, 1);
	Symbol *symbol = &global_symbol->symbol;

	symbol->kind = GLOBAL_SYMBOL_KIND;
	symbol->identifier = identifier_token;
    symbol->scope = scope;

    global_symbol->is_mutable = is_mutable;

    lzohtable_put_ck(
        identifier_token->lexeme_len,
        identifier_token->lexeme,
        symbol,
        scope->symbols,
        NULL
    );

	return global_symbol;
}

NativeFnSymbol *scope_manager_define_native_fn(
    ScopeManager *manager,
    uint8_t arity,
    const char *name
){
    Scope *scope = scope_manager_peek(manager);
	NativeFnSymbol *native_fn_symbol = MEMORY_ALLOC(manager->allocator, NativeFnSymbol, 1);
	Symbol *symbol = &native_fn_symbol->symbol;

	symbol->kind = NATIVE_FN_SYMBOL_KIND;
	symbol->identifier = NULL;
    symbol->scope = scope;

    native_fn_symbol->params_count = arity;
    native_fn_symbol->name = name;

    lzohtable_put_ck(
        strlen(name),
        name,
        symbol,
        scope->symbols,
        NULL
    );

	return native_fn_symbol;
}

FnSymbol *scope_manager_define_fn(
    ScopeManager *manager,
    uint8_t arity,
    const Token *identifier_token
){
    Scope *scope = scope_manager_peek(manager);

	if(exists_local(scope, identifier_token)){
		error(
			manager,
			identifier_token,
			"Already exists symbol '%s' in current scope",
			identifier_token->lexeme
		);
	}

	FnSymbol *fn_symbol = MEMORY_ALLOC(manager->allocator, FnSymbol, 1);
	Symbol *symbol = &fn_symbol->symbol;

	symbol->kind = FN_SYMBOL_KIND;
	symbol->identifier = identifier_token;
    symbol->scope = scope;
	fn_symbol->params_count = arity;

    lzohtable_put_ck(
        identifier_token->lexeme_len,
        identifier_token->lexeme,
        symbol,
        scope->symbols,
        NULL
    );

	return fn_symbol;
}

ModuleSymbol *scope_manager_define_module(
    ScopeManager *manager,
    const Token *identifier_token
){
    Scope *scope = scope_manager_peek(manager);

	if(exists_local(scope, identifier_token)){
		error(
			manager,
			identifier_token,
			"Already exists symbol '%s' in current scope",
			identifier_token->lexeme
		);
	}

	ModuleSymbol *module_symbol = MEMORY_ALLOC(manager->allocator, ModuleSymbol, 1);
	Symbol *symbol = &module_symbol->symbol;

	symbol->kind = MODULE_SYMBOL_KIND;
	symbol->identifier = identifier_token;
    symbol->scope = scope;

    lzohtable_put_ck(
        identifier_token->lexeme_len,
        identifier_token->lexeme,
        symbol,
        scope->symbols,
        NULL
    );

	return module_symbol;
}
