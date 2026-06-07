#ifndef VALUE_H
#define VALUE_H

#include "vm_types.h"

#include <stdint.h>

enum value_type{
    EMPTY_VALUE_TYPE,
    BOOL_VALUE_TYPE,
    INT_VALUE_TYPE,
    FLOAT_VALUE_TYPE,
	OBJ_VALUE_TYPE
};

struct value{
    ValueType type;

    union{
        uint8_t bool_val;
        int64_t int_val;
        double  float_val;
		Obj     *obj_val;
    }content;
};

enum global_value_access_type{
    PRIVATE_GLOBAL_VALUE_TYPE,
    PUBLIC_GLOBAL_VALUE_TYPE,
};

struct global_value{
    GlobalValueAccessType access;
    Value                 value;
};

#define VALUE_SIZE sizeof(Value)
#define VALUE_TYPE(_value)((_value)->type)
#define IS_VALUE_MARKED(_object)(((ObjHeader *)(_object))->marked == 1)

#endif