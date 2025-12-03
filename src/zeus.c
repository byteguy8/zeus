#include "essentials/memory.h"

#include "utils.h"

#include "token.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "dumpper.h"

#include "scope_manager/scope_manager.h"
#include "native_module/native_module_default.h"

#include "vm/vm_factory.h"
#include "vm/vmu.h"
#include "vm/vm.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#define DEFAULT_INITIAL_COMPILE_TIME_MEMORY MEMORY_MIBIBYTES(2)
#define DEFAULT_INITIAL_RUNTIME_MEMORY MEMORY_MIBIBYTES(3)
#define DEFAULT_INITIAL_SEARCH_PATHS_BUFF_LEN 256

static LZFList *ctflist = NULL;
static LZFList *rtflist = NULL;
static Allocator ctallocator = {0};
static Allocator rtallocator = {0};

typedef struct args{
    uint8_t help;
    uint8_t exclusives;
    char    *search_paths;
    char    *source_pathname;
}Args;

#define ARGS_LEX     0b00000001
#define ARGS_PARSE   0b00000010
#define ARGS_COMPILE 0b00000100
#define ARGS_DUMP    0b00001000

static LZFList *allocator = NULL;

void *lzalloc_link_flist(size_t size, void *ctx){
    void *ptr = lzflist_alloc(ctx, size);

    if(!ptr){
        fprintf(stderr, "It seems the system ran out of memory");
        lzflist_destroy(allocator);
        exit(EXIT_FAILURE);
    }

    return ptr;
}

void *lzrealloc_link_flist(void *ptr, size_t old_size, size_t new_size, void *ctx){
    void *new_ptr = lzflist_realloc(ctx, ptr, new_size);

    if(!new_ptr){
        fprintf(stderr, "It seems the system ran out of memory");
        lzflist_destroy(allocator);
        exit(EXIT_FAILURE);
    }

    return new_ptr;
}

void lzdealloc_link_flist(void *ptr, size_t size, void *ctx){
    lzflist_dealloc(ctx, ptr);
}

void *lzalloc_link_arena(size_t size, void *ctx){
    return LZARENA_ALLOC(ctx, size);
}

void *lzrealloc_link_arena(void *ptr, size_t old_size, size_t new_size, void *ctx){
    return LZARENA_REALLOC(ctx, ptr, old_size, new_size);
}

void lzdealloc_link_arena(void *ptr, size_t size, void *ctx){
    // nothing to be done
}

static void get_args(int argc, const char *argv[], Args *args){
    for(int i = 1; i < argc; i++){
		const char *arg = argv[i];

		if(strcmp("-l", arg) == 0){
            if(args->exclusives != 0){
                fprintf(stderr, "ERROR: flags '-l', '-p', '-c' and '-d' are mutually exclusive\n");
                exit(EXIT_FAILURE);
            }

            if(args->exclusives & ARGS_LEX){
                fprintf(stderr, "ERROR: -l flag already used\n");
                exit(EXIT_FAILURE);
            }

            args->exclusives |= ARGS_LEX;
        }else if(strcmp("-p", arg) == 0){
            if(args->exclusives != 0){
                fprintf(stderr, "ERROR: flags '-l', '-p', '-c' and -d are mutually exclusive\n");
                exit(EXIT_FAILURE);
            }

            if(args->exclusives & ARGS_PARSE){
                fprintf(stderr, "ERROR: '-p' flag already used\n");
                exit(EXIT_FAILURE);
            }

            args->exclusives |= ARGS_PARSE;
        }else if(strcmp("-c", arg) == 0){
            if(args->exclusives != 0){
                fprintf(stderr, "ERROR: flags '-l', '-p', '-c' and '-d' are mutually exclusive\n");
                exit(EXIT_FAILURE);
            }

            if(args->exclusives & ARGS_COMPILE){
                fprintf(stderr, "ERROR: '-c' flag already used\n");
                exit(EXIT_FAILURE);
            }

            args->exclusives |= ARGS_COMPILE;
        }else if(strcmp("-d", arg) == 0){
            if(args->exclusives != 0){
                fprintf(stderr, "ERROR: flags '-l', '-p', '-c' and '-d' are mutually exclusive\n");
                exit(EXIT_FAILURE);
            }

            if(args->exclusives & ARGS_DUMP){
                fprintf(stderr, "ERROR: '-d' flag already used\n");
                exit(EXIT_FAILURE);
            }

            args->exclusives |= ARGS_DUMP;
        }else if(strcmp("-h", arg) == 0){
            if(args->help){
                fprintf(stderr, "ERROR: '-h' flag already used\n");
                exit(EXIT_FAILURE);
            }

            args->help = 1;
        }else if(strcmp("--search-paths", arg) == 0){
            if(args->search_paths){
                fprintf(stderr, "ERROR: 'search paths' already set\n");
                exit(EXIT_FAILURE);
            }

            if(i + 1 >= argc){
                fprintf(stderr, "ERROR: expect 'search paths' after '--search-paths' flag\n");
                exit(EXIT_FAILURE);
            }

            args->search_paths = (char *)argv[++i];
        }else{
            if(args->source_pathname){
                fprintf(stderr, "ERROR: 'Source pathname' already set\n");
                exit(EXIT_FAILURE);
            }

            args->source_pathname = (char *)arg;
        }
	}

    if(args->help && (args->exclusives || args->search_paths || args->source_pathname)){
        fprintf(stderr, "ERROR: flag '-h' must be used alone\n");
        exit(EXIT_FAILURE);
    }

    if(args->exclusives && !args->source_pathname){
        fprintf(
            stderr,
            "ERROR: expect 'source pathname' with flags: '-l', '-p', '-c' and '-d'\n"
        );
        exit(EXIT_FAILURE);
    }

    if(args->search_paths && !args->source_pathname){
        fprintf(
            stderr,
            "ERROR: expect 'source pathname' with flag '--search-paths'\n"
        );
        exit(EXIT_FAILURE);
    }
}

