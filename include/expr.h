#ifndef EXPR_H
#define EXPR_H

#include "token.h"
#include <stdint.h>

typedef enum expr_type{
    EMPTY_EXPR_TYPE,
	BOOL_EXPR_TYPE,
    INT_EXPR_TYPE,
    FLOAT_EXPR_TYPE,
	STRING_EXPR_TYPE,
    TEMPLATE_EXPR_TYPE,
    ANON_EXPR_TYPE,
    GROUP_EXPR_TYPE,
    IDENTIFIER_EXPR_TYPE,
    CALL_EXPR_TYPE,
	ACCESS_EXPR_TYPE,
    INDEX_EXPR_TYPE,
    UNARY_EXPR_TYPE,
	BINARY_EXPR_TYPE,
    CONCAT_EXPR_TYPE,
    MULSTR_EXPR_TYPE,
    BITWISE_EXPR_TYPE,
    COMPARISON_EXPR_TYPE,
    LOGICAL_EXPR_TYPE,
    ASSIGN_EXPR_TYPE,
	COMPOUND_EXPR_TYPE,
    ARRAY_EXPR_TYPE,
	LIST_EXPR_TYPE,
    DICT_EXPR_TYPE,
	RECORD_EXPR_TYPE,
	IS_EXPR_TYPE,
	TENARY_EXPR_TYPE
}ExprType;

typedef struct expr{
	ExprType type;
	void     *sub_expr;
}Expr;

typedef struct empty_expr{
    Token *empty_token;
}EmptyExpr;

typedef struct bool_expr{
    uint8_t value;
    Token   *bool_token;
}BoolExpr;

typedef struct int_expr{
	Token *token;
}IntExpr;

typedef struct float_expr{
	Token *token;
}FloatExpr;

typedef struct string_expr{
	Token *str_token;
}StrExpr;

typedef struct template_expr{
    Token  *template_token;
    DynArr *exprs; //expressions statements
}TemplateExpr;

typedef struct anon_expr{
    Token  *anon_token;
    DynArr *params;
    DynArr *stmts;
}AnonExpr;

typedef struct group_expr{
    Token *left_paren_token;
    Expr  *expr;
}GroupExpr;

typedef struct identifier_expr{
    Token *identifier_token;
}IdentifierExpr;

typedef struct call_expr{
    Expr   *left_expr;
    Token  *left_paren;
    DynArr *args;
}CallExpr;

typedef struct access_expr{
	Expr  *left_expr;
	Token *dot_token;
	Token *symbol_token;
}AccessExpr;

typedef struct index_expr{
    Expr  *target_expr;
    Token *left_square_token;
    Expr  *index_expr;
}IndexExpr;

typedef struct unary_expr{
    Token *operator_token;
    Expr  *right;
}UnaryExpr;

typedef struct binary_expr{
	Expr  *left;
	Token *operator;
	Expr  *right;
}BinaryExpr;

typedef struct concat_expr{
    Expr  *left;
	Token *operator_token;
	Expr  *right;
}ConcatExpr;

typedef struct mul_str_expr{
    Expr  *left;
	Token *operator_token;
	Expr  *right;
}MulStrExpr;

typedef struct bitwise_expr{
    Expr  *left;
	Token *operator_token;
	Expr  *right;
}BitWiseExpr;

typedef struct comparison_expr{
    Expr  *left;
	Token *operator_token;
	Expr  *right;
}ComparisonExpr;

typedef struct logical_expr{
	Expr  *left;
	Token *operator;
	Expr  *right;
}LogicalExpr;

typedef struct assign_expr{
	Expr  *left_expr;
	Token *equals_token;
    Expr  *value_expr;
}AssignExpr;

typedef struct compound_expr{
	Expr  *left_expr;
	Token *operator_token;
	Expr  *right_expr;
}CompoundExpr;

typedef struct array_expr{
    Token  *array_token;
    Expr   *len_expr;
    DynArr *values;
}ArrayExpr;

typedef struct list_expr{
	Token  *list_token;
	DynArr *exprs;
}ListExpr;

typedef struct dict_key_value{
    Expr *key;
    Expr *value;
}DictKeyValue;

typedef struct dict_expr{
    Token  *dict_token;
    DynArr *key_values;
}DictExpr;

typedef struct record_expr_value{
	Token *key;
	Expr  *value;
}RecordExprValue;

typedef struct record_expr{
	Token  *record_token;
	DynArr *key_values;
}RecordExpr;

typedef struct is_expr{
	Expr  *left_expr;
	Token *is_token;
	Token *type_token;
}IsExpr;

typedef struct tenary_expr{
    Expr  *condition;
    Expr  *left;
    Token *mark_token;
    Expr  *right;
}TenaryExpr;

#endif
