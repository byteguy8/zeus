#ifndef STMT_H
#define STMT_H

#include "essentials/dynarr.h"

#include "token.h"
#include "expr.h"

typedef enum stmt_type{
    EXPR_STMT_TYPE,
    VAR_DECL_STMT_TYPE,
    BLOCK_STMT_TYPE,
    IF_STMT_TYPE,
    STOP_STMT_TYPE,
    CONTINUE_STMT_TYPE,
    WHILE_STMT_TYPE,
    FOR_STMT_TYPE,
    FOR_RANGE_STMT_TYPE,
    THROW_STMT_TYPE,
    TRY_STMT_TYPE,
    RETURN_STMT_TYPE,
    PROC_STMT_TYPE,
    IMPORT_STMT_TYPE,
    EXPORT_STMT_TYPE,
}StmtType;

typedef struct stmt{
    StmtType type;
    void     *sub_stmt;
}Stmt;

typedef struct expr_stmt{
    Expr *expr;
}ExprStmt;

typedef struct block_stmt{
	Token  *left_bracket_token;
    DynArr *stmts;
}BlockStmt;

typedef struct if_stmt_branch{
    Token  *branch_token;
    Expr   *condition_expr;
    DynArr *stmts;
}IfStmtBranch;

typedef struct if_stmt{
    IfStmtBranch *if_branch;
    DynArr       *elif_branches;
	DynArr       *else_stmts;
}IfStmt;

typedef struct stop_stmt{
	Token *stop_token;
}StopStmt;

typedef struct continue_stmt{
    Token *continue_token;
}ContinueStmt;

typedef struct while_stmt{
    Token  *while_token;
	Expr   *condition_expr;
	DynArr *stmts;
}WhileStmt;

typedef struct for_stmt{
    Token  *for_token;
    DynArr *initializations;
    Expr   *condition_expr;
    DynArr *update_exprs;
    DynArr *stmts;
}ForStmt;

typedef struct for_range_stmt{
    Token  *for_token;
    Token  *symbol_token;
    Expr   *left_expr;
    Token  *for_type_token;
    Expr   *right_expr;
    DynArr *stmts;
}ForRangeStmt;

typedef struct throw_stmt{
    Token *throw_token;
	Expr  *value_expr;
}ThrowStmt;

typedef struct try_stmt{
    Token  *try_token;
    DynArr *try_stmts;
	Token  *catch_token;
	Token  *err_identifier;
	DynArr *catch_stmts;
}TryStmt;

typedef struct return_stmt{
    Token *return_token;
    Expr  *ret_expr;
}ReturnStmt;

typedef struct var_decl_stmt{
    char  is_mutable;
    char  is_initialized;
    Token *identifier_token;
    Expr  *initial_value_expr;
}VarDeclStmt;

typedef struct proc_prototype{
    Token   *identifier_token;
    uint8_t arity;
}ProcPrototype;

typedef struct function_stmt{
    Token  *identifier;
    DynArr *params;
    DynArr *stmts;
}ProcStmt;

typedef struct import_stmt{
    Token  *import_token;
    DynArr *names;
    Token  *alt_name;
}ImportStmt;

typedef struct export_stmt{
    Token  *export_token;
    DynArr *symbols;
}ExportStmt;

#endif
