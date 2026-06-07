#ifndef SCOPE_H
#define SCOPE_H

#include "essentials/lzohtable.h"
#include "essentials/lzarena.h"
#include "essentials/lzpool.h"
#include "essentials/memory.h"

#include "token.h"
#include "symbol.h"

#include <setjmp.h>
#include <inttypes.h>

typedef enum scope_kind{
    BLOCK_SCOPE_KIND,
    IF_SCOPE_KIND,
    ELIF_SCOPE_KIND,
    ELSE_SCOPE_KIND,
    WHILE_SCOPE_KIND,
    FOR_SCOPE_KIND,
    TRY_SCOPE_KIND,
    CATCH_SCOPE_KIND,
	FN_SCOPE_KIND,
	GLOBAL_SCOPE_KIND,
}ScopeKind;

typedef uint8_t       depth_t;
#define DEPTH_T_MAX   UINT8_MAX
#define DEPTH_T_PRINT PRIu8

typedef uint8_t       local_t;
#define LOCAL_T_MAX   UINT8_MAX
#define LOCAL_T_PRINT PRIu16

typedef struct scope        Scope;
typedef struct local_scope  LocalScope;
typedef struct fn_scope     FnScope;

struct local_scope{
    depth_t depth;
    local_t locals;
    uint8_t returned;
    Scope   *fn_scope;
};

struct fn_scope{
    depth_t depth;
    local_t locals;
    uint8_t returned;
    Scope   *prev_fn;
};

struct scope{
    ScopeKind type;
	LZOHTable *symbols;
    Scope     *prev;

    union{
        LocalScope local_scope;
        FnScope    fn_scope;
    }content;
};

#define IS_LOCAL_SCOPE(_scope)           ((_scope)->type != GLOBAL_SCOPE_KIND)
#define IS_BLOCK_SCOPE(_scope)           ((_scope)->type == BLOCK_SCOPE_KIND)
#define IS_FN_SCOPE(_scope)              ((_scope)->type == FN_SCOPE_KIND)
#define IS_GLOBAL_SCOPE(_scope)          ((_scope)->type == GLOBAL_SCOPE_KIND)

#define AS_LOCAL_SCOPE(_scope)           (&((_scope)->content.local_scope))
#define AS_FN_SCOPE(_scope)              (&((_scope)->content.fn_scope))

#define PREV_SCOPE(_scope)               ((_scope)->prev)
#define LOCAL_SCOPE_LOCALS_COUNT(_scope) ((_scope)->locals)

#endif
