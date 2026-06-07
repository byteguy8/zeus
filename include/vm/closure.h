#ifndef CLOSURE_H
#define CLOSURE_H

#include "vm_types.h"

#include <stdint.h>

typedef struct foreign_value{
    bool_t               linked;
    int16_t              local;
    Value                *value;
    struct foreign_value *prev;
    struct foreign_value *next;
}ForeignValue;

typedef struct closure{
    uint8_t      locals_len;
    uint8_t      *locals;
    ForeignValue *foreigns;
    Fn           *fn;
}Closure;

#endif