DStr get_cwd(Allocator *allocator){
    char *buff = utils_files_cwd(allocator);
    size_t len = strlen(buff);

    return (DStr){
        .len = len,
        .buff = buff
    };
}

DynArr *parse_search_paths(Allocator *allocator, DStr *main_search_pathname, char *raw_search_paths){
    DynArr *search_paths = MEMORY_DYNARR_TYPE_COUNT(
        allocator,
        DStr,
        DEFAULT_INITIAL_SEARCH_PATHS_BUFF_LEN
    );
    DStr cwd_rstr = get_cwd(allocator);

    if(strcmp(main_search_pathname->buff, cwd_rstr.buff) == 0){
        MEMORY_DEALLOC(allocator, char, cwd_rstr.len + 1, cwd_rstr.buff);
    }else{
        dynarr_insert(search_paths, &cwd_rstr);
    }

    size_t start_idx = 0;
    size_t raw_search_paths_len = raw_search_paths ? strlen(raw_search_paths) : 0;

    if(raw_search_paths_len == 0){
        return search_paths;
    }

    if(raw_search_paths[0] == OS_PATH_SEPARATOR){
        fprintf(
            stderr,
            "'search paths' cannot starts with '%c'",
            OS_PATH_SEPARATOR
        );
        exit(EXIT_FAILURE);
    }

    if(raw_search_paths[raw_search_paths_len - 1] == OS_PATH_SEPARATOR){
        fprintf(
            stderr,
            "'search paths' cannot ends with '%c'",
            OS_PATH_SEPARATOR
        );
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < raw_search_paths_len; i++){
        char c = raw_search_paths[i];

        if(i + 1 < raw_search_paths_len && c != OS_PATH_SEPARATOR){
            continue;
        }

        size_t buff_len = i - start_idx + (c == OS_PATH_SEPARATOR ? 0 : 1);
        char *buff = MEMORY_ALLOC(allocator, char, buff_len + 1);

        memcpy(buff, raw_search_paths + start_idx, buff_len);
        buff[buff_len] = 0;

        dynarr_insert(search_paths, &((DStr){.len = buff_len, .buff = buff}));

        start_idx = i + 1;
    }

    return search_paths;
}

static DStr *create_main_search_pathname(const Allocator *allocator, const char *source_pathname){
    char *parent_source_pathname = utils_files_parent_pathname(allocator, source_pathname);
    DStr *main_search_pathname = MEMORY_ALLOC(allocator, DStr, 1);

    *main_search_pathname = (DStr){
        .len = strlen(parent_source_pathname),
        .buff = parent_source_pathname
    };

    return main_search_pathname;
}

