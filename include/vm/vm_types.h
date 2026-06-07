#ifndef VM_TYPES
#define VM_TYPES

#include <stddef.h>
#include <inttypes.h>

#define false 0
#define true 1

typedef unsigned char bool_t;
typedef unsigned char byte_t;

typedef struct static_str{
    size_t len;
    char   *buff; // NULL terminated
}StaticStr;

typedef struct fn                     Fn;
typedef struct module                 Module;

typedef enum value_type               ValueType;
typedef struct value                  Value;

typedef enum   obj_type               ObjType;
typedef enum   obj_color              ObjColor;
typedef struct obj                    Obj;
typedef struct obj_list               ObjList;

typedef struct str_obj                StrObj;
typedef struct array_obj              ArrayObj;
typedef struct list_obj               ListObj;
typedef struct dict_obj               DictObj;
typedef struct record_obj             RecordObj;
typedef struct native_obj             NativeObj;
typedef struct native_fn_obj          NativeFnObj;
typedef struct fn_obj                 FnObj;
typedef struct closure_obj            ClosureObj;
typedef struct native_module_obj      NativeModuleObj;
typedef struct module_obj             ModuleObj;

typedef enum global_value_access_type GlobalValueAccessType;
typedef struct global_value           GlobalValue;

typedef struct vm                     VM;

#endif