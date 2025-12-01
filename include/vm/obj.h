#ifndef OBJ_H
#define OBJ_H

#include "essentials/dynarr.h"
#include "value.h"
#include "native_fn.h"
#include "fn.h"
#include "closure.h"
#include "native_module.h"
#include "module.h"
#include <stdio.h>
#include <inttypes.h>

typedef enum obj_type ObjType;
typedef enum obj_color ObjColor;
typedef struct obj Obj;
typedef struct obj_list ObjList;

enum obj_type{
	STR_OBJ_TYPE,
    ARRAY_OBJ_TYPE,
	LIST_OBJ_TYPE,
    DICT_OBJ_TYPE,
	RECORD_OBJ_TYPE,
	NATIVE_OBJ_TYPE,
    NATIVE_FN_OBJ_TYPE,
    FN_OBJ_TYPE,
    CLOSURE_OBJ_TYPE,
    NATIVE_MODULE_OBJ_TYPE,
    MODULE_OBJ_TYPE,
};

enum obj_color{
    TRANSPARENT_OBJ_COLOR,
    WHITE_OBJ_COLOR,
    GRAY_OBJ_COLOR,
    BLACK_OBJ_COLOR,
};

struct obj{
    ObjType type;
    char marked;
    ObjColor color;
    Obj *prev;
    Obj *next;
    ObjList *list;
};

struct obj_list{
    size_t len;
    Obj *head;
    Obj *tail;
};

typedef struct str_obj{
    Obj header;
    char runtime;
    size_t len;
	char *buff;
}StrObj;

typedef struct array_obj{
    Obj header;
    size_t len;
    Value *values;
}ArrayObj;

typedef struct list_obj{
    Obj header;
    DynArr *items;
}ListObj;

typedef struct dict_obj{
    Obj header;
    LZOHTable *key_values;
}DictObj;

typedef struct record_obj{
    Obj header;
	LZOHTable *attrs;
}RecordObj;

typedef struct native_obj{
	Obj header;
	void *native;
}NativeObj;

typedef struct native_fn_obj{
    Obj header;
    Value target;
    NativeFn *native_fn;
}NativeFnObj;

typedef struct fn_obj{
    Obj header;
    const Fn *fn;
}FnObj;

typedef struct closure_obj{
    Obj header;
    Closure *closure;
}ClosureObj;

typedef struct native_module_obj{
    Obj header;
    NativeModule *native_module;
}NativeModuleObj;

typedef struct module_obj{
    Obj header;
    Module *module;
}ModuleObj;

void obj_list_insert(Obj *obj, ObjList *list);
void obj_list_remove(Obj *obj);

#endif
