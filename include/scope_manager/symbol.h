#ifndef SYMBOL_H
#define SYMBOL_H

#include "token.h"

#include <stdint.h>

typedef enum symbol_kind{
	LOCAL_SYMBOL_KIND,
    GLOBAL_SYMBOL_KIND,
    NATIVE_FN_SYMBOL_KIND,
	FN_SYMBOL_KIND,
	MODULE_SYMBOL_KIND,
}SymbolKind;

typedef struct symbol{
	SymbolKind  kind;
	const Token *identifier;
    const void  *scope;
}Symbol;

typedef struct local_symbol{
	Symbol  symbol;
	uint8_t offset;
	uint8_t is_mutable;
	uint8_t is_initialized;
}LocalSymbol;

typedef struct global_symbol{
    Symbol  symbol;
    uint8_t is_public;
    uint8_t is_mutable;
}GlobalSymbol;

typedef struct native_fn_symbol{
	Symbol     symbol;
	uint8_t    params_count;
    const char *name;
}NativeFnSymbol;

typedef struct fn_symbol{
	Symbol  symbol;
	uint8_t is_public;
    uint8_t params_count;
}FnSymbol;

typedef struct module_symbol{
	Symbol          symbol;
    const LZOHTable *symbols;
}ModuleSymbol;

#endif