void add_native(const char *name, int arity, RawNativeFn raw_native, LZOHTable *natives, const Allocator *allocator){
    NativeFn *native_fn = vm_factory_native_fn_create(allocator, 1, name, arity, raw_native);
    lzohtable_put_ck(strlen(name), name, native_fn, natives, NULL);
}

void add_keyword(const char *name, TokType type, LZOHTable *keywords, const Allocator *allocator){
    lzohtable_put_ckv(strlen(name), name, sizeof(TokType), &type, keywords, NULL);
}

LZOHTable *create_keywords_table(const Allocator *allocator){
	LZOHTable *keywords = MEMORY_LZOHTABLE_LEN(allocator, 64);

	add_keyword("mod", MOD_TOKTYPE, keywords, allocator);
	add_keyword("empty", EMPTY_TOKTYPE, keywords, allocator);
    add_keyword("false", FALSE_TOKTYPE, keywords, allocator);
    add_keyword("true", TRUE_TOKTYPE, keywords, allocator);
    add_keyword("make", MAKE_TOKTYPE, keywords, allocator);
    add_keyword("mut", MUT_TOKTYPE, keywords, allocator);
    add_keyword("or", OR_TOKTYPE, keywords, allocator);
    add_keyword("and", AND_TOKTYPE, keywords, allocator);
	add_keyword("if", IF_TOKTYPE, keywords, allocator);
    add_keyword("elif", ELIF_TOKTYPE, keywords, allocator);
	add_keyword("else", ELSE_TOKTYPE, keywords, allocator);
	add_keyword("while", WHILE_TOKTYPE, keywords, allocator);
    add_keyword("for", FOR_TOKTYPE, keywords, allocator);
    add_keyword("upto", UPTO_TOKTYPE, keywords, allocator);
    add_keyword("downto", DOWNTO_TOKTYPE, keywords, allocator);
	add_keyword("stop", STOP_TOKTYPE, keywords, allocator);
    add_keyword("continue", CONTINUE_TOKTYPE, keywords, allocator);
    add_keyword("array", ARRAY_TOKTYPE, keywords, allocator);
	add_keyword("list", LIST_TOKTYPE, keywords, allocator);
    add_keyword("to", TO_TOKTYPE, keywords, allocator);
    add_keyword("dict", DICT_TOKTYPE, keywords, allocator);
	add_keyword("record", RECORD_TOKTYPE, keywords, allocator);
    add_keyword("proc", PROC_TOKTYPE, keywords, allocator);
    add_keyword("anon", ANON_TOKTYPE, keywords, allocator);
    add_keyword("ret", RET_TOKTYPE, keywords, allocator);
    add_keyword("import", IMPORT_TOKTYPE, keywords, allocator);
    add_keyword("as", AS_TOKTYPE, keywords, allocator);
	add_keyword("bool", BOOL_TOKTYPE, keywords, allocator);
	add_keyword("int", INT_TOKTYPE, keywords, allocator);
	add_keyword("float", FLOAT_TOKTYPE, keywords, allocator);
	add_keyword("str", STR_TOKTYPE, keywords, allocator);
	add_keyword("is", IS_TOKTYPE, keywords, allocator);
    add_keyword("try", TRY_TOKTYPE, keywords, allocator);
    add_keyword("catch", CATCH_TOKTYPE, keywords, allocator);
    add_keyword("throw", THROW_TOKTYPE, keywords, allocator);
    add_keyword("export", EXPORT_TOKTYPE, keywords, allocator);

    return keywords;
}

static void print_help(){
    fprintf(stderr, "Usage: zeus [ /path/to/source/file.ze [Options] | -h ]\n");

    fprintf(stderr, "\n    The Zeus Programming Language\n");
    fprintf(stderr, "        Zeus is a dynamic programming language made for learning purposes\n\n");

    fprintf(stderr, "Options:\n");

    fprintf(stderr, "    -l\n");
    fprintf(stderr, "                      Just run the lexer\n");

    fprintf(stderr, "    -p\n");
    fprintf(stderr, "                      Just run the lexer and parser\n");

    fprintf(stderr, "    -c\n");
    fprintf(stderr, "                      Just run the lexer, parser and compiler\n");

    fprintf(stderr, "    -d\n");
    fprintf(stderr, "                      Run the disassembler (executing: lexer, parser and compiler)\n");

    fprintf(stderr, "    --search-paths\n");
    fprintf(stderr, "                      Make compiler aware of the paths it must use for imports.\n");
    fprintf(stderr, "                      The paths must be separated by the OS's paths separator.\n");
    fprintf(stderr, "                      In Windows is ';', while in Linux is ':'. For example:\n");
    fprintf(stderr, "                          Windows:\n");
    fprintf(stderr, "                              D:\\path\\a;D:\\path\\b;D:\\path\\c\n");
    fprintf(stderr, "                          Linux:\n");
    fprintf(stderr, "                              /path/a:path/b:path/c\n");

    exit(EXIT_FAILURE);
}

