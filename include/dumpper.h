#ifndef DUMPPER_H
#define DUMPPER_H

#include "essentials/dynarr.h"
#include "vm/fn.h"
#include "vm/module.h"

typedef struct dumpper{
    size_t          ip;
    LZOHTable       *contexts;
    ModuleContext   *main_context;
    ModuleContext   *current_context;
    Fn              *current_fn;
    LZBStr          *helper_str;
    const Allocator *allocator;
}Dumpper;

Dumpper *dumpper_create(const Allocator *allocator);
void dumpper_dump(LZOHTable *contexts, Module *main_module, Dumpper *dumpper);

#endif
