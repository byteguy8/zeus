#ifndef SCOPE_MANAGER_H
#define SCOPE_MANAGER_H

#include "essentials/lzpool.h"
#include "essentials/memory.h"

#include "scope.h"
#include "token.h"

#include <setjmp.h>

typedef struct scope_manager{
    jmp_buf         *err_buf;

    depth_t         depth;
    Scope           *scope_stack;
    Scope           *fn_scope_stack;
    Scope           *global_scope;
    DynArr          *current_scopes;

	const Allocator *allocator;
}ScopeManager;

ScopeManager *scope_manager_create(const Allocator *ctallocator);
void scope_manager_destroy(ScopeManager *manager);

DynArr *scope_manager_push_scopes(ScopeManager *manager);
DynArr *scope_manager_pop_scopes(ScopeManager *manager, DynArr *old_scopes);

Scope *scope_manager_peek(const ScopeManager *manager);
Scope *scope_manager_push(ScopeManager *manager, ScopeKind type);
void scope_manager_pop(ScopeManager *scope_manager);

int scope_manager_is_global_scope(const ScopeManager *manager);
int scope_manager_is_scope_type(const ScopeManager *manager, ScopeKind type);
int scope_manager_exists_procedure_name(const ScopeManager *manager, size_t name_len, const char *name);
Scope *scope_manager_is_loop(const ScopeManager *manager);
size_t scope_manager_locals_count(const ScopeManager *manager);

Symbol *scope_manager_get_symbol(ScopeManager *scope_manager, Token *identifier);

LocalSymbol *scope_manager_define_local(
    ScopeManager *manager,
    uint8_t is_mutable,
    uint8_t is_initialized,
    const Token *identifier_token
);
GlobalSymbol *scope_manager_define_global(
    ScopeManager *manager,
    uint8_t is_mutable,
    const Token *identifier_token
);
NativeFnSymbol *scope_manager_define_native_fn(
    ScopeManager *manager,
    uint8_t arity,
    const char *name
);
FnSymbol *scope_manager_define_fn(
    ScopeManager *manager,
    uint8_t arity,
    const Token *identifier_token
);
ModuleSymbol *scope_manager_define_module(
    ScopeManager *manager,
    const Token *identifier_token
);

#endif