static void init_memory(){
    ctflist = lzflist_create(NULL);
    rtflist = lzflist_create(NULL);

    if(!ctflist || !rtflist){
        lzflist_destroy(rtflist);
        fprintf(stderr, "Failed to init memory");
        exit(EXIT_FAILURE);
    }

    if(lzflist_prealloc(ctflist, DEFAULT_INITIAL_COMPILE_TIME_MEMORY) ||
       lzflist_prealloc(rtflist, DEFAULT_INITIAL_RUNTIME_MEMORY)
    ){
        lzflist_destroy(ctflist);
        lzflist_destroy(rtflist);
        fprintf(stderr, "Failed to init memory");
        exit(EXIT_FAILURE);
    }

    MEMORY_INIT_ALLOCATOR(
        ctflist,
        lzalloc_link_flist,
        lzrealloc_link_flist,
        lzdealloc_link_flist,
        &ctallocator
    );

    MEMORY_INIT_ALLOCATOR(
        rtflist,
        lzalloc_link_flist,
        lzrealloc_link_flist,
        lzdealloc_link_flist,
        &rtallocator
    );
}

static void add_native_fn_obj(
    LZOHTable *natives,
    const Allocator *allocator,
    const char *name,
    uint8_t arity,
    RawNativeFn raw_native_fn
){
    NativeFn *native_fn = vm_factory_native_fn_create(allocator, 1, name, arity, raw_native_fn);
    NativeFnObj *native_fn_obj = vm_factory_native_fn_obj_create(allocator, native_fn);

    lzohtable_put_ckv(
        strlen(name),
        name,
        sizeof(Value),
        &((Value){.type = OBJ_VALUE_TYPE, .content.obj_val = native_fn_obj}),
        natives,
        NULL
    );
}

static LZOHTable *create_default_native_fns(Allocator *allocator){
    LZOHTable *default_natives = MEMORY_LZOHTABLE(allocator);

    add_native_fn_obj(default_natives, allocator, "exit", 1, native_fn_exit);
	add_native_fn_obj(default_natives, allocator, "assert", 1, native_fn_assert);
    add_native_fn_obj(default_natives, allocator, "assertm", 2, native_fn_assert);
    add_native_fn_obj(default_natives, allocator, "is_str_int", 1, native_fn_is_str_int);
    add_native_fn_obj(default_natives, allocator, "is_str_float", 1, native_fn_is_str_float);
    add_native_fn_obj(default_natives, allocator, "to_str", 1, native_fn_to_str);
    add_native_fn_obj(default_natives, allocator, "to_json", 1, native_fn_to_json);
    add_native_fn_obj(default_natives, allocator, "to_int", 1, native_fn_to_int);
    add_native_fn_obj(default_natives, allocator, "to_float", 1, native_fn_to_float);
    add_native_fn_obj(default_natives, allocator, "print", 1, native_fn_print);
    add_native_fn_obj(default_natives, allocator, "println", 1, native_fn_println);
    add_native_fn_obj(default_natives, allocator, "eprint", 1, native_fn_eprint);
    add_native_fn_obj(default_natives, allocator, "eprintln", 1, native_fn_eprintln);
    add_native_fn_obj(default_natives, allocator, "print_stack", 0, native_fn_print_stack);
    add_native_fn_obj(default_natives, allocator, "readln", 0, native_fn_readln);
    add_native_fn_obj(default_natives, allocator, "gc", 0, native_fn_gc);
    add_native_fn_obj(default_natives, allocator, "halt", 0, native_fn_halt);

    return default_natives;
}

static void print_size(size_t size){
    if(size < 1024){
        printf("%zu B", size);
        return;
    }

    size /= 1024;

    if(size < 1024){
        printf("%zu KiB", size);
        return;
    }

    size /= 1024;

    if(size < 1024){
        printf("%zu MiB", size);
        return;
    }

    size /= 1024;

    if(size < 1024){
        printf("%zu GiB", size);
        return;
    }
}

