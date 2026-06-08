#ifndef CLOSURE_H
#define CLOSURE_H

#include "vm_types.h"

#include <stdint.h>

typedef struct foreign_value ForeingValue;
typedef struct closure_inf   ClosureInf;
typedef struct closure       Closure;
typedef struct closure_list  ClosureList;

struct foreign_value{
    int16_t local;
    Value   *value;
};

struct closure_inf{
    uint8_t locals_len;
    uint8_t *locals;
    Fn      *fn;
};

struct closure{
    ClosureInf   inf;
    ForeingValue *foreigns;
    Closure      *prev;
    Closure      *next;
    ClosureList  *list;
};

struct closure_list{
    Closure *head;
    Closure *tail;
};

#endif