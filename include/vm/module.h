#ifndef MODULE_H
#define MODULE_H

#include "vm_types.h"

#include "essentials/memory.h"
#include "essentials/dynarr.h"
#include "essentials/lzohtable.h"

typedef struct try_block{
    size_t           try;
    size_t           catch;
	uint8_t          local;
    struct try_block *outer;
}TryBlock;

typedef struct module_context{
    bool_t    resolved;
    char      *pathname;
    Fn        *entry_fn;
    DynArr    *str_consts;
    DynArr    *fns;
    DynArr    *closures;
    LZOHTable *globals;
}ModuleContext;

struct module{
    bool_t        original;
    char          *name;
    ModuleContext *context;
};

#define MODULE_STRINGS(_module)((_module)->context->str_consts)
#define MODULE_SYMBOLS(_module)((_module)->context->symbols)
#define MODULE_GLOBALS(_module)((_module)->context->globals)

#define MODULE_CLOSURES(_module) ((_module)->context->closures)

#endif