int main(int argc, const char *argv[]){
	int result = 0;
    Args args = {0};

	get_args(argc, argv, &args);

	char *source_pathname = args.source_pathname;

	if(!source_pathname){
		print_help();
	}

	if(!utils_files_can_read(source_pathname)){
		fprintf(stderr, "File at '%s' do not exists or cannot be read\n", source_pathname);
		exit(EXIT_FAILURE);
	}

	if(!utils_files_is_regular(source_pathname)){
		fprintf(stderr, "File at '%s' is not a regular file\n", source_pathname);
		exit(EXIT_FAILURE);
	}

    init_memory();

    DStr *source = utils_read_source(source_pathname, &ctallocator);
    DStr *main_search_pathname = create_main_search_pathname(&ctallocator, source_pathname);
    DynArr *search_pathnames = parse_search_paths(&ctallocator, main_search_pathname, args.search_paths);
    char *module_path = memory_clone_cstr(&ctallocator, source_pathname, NULL);
    LZOHTable *keywords = create_keywords_table(&ctallocator);

    LZOHTable *default_native = create_default_native_fns(&rtallocator);
    ScopeManager *manager = scope_manager_create(&ctallocator);
    LZOHTable *modules = MEMORY_LZOHTABLE(&ctallocator);
	DynArr *tokens = MEMORY_DYNARR_PTR(&ctallocator);
    DynArr *fns_prototypes = MEMORY_DYNARR_PTR(&ctallocator);
	DynArr *stmts = MEMORY_DYNARR_PTR(&ctallocator);
	Lexer *lexer = lexer_create(&ctallocator, &rtallocator);
	Parser *parser = parser_create(&ctallocator);
    Compiler *compiler = compiler_create(&ctallocator, &rtallocator);
    Dumpper *dumpper = dumpper_create(&ctallocator);

    Module *main_module = NULL;
    VM *vm = vm_create(&rtallocator);

    switch (args.exclusives){
        case ARGS_LEX:{
            if(lexer_scan(source, tokens, keywords, module_path, lexer)){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            break;
        }case ARGS_PARSE:{
            if(lexer_scan(source, tokens, keywords, module_path, lexer)){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            if(parser_parse(tokens, fns_prototypes, stmts, parser)){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            break;
        }case ARGS_COMPILE:{
            if(lexer_scan(source, tokens, keywords, module_path, lexer)){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            if(parser_parse(tokens, fns_prototypes, stmts, parser)){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            main_module = compiler_compile(
                compiler,
                keywords,
                main_search_pathname,
                search_pathnames,
                default_native,
                manager,
                stmts,
                module_path
            );

            if(!main_module){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            break;
        }case ARGS_DUMP:{
            if(lexer_scan(source, tokens, keywords, module_path, lexer)){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            if(parser_parse(tokens, fns_prototypes, stmts, parser)){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            main_module = compiler_compile(
                compiler,
                keywords,
                main_search_pathname,
                search_pathnames,
                default_native,
                manager,
                stmts,
                module_path
            );

            if(!main_module){
                result = 1;
                goto CLEAN_UP_COMPTIME;
            }

            dumpper_dump(modules, main_module, dumpper);

            break;
        }default:{
            if(args.help){
                print_help();
            }else if(args.source_pathname){
                if(lexer_scan(source, tokens, keywords, module_path, lexer)){
                    result = 1;
                    goto CLEAN_UP_COMPTIME;
                }

                if(parser_parse(tokens, fns_prototypes, stmts, parser)){
                    result = 1;
                    goto CLEAN_UP_COMPTIME;
                }

                main_module = compiler_compile(
                    compiler,
                    keywords,
                    main_search_pathname,
                    search_pathnames,
                    default_native,
                    manager,
                    stmts,
                    module_path
                );

                if(!main_module){
                    result = 1;
                    goto CLEAN_UP_COMPTIME;
                }

                lzflist_destroy(ctflist);
                vm_initialize(vm);

                result = vm_execute(default_native, main_module, vm);

                goto CLEAN_UP_RUNTIME;
            }

            print_help();

            break;
        }
    }

CLEAN_UP_COMPTIME:
    lzflist_destroy(ctflist);
CLEAN_UP_RUNTIME:
    lzflist_destroy(rtflist);

    return result;
}
