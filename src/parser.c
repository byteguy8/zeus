#include "parser.h"

#include "essentials/memory.h"

#include "token.h"
#include "stmt.h"
#include "expr.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>
#include <assert.h>

#define CTALLOCATOR (parser->ctallocator)
//--------------------------------------------------------------------------//
//                            PRIVATE INTERFACE                             //
//--------------------------------------------------------------------------//
//--------------------------------  ERROR  ---------------------------------//
static void error(Parser *parser, Token *token, char *msg, ...);
//--------------------------------  OTHER  ---------------------------------//
static Expr *create_expr(ExprType type, void *sub_expr, Parser *parser);
static Stmt *create_stmt(StmtType type, void *sub_stmt, Parser *parser);
static Token *peek(Parser *parser);
static Token *previous(Parser *parser);
static int is_at_end(Parser *parser);
static int match(Parser *parser, int count, ...);
static int check(Parser *parser, TokType type);
static Token *consume(Parser *parser, TokType type, char *err_msg, ...);
static DynArr *record_key_values(Token *record_token, Parser *parser);
//------------------------------  EXPRESSION  ------------------------------//
static Expr *parse_expr(Parser *paser);
static Expr *parse_assign(Parser *parser);
static Expr *parse_is_expr(Parser *parser);
static Expr *parse_tenary_expr(Parser *parser);
static Expr *parse_or(Parser *parser);
static Expr *parse_and(Parser *parser);
static Expr *parse_bitwise_or(Parser *parser);
static Expr *parse_bitwise_xor(Parser *parser);
static Expr *parse_bitwise_and(Parser *parser);
static Expr *parse_equality(Parser *parser);
static Expr *parse_relational(Parser *parser);
static Expr *parse_shift(Parser *parser);
static Expr *parse_concat(Parser *parser);
static Expr *parse_mulstr(Parser *parser);
static Expr *parse_term(Parser *parser);
static Expr *parse_factor(Parser *parser);
static Expr *parse_unary(Parser *parser);
static Expr *parse_call(Parser *parser);
static Expr *parse_record(Parser *parser, Token *type_token);
static Expr *parse_dict(Parser *parser, Token *type_token);
static Expr *parse_list(Parser *parser, Token *type_token);
static Expr *parse_array(Parser *parser, Token *type_token);
static Expr *parse_types(Parser *parser);
static Expr *parse_literal(Parser *parser);
//------------------------------  STATEMENT  -------------------------------//
static Stmt *parse_stmt(Parser *parser);
static Stmt *parse_expr_stmt(Parser *parser);
static DynArr *parse_block_stmt(Parser *parser);
static Stmt *parse_if_stmt(Parser *parser);
static Stmt *parse_while_stmt(Parser *parser);
static Stmt *parse_for_stmt(Parser *parser);
static Stmt *parse_throw_stmt(Parser *parser);
static Stmt *parse_try_stmt(Parser *parser);
static Stmt *parse_return_stmt(Parser *parser);
static Stmt *parse_var_decl_stmt(Parser *parser);
static Stmt *parse_function_stmt(Parser *parser);
static Stmt *parse_import_stmt(Parser *parser);
static Stmt *parse_export_stmt(Parser *parser);
//--------------------------------------------------------------------------//
//                          PRIVATE IMPLEMENTATION                          //
//--------------------------------------------------------------------------//
void error(Parser *parser, Token *token, char *msg, ...){
    va_list args;
	va_start(args, msg);

	fprintf(stderr, "ERROR at line %d in file '%s':\n\t", token->line, token->pathname);
	vfprintf(stderr, msg, args);
    fprintf(stderr, "\n");

	va_end(args);

    longjmp(parser->err_buf, 1);
}

inline Expr *create_expr(ExprType type, void *sub_expr, Parser *parser){
    Expr *expr = MEMORY_ALLOC(CTALLOCATOR, Expr, 1);

    *expr = (Expr){
        .type = type,
        .sub_expr = sub_expr
    };

    return expr;
}

inline Stmt *create_stmt(StmtType type, void *sub_stmt, Parser *parser){
    Stmt *stmt = MEMORY_ALLOC(CTALLOCATOR, Stmt, 1);

    *stmt = (Stmt){
        .type = type,
        .sub_stmt = sub_stmt
    };

    return stmt;
}

inline Token *peek(Parser *parser){
	DynArr *tokens = parser->tokens;

    return DYNARR_GET_PTR_AS(tokens, Token, parser->current);
}

inline Token *previous(Parser *parser){
	DynArr *tokens = parser->tokens;

    return DYNARR_GET_PTR_AS(tokens, Token, parser->current - 1);
}

inline int is_at_end(Parser *parser){
    Token *token = peek(parser);

    return token->type == EOF_TOKTYPE;
}

int match(Parser *parser, int count, ...){
	Token *token = peek(parser);

	va_list args;
	va_start(args, count);

	for(int i = 0; i < count; i++){
		TokType type = va_arg(args, TokType);

		if(token->type == type){
			parser->current++;

            va_end(args);

            return 1;
		}
	}

	va_end(args);

    return 0;
}

int check(Parser *parser, TokType type){
    Token *token = peek(parser);

    return token->type == type;
}

Token *consume(Parser *parser, TokType type, char *err_msg, ...){
    Token *token = peek(parser);

    if(token->type == type){
        parser->current++;

        return token;
    }

    va_list args;
	va_start(args, err_msg);

	fprintf(stderr, "Parser error at line %d in file '%s':\n\t", token->line, token->pathname);
	vfprintf(stderr, err_msg, args);
    fprintf(stderr, "\n");

	va_end(args);

    longjmp(parser->err_buf, 1);

    return NULL;
}

DynArr *record_key_values(Token *record_token, Parser *parser){
	DynArr *key_values = MEMORY_DYNARR_PTR(CTALLOCATOR);

	do{
		if(dynarr_len(key_values) >= 255){
            error(parser, record_token, "Record expressions only accept up to %d values", 255);
        }

		Token *key = consume(parser, IDENTIFIER_TOKTYPE, "Expect record key");

        consume(parser, COLON_TOKTYPE, "Expect ':' after record key");

        Expr *value = parse_expr(parser);
		RecordExprValue *key_value = MEMORY_ALLOC(CTALLOCATOR, RecordExprValue, 1);

        *key_value = (RecordExprValue){
            .key = key,
		    .value = value
        };

        dynarr_insert_ptr(key_values, key_value);
	}while(match(parser, 1, COMMA_TOKTYPE));

	return key_values;
}

Expr *parse_expr(Parser *parser){
	return parse_assign(parser);
}

Expr *parse_assign(Parser *parser){
    Expr *expr = parse_tenary_expr(parser);

	if(match(parser, 4,
		COMPOUND_ADD_TOKTYPE,
		COMPOUND_SUB_TOKTYPE,
		COMPOUND_MUL_TOKTYPE,
		COMPOUND_DIV_TOKTYPE
    )){
		Token *operator = previous(parser);
		Expr *right = parse_assign(parser);
		CompoundExpr *compound_expr = MEMORY_ALLOC(CTALLOCATOR, CompoundExpr, 1);

        *compound_expr = (CompoundExpr){
            .left_expr = expr,
		    .operator_token = operator,
		    .right_expr = right
        };

		return create_expr(COMPOUND_EXPR_TYPE, compound_expr, parser);
	}

    if(match(parser, 1, EQUALS_TOKTYPE)){
		Token *equals_token = previous(parser);
        Expr *value_expr = parse_assign(parser);
        AssignExpr *assign_expr = MEMORY_ALLOC(CTALLOCATOR, AssignExpr, 1);

        *assign_expr = (AssignExpr){
            .left_expr = expr,
		    .equals_token = equals_token,
            .value_expr = value_expr
        };

        return create_expr(ASSIGN_EXPR_TYPE, assign_expr, parser);
    }

    return expr;
}

Expr *parse_tenary_expr(Parser *parser){
    Expr *condition = parse_is_expr(parser);

    if(match(parser, 1, QUESTION_MARK_TOKTYPE)){
        Token *mark_token = previous(parser);
        Expr *left = parse_tenary_expr(parser);

        consume(parser, COLON_TOKTYPE, "Expect ':' after left side expression");

        Expr *right = parse_tenary_expr(parser);
        TenaryExpr *tenary_expr = MEMORY_ALLOC(CTALLOCATOR, TenaryExpr, 1);

        *tenary_expr = (TenaryExpr){
            .condition = condition,
            .left = left,
            .mark_token = mark_token,
            .right = right
        };

        return create_expr(TENARY_EXPR_TYPE, tenary_expr, parser);
    }

    return condition;
}

Expr *parse_is_expr(Parser *parser){
	Expr *left = parse_or(parser);

	if(match(parser, 1, IS_TOKTYPE)){
		Token *is_token = NULL;
		Token *type_token = NULL;

		is_token = previous(parser);

		if(match(parser, 10,
			EMPTY_TOKTYPE,
			BOOL_TOKTYPE,
			INT_TOKTYPE,
			FLOAT_TOKTYPE,
			STR_TOKTYPE,
            ARRAY_TOKTYPE,
			LIST_TOKTYPE,
			DICT_TOKTYPE,
            RECORD_TOKTYPE,
            PROC_TOKTYPE
        )){
			type_token = previous(parser);
		}

		if(!type_token){
            error(parser, is_token, "Expect type after 'is' keyword");
        }

		IsExpr *is_expr = MEMORY_ALLOC(CTALLOCATOR, IsExpr, 1);

        *is_expr = (IsExpr){
            .left_expr = left,
		    .is_token = is_token,
		    .type_token = type_token
        };

		return create_expr(IS_EXPR_TYPE, is_expr, parser);
	}

	return left;
}

Expr *parse_or(Parser *parser){
    Expr *left = parse_and(parser);

	while(match(parser, 1, OR_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_and(parser);
		LogicalExpr *logical_expr = MEMORY_ALLOC(CTALLOCATOR, LogicalExpr, 1);

        *logical_expr = (LogicalExpr){
            logical_expr->left = left,
            logical_expr->operator = operator,
            logical_expr->right = right
        };

		left = create_expr(LOGICAL_EXPR_TYPE, logical_expr, parser);
	}

    return left;
}

Expr *parse_and(Parser *parser){
    Expr *left = parse_bitwise_or(parser);

	while(match(parser, 1, AND_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_bitwise_or(parser);
		LogicalExpr *logical_expr = MEMORY_ALLOC(CTALLOCATOR, LogicalExpr, 1);

        *logical_expr = (LogicalExpr){
            .left = left,
		    .operator = operator,
		    .right = right
        };

		left = create_expr(LOGICAL_EXPR_TYPE, logical_expr, parser);
	}

    return left;
}

Expr *parse_bitwise_or(Parser *parser){
    Expr *left = parse_bitwise_xor(parser);

	while(match(parser, 1, OR_BITWISE_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_bitwise_xor(parser);
		BitWiseExpr *bitwise_expr = MEMORY_ALLOC(CTALLOCATOR, BitWiseExpr, 1);

        *bitwise_expr = (BitWiseExpr){
            .left = left,
            .operator_token = operator,
            .right = right
        };

		left = create_expr(BITWISE_EXPR_TYPE, bitwise_expr, parser);
	}

    return left;
}

Expr *parse_bitwise_xor(Parser *parser){
    Expr *left = parse_bitwise_and(parser);

	while(match(parser, 1, XOR_BITWISE_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_bitwise_and(parser);
		BitWiseExpr *bitwise_expr = MEMORY_ALLOC(CTALLOCATOR, BitWiseExpr, 1);

        *bitwise_expr = (BitWiseExpr){
            .left = left,
		    .operator_token = operator,
		    .right = right
        };

		left = create_expr(BITWISE_EXPR_TYPE, bitwise_expr, parser);
	}

    return left;
}

Expr *parse_bitwise_and(Parser *parser){
    Expr *left = parse_equality(parser);

	while(match(parser, 1, AND_BITWISE_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_equality(parser);
		BitWiseExpr *bitwise_expr = MEMORY_ALLOC(CTALLOCATOR, BitWiseExpr, 1);

        *bitwise_expr = (BitWiseExpr){
            .left = left,
		    .operator_token = operator,
		    .right = right
        };

		left = create_expr(BITWISE_EXPR_TYPE, bitwise_expr, parser);
	}

    return left;
}

Expr *parse_equality(Parser *parser){
    Expr *left = parse_relational(parser);

	while(match(parser, 2, EQUALS_EQUALS_TOKTYPE, NOT_EQUALS_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_relational(parser);
		ComparisonExpr *equality_expr = MEMORY_ALLOC(CTALLOCATOR, ComparisonExpr, 1);

        *equality_expr = (ComparisonExpr){
            .left = left,
		    .operator_token = operator,
		    .right = right
        };

		left = create_expr(COMPARISON_EXPR_TYPE, equality_expr, parser);
	}

    return left;
}

Expr *parse_relational(Parser *parser){
    Expr *left = parse_shift(parser);

	while(match(parser, 4,
        LESS_TOKTYPE,
        GREATER_TOKTYPE,
        LESS_EQUALS_TOKTYPE,
        GREATER_EQUALS_TOKTYPE
    )){
		Token *operator = previous(parser);
		Expr *right = parse_shift(parser);
		ComparisonExpr *relational_expr = MEMORY_ALLOC(CTALLOCATOR, ComparisonExpr, 1);

        *relational_expr = (ComparisonExpr){
            .left = left,
		    .operator_token = operator,
		    .right = right
        };

		left = create_expr(COMPARISON_EXPR_TYPE, relational_expr, parser);
	}

    return left;
}

Expr *parse_shift(Parser *parser){
    Expr *left = parse_concat(parser);

	while(match(parser, 2, LEFT_SHIFT_TOKTYPE, RIGHT_SHIFT_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_concat(parser);
		BitWiseExpr *bitwise_expr = MEMORY_ALLOC(CTALLOCATOR, BitWiseExpr, 1);

        *bitwise_expr = (BitWiseExpr){
            .left = left,
		    .operator_token = operator,
		    .right = right
        };

		left = create_expr(BITWISE_EXPR_TYPE, bitwise_expr, parser);
	}

	return left;
}

Expr *parse_concat(Parser *parser){
    Expr *left = parse_mulstr(parser);

	while(match(parser, 1, DOUBLE_DOT_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_mulstr(parser);
		ConcatExpr *concat_expr = MEMORY_ALLOC(CTALLOCATOR, ConcatExpr, 1);

        *concat_expr = (ConcatExpr){
            .left = left,
		    .operator_token = operator,
		    .right = right
        };

		left = create_expr(CONCAT_EXPR_TYPE, concat_expr, parser);
	}

	return left;
}

Expr *parse_mulstr(Parser *parser){
    Expr *left = parse_term(parser);

	while(match(parser, 1, DOUBLE_ASTERISK_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_term(parser);
		MulStrExpr *mul_str_expr = MEMORY_ALLOC(CTALLOCATOR, MulStrExpr, 1);

        *mul_str_expr = (MulStrExpr){
            .left = left,
		    .operator_token = operator,
		    .right = right
        };

		left = create_expr(MULSTR_EXPR_TYPE, mul_str_expr, parser);
	}

	return left;
}

Expr *parse_term(Parser *parser){
	Expr *left = parse_factor(parser);

	while(match(parser, 2, PLUS_TOKTYPE, MINUS_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_factor(parser);
		BinaryExpr *binary_expr = MEMORY_ALLOC(CTALLOCATOR, BinaryExpr, 1);

        *binary_expr = (BinaryExpr){
            .left = left,
		    .operator = operator,
		    .right = right
        };

		left = create_expr(BINARY_EXPR_TYPE, binary_expr, parser);
	}

	return left;
}

Expr *parse_factor(Parser *parser){
	Expr *left = parse_unary(parser);

	while(match(parser, 3, ASTERISK_TOKTYPE, SLASH_TOKTYPE, MOD_TOKTYPE)){
		Token *operator = previous(parser);
		Expr *right = parse_unary(parser);
		BinaryExpr *binary_expr = MEMORY_ALLOC(CTALLOCATOR, BinaryExpr, 1);

        *binary_expr = (BinaryExpr){
            .left = left,
		    .operator = operator,
		    .right = right
        };

		left = create_expr(BINARY_EXPR_TYPE, binary_expr, parser);
	}

	return left;
}

Expr *parse_unary(Parser *parser){
    if(match(parser, 3, MINUS_TOKTYPE, EXCLAMATION_TOKTYPE, NOT_BITWISE_TOKTYPE)){
        Token *operator = previous(parser);
        Expr *right = parse_unary(parser);
        UnaryExpr *unary_expr = MEMORY_ALLOC(CTALLOCATOR, UnaryExpr, 1);

        *unary_expr = (UnaryExpr){
            .operator_token = operator,
            .right = right
        };

        return create_expr(UNARY_EXPR_TYPE, unary_expr, parser);
    }

    return parse_call(parser);
}

Expr *parse_call(Parser *parser){
    Expr *left = parse_types(parser);

    if(check(parser, DOT_TOKTYPE) || check(parser, LEFT_PAREN_TOKTYPE) || check(parser, LEFT_SQUARE_TOKTYPE)){
        while (match(parser, 3, DOT_TOKTYPE, LEFT_PAREN_TOKTYPE, LEFT_SQUARE_TOKTYPE)){
            Token *token = previous(parser);

            switch (token->type){
                case DOT_TOKTYPE:{
                    Token *symbol_token = consume(parser, IDENTIFIER_TOKTYPE, "Expect identifier");
                    AccessExpr *access_expr = MEMORY_ALLOC(CTALLOCATOR, AccessExpr, 1);

                    *access_expr = (AccessExpr){
                        .left_expr = left,
                        .dot_token = token,
                        .symbol_token = symbol_token
                    };

                    left = create_expr(ACCESS_EXPR_TYPE, access_expr, parser);

                    break;
                }case LEFT_PAREN_TOKTYPE:{
                    DynArr *args = NULL;

                    if(!check(parser, RIGHT_PAREN_TOKTYPE)){
                        args = MEMORY_DYNARR_PTR(CTALLOCATOR);

                        do{
                            Expr *expr = parse_expr(parser);

                            dynarr_insert_ptr(args, expr);
                        }while(match(parser, 1, COMMA_TOKTYPE));
                    }

                    consume(parser, RIGHT_PAREN_TOKTYPE, "Expect ')' after call arguments");

                    CallExpr *call_expr = MEMORY_ALLOC(CTALLOCATOR, CallExpr, 1);

                    *call_expr = (CallExpr){
                        .left_expr = left,
                        .left_paren = token,
                        .args = args
                    };

                    left = create_expr(CALL_EXPR_TYPE, call_expr, parser);

                    break;
                }case LEFT_SQUARE_TOKTYPE:{
                    Expr *index = parse_expr(parser);

                    consume(parser, RIGHT_SQUARE_TOKTYPE, "Expect ']' after index expression");

                    IndexExpr *index_expr = MEMORY_ALLOC(CTALLOCATOR, IndexExpr, 1);

                    *index_expr = (IndexExpr){
                        .target_expr = left,
                        .left_square_token = token,
                        .index_expr = index
                    };

                    left = create_expr(INDEX_EXPR_TYPE, index_expr, parser);

                    break;
                }default:{
                    assert("Illegal token type");

                    break;
                }
            }
        }
    }

    return left;
}

Expr *parse_record(Parser *parser, Token *type_token){
    DynArr *key_values = NULL;

    consume(parser, LEFT_BRACKET_TOKTYPE, "Expect '{' after 'record' keyword at start of record body");

    if(!check(parser, RIGHT_BRACKET_TOKTYPE)){
        key_values = record_key_values(type_token, parser);
    }

    consume(parser, RIGHT_BRACKET_TOKTYPE, "Expect '}' at end of record body");

    RecordExpr *record_expr = MEMORY_ALLOC(CTALLOCATOR, RecordExpr, 1);

    *record_expr = (RecordExpr){
        .record_token = type_token,
        .key_values = key_values
    };

    return create_expr(RECORD_EXPR_TYPE, record_expr, parser);
}

Expr *parse_dict(Parser *parser, Token *type_token){
    DynArr *key_values = NULL;

    consume(parser, LEFT_PAREN_TOKTYPE, "Expect '(' after 'dict' keyword");

    if(!check(parser, RIGHT_PAREN_TOKTYPE)){
        key_values = MEMORY_DYNARR_PTR(CTALLOCATOR);

        do{
            if(dynarr_len(key_values) >= INT16_MAX){
                error(parser, type_token, "Dict expressions only accept up to %d values", INT16_MAX);
            }

            Expr *key = parse_expr(parser);

            consume(parser, TO_TOKTYPE, "Expect 'to' after keyword");

            Expr *value = parse_expr(parser);
            DictKeyValue *key_value = MEMORY_ALLOC(CTALLOCATOR, DictKeyValue, 1);

            *key_value = (DictKeyValue){
                .key = key,
                .value = value
            };

            dynarr_insert_ptr(key_values, key_value);
        }while(match(parser, 1, COMMA_TOKTYPE));
    }

    consume(parser, RIGHT_PAREN_TOKTYPE, "Expect ')' at end of list expression");

    DictExpr *dict_expr = MEMORY_ALLOC(CTALLOCATOR, DictExpr, 1);

    *dict_expr = (DictExpr){
        .dict_token = type_token,
        .key_values = key_values
    };

    return create_expr(DICT_EXPR_TYPE, dict_expr, parser);
}

Expr *parse_list(Parser *parser, Token *type_token){
	DynArr *exprs = NULL;

    consume(parser, LEFT_PAREN_TOKTYPE, "Expect '(' after 'list' keyword");

    if(!check(parser, RIGHT_PAREN_TOKTYPE)){
        exprs = MEMORY_DYNARR_PTR(CTALLOCATOR);

        do{
            if(dynarr_len(exprs) >= INT16_MAX){
                error(parser, type_token, "List expressions only accept up to %d values", INT16_MAX);
            }

            Expr *expr = parse_expr(parser);

            dynarr_insert_ptr(exprs, expr);
        }while(match(parser, 1, COMMA_TOKTYPE));
    }

    consume(parser, RIGHT_PAREN_TOKTYPE, "Expect ')' at end of list expression");

    ListExpr *list_expr = MEMORY_ALLOC(CTALLOCATOR, ListExpr, 1);

    *list_expr = (ListExpr){
        .list_token = type_token,
        .exprs = exprs
    };

    return create_expr(LIST_EXPR_TYPE, list_expr, parser);
}

Expr *parse_array(Parser *parser, Token *type_token){
    Expr *len_expr = NULL;
    DynArr *values = NULL;

    if(match(parser, 1, LEFT_SQUARE_TOKTYPE)){
        len_expr = parse_expr(parser);

        consume(parser, RIGHT_SQUARE_TOKTYPE, "Expect ']' after array length expression");
    }else{
        consume(parser, LEFT_PAREN_TOKTYPE, "Expect '(' after 'array' keyword");

        if(!check(parser, RIGHT_PAREN_TOKTYPE)){
            values = MEMORY_DYNARR_PTR(CTALLOCATOR);

            do{
                if(dynarr_len(values) >= INT32_MAX){
                    error(parser, type_token, "Array expressions only accept up to %d values", INT16_MAX);
                }

                Expr *expr = parse_expr(parser);

                dynarr_insert_ptr(values, expr);
            }while(match(parser, 1, COMMA_TOKTYPE));
        }

        consume(parser, RIGHT_PAREN_TOKTYPE, "Expect ')' at end of array elements");
    }

    ArrayExpr *array_expr = MEMORY_ALLOC(CTALLOCATOR, ArrayExpr, 1);

    *array_expr = (ArrayExpr){
        .array_token = type_token,
        .len_expr = len_expr,
        .values = values
    };

    return create_expr(ARRAY_EXPR_TYPE, array_expr, parser);
}

Expr *parse_types(Parser *parser){
    if(match(parser, 4, RECORD_TOKTYPE, DICT_TOKTYPE, LIST_TOKTYPE, ARRAY_TOKTYPE)){
        Token *type_token = previous(parser);

        switch (type_token->type){
            case RECORD_TOKTYPE:{
                return parse_record(parser, type_token);
            }case DICT_TOKTYPE:{
                return parse_dict(parser, type_token);
            }case LIST_TOKTYPE:{
                return parse_list(parser, type_token);
            }case ARRAY_TOKTYPE:{
                return parse_array(parser, type_token);
            }default:{
                assert(0 && "Illegal token type");

                break;
            }
        }
    }

    return parse_literal(parser);
}

Expr *parse_literal(Parser *parser){
    if(match(parser, 1, EMPTY_TOKTYPE)){
        Token *empty_token = previous(parser);
        EmptyExpr *empty_expr = MEMORY_ALLOC(CTALLOCATOR, EmptyExpr, 1);

        empty_expr->empty_token = empty_token;

        return create_expr(EMPTY_EXPR_TYPE, empty_expr, parser);
    }

	if(match(parser, 1, FALSE_TOKTYPE)){
        Token *bool_token = previous(parser);
        BoolExpr *bool_expr = MEMORY_ALLOC(CTALLOCATOR, BoolExpr, 1);

        bool_expr->value = 0;
        bool_expr->bool_token = bool_token;

        return create_expr(BOOL_EXPR_TYPE, bool_expr, parser);
    }

    if(match(parser, 1, TRUE_TOKTYPE)){
        Token *bool_token = previous(parser);
        BoolExpr *bool_expr = MEMORY_ALLOC(CTALLOCATOR, BoolExpr, 1);

        bool_expr->value = 1;
        bool_expr->bool_token = bool_token;

        return create_expr(BOOL_EXPR_TYPE, bool_expr, parser);
    }

	if(match(parser, 1, INT_TYPE_TOKTYPE)){
		Token *int_token = previous(parser);
        IntExpr *int_expr = MEMORY_ALLOC(CTALLOCATOR, IntExpr, 1);

		int_expr->token = int_token;

        return create_expr(INT_EXPR_TYPE, int_expr, parser);
	}

	if(match(parser, 1, FLOAT_TYPE_TOKTYPE)){
		Token *float_token = previous(parser);
        FloatExpr *float_expr = MEMORY_ALLOC(CTALLOCATOR, FloatExpr, 1);

		float_expr->token = float_token;

        return create_expr(FLOAT_EXPR_TYPE, float_expr, parser);
	}

	if(match(parser, 1, STR_TYPE_TOKTYPE)){
		Token *string_token = previous(parser);
		StrExpr *string_expr = MEMORY_ALLOC(CTALLOCATOR, StrExpr, 1);

		string_expr->str_token = string_token;

		return create_expr(STRING_EXPR_TYPE, string_expr, parser);
	}

    if(match(parser, 1, TEMPLATE_TYPE_TOKTYPE)){
        Token *template_token = previous(parser);
        DynArr *tokens = (DynArr *)template_token->literal;
        DynArr *exprs = MEMORY_DYNARR_PTR(CTALLOCATOR);
        Parser *template_parser = parser_create(CTALLOCATOR);

        if(parser_parse_str_interpolation(tokens, exprs, template_parser)){
            error(parser, template_token, "Failed to parse string interpolation");
        }

        TemplateExpr *template_expr = MEMORY_ALLOC(CTALLOCATOR, TemplateExpr, 1);

        *template_expr = (TemplateExpr){
            .template_token = template_token,
            .exprs = exprs
        };

        return create_expr(TEMPLATE_EXPR_TYPE, template_expr, parser);
    }

    if(match(parser, 1, ANON_TOKTYPE)){
        Token *anon_token = NULL;
        DynArr *params = NULL;
        DynArr *stmts = NULL;

        anon_token = previous(parser);

        if(match(parser, 1, LEFT_PAREN_TOKTYPE)){
            if(!check(parser, RIGHT_PAREN_TOKTYPE)){
                params = MEMORY_DYNARR_PTR(CTALLOCATOR);

                do{
                    unsigned char is_mutable = match(parser, 1, MUT_TOKTYPE);
                    Token *identifier = consume(parser, IDENTIFIER_TOKTYPE, "Expect parameter identifier");
                    ProcParam *param = MEMORY_ALLOC(CTALLOCATOR, ProcParam, 1);

                    *param = (ProcParam){
                        .is_mutable = is_mutable,
                        .identifier = identifier
                    };

                    dynarr_insert_ptr(params, param);
                } while (match(parser, 1, COMMA_TOKTYPE));
            }

            consume(parser, RIGHT_PAREN_TOKTYPE, "Expect ')' at end of function parameters");
        }

        consume(parser, LEFT_BRACKET_TOKTYPE, "Expect '{' at start of function body");

        stmts = parse_block_stmt(parser);

        AnonExpr *anon_expr = MEMORY_ALLOC(CTALLOCATOR, AnonExpr, 1);

        *anon_expr = (AnonExpr){
            .anon_token = anon_token,
            .params = params,
            .stmts = stmts
        };

        return create_expr(ANON_EXPR_TYPE, anon_expr, parser);
    }

    if(match(parser, 1, LEFT_PAREN_TOKTYPE)){
        Token *left_paren_token = previous(parser);
        Expr *group_sub_expr = parse_expr(parser);

        consume(parser, RIGHT_PAREN_TOKTYPE, "Expect ')' after expression in group expression");

        GroupExpr *group_expr = MEMORY_ALLOC(CTALLOCATOR, GroupExpr, 1);

        *group_expr = (GroupExpr){
            .left_paren_token = left_paren_token,
            .expr = group_sub_expr
        };

        return create_expr(GROUP_EXPR_TYPE, group_expr, parser);
    }

    if(match(parser, 1, IDENTIFIER_TOKTYPE)){
        Token *identifier_token = previous(parser);
        IdentifierExpr *identifier_expr = MEMORY_ALLOC(CTALLOCATOR, IdentifierExpr, 1);

        identifier_expr->identifier_token = identifier_token;

        return create_expr(IDENTIFIER_EXPR_TYPE, identifier_expr,parser);
    }

	Token *current_token = peek(parser);

    error(
        parser,
        current_token,
        "Expect something: 'false', 'true' or integer, but got '%s'",
        current_token->lexeme
    );

    return NULL;
}

Stmt *parse_stmt(Parser *parser){
    if(match(parser, 1, LET_TOKTYPE)){
        return parse_var_decl_stmt(parser);
    }

    if(match(parser, 1, LEFT_BRACKET_TOKTYPE)){
    	Token *left_bracket_token = previous(parser);
        DynArr *stmts = parse_block_stmt(parser);
        BlockStmt *block_stmt = MEMORY_ALLOC(CTALLOCATOR, BlockStmt, 1);

        block_stmt->left_bracket_token = left_bracket_token;
        block_stmt->stmts = stmts;

        return create_stmt(BLOCK_STMT_TYPE, block_stmt, parser);
    }

	if(match(parser, 1, IF_TOKTYPE)){
        return parse_if_stmt(parser);
    }

	if(match(parser, 1, WHILE_TOKTYPE)){
        return parse_while_stmt(parser);
    }

    if(match(parser, 1, FOR_TOKTYPE)){
        return parse_for_stmt(parser);
    }

	if(match(parser, 1, STOP_TOKTYPE)){
		Token *stop_token = previous(parser);
		consume(parser, SEMICOLON_TOKTYPE, "Expect ';' at end of 'stop' statement");

		StopStmt *stop_stmt = MEMORY_ALLOC(CTALLOCATOR, StopStmt, 1);

		stop_stmt->stop_token = stop_token;

		return create_stmt(STOP_STMT_TYPE, stop_stmt, parser);
	}

    if(match(parser, 1, CONTINUE_TOKTYPE)){
        Token *continue_token = previous(parser);
        consume(parser, SEMICOLON_TOKTYPE, "Expect ';' at end of 'continue' statement");

        ContinueStmt *continue_stmt = MEMORY_ALLOC(CTALLOCATOR, ContinueStmt, 1);

        continue_stmt->continue_token = continue_token;

        return create_stmt(CONTINUE_STMT_TYPE, continue_stmt, parser);
    }

    if(match(parser, 1, RET_TOKTYPE)){
        return parse_return_stmt(parser);
    }

    if(match(parser, 1, PROC_TOKTYPE)){
        return parse_function_stmt(parser);
    }

    if(match(parser, 1, IMPORT_TOKTYPE)){
        return parse_import_stmt(parser);
    }

    if(match(parser, 1, EXPORT_TOKTYPE)){
        return parse_export_stmt(parser);
    }

    if(match(parser, 1, THROW_TOKTYPE)){
        return parse_throw_stmt(parser);
    }

    if(match(parser, 1, TRY_TOKTYPE)){
        return parse_try_stmt(parser);
    }

    return parse_expr_stmt(parser);
}

Stmt *parse_expr_stmt(Parser *parser){
    Expr *expr = parse_expr(parser);

    consume(parser, SEMICOLON_TOKTYPE, "Expect ';' at end of statement expression");

    ExprStmt *expr_stmt = MEMORY_ALLOC(CTALLOCATOR, ExprStmt, 1);
    Stmt *stmt = MEMORY_ALLOC(CTALLOCATOR, Stmt, 1);

    expr_stmt->expr = expr;

    *stmt = (Stmt){
        .type = EXPR_STMT_TYPE,
        .sub_stmt = expr_stmt
    };

    return stmt;
}

DynArr *parse_block_stmt(Parser *parser){
    DynArr *stmts = MEMORY_DYNARR_PTR(CTALLOCATOR);

    while (!check(parser, RIGHT_BRACKET_TOKTYPE)){
        Stmt *stmt = parse_stmt(parser);

        dynarr_insert_ptr(stmts, stmt);
    }

    consume(
    	parser,
     	RIGHT_BRACKET_TOKTYPE,
      	"Expect '}' at end of block statement"
    );

    return stmts;
}

IfStmtBranch *parse_if_stmt_branch(Token *branch_token, Parser *parser){
    Expr *condition = NULL;
    DynArr *stmts = NULL;

    consume(
    	parser,
     	LEFT_PAREN_TOKTYPE,
      	"Expect '(' after '%s' keyword",
       	branch_token->lexeme
    );

    condition = parse_is_expr(parser);

	consume(
		parser,
		RIGHT_PAREN_TOKTYPE,
		"Expect ')' at end of '%s' condition",
		branch_token->lexeme
	);

    if(match(parser, 1, COLON_TOKTYPE)){
		stmts = MEMORY_DYNARR_PTR(CTALLOCATOR);

        Stmt *unique_stmt = NULL;

		if(match(parser, 1, RET_TOKTYPE)){
			unique_stmt = parse_return_stmt(parser);
		}else{
			unique_stmt = parse_expr_stmt(parser);
		}

		dynarr_insert_ptr(stmts, unique_stmt);
	}else{
		consume(
			parser,
			LEFT_BRACKET_TOKTYPE,
			"Expect '{' at start of '%s' body",
			branch_token->lexeme
		);

		stmts = parse_block_stmt(parser);
	}

    IfStmtBranch *branch = MEMORY_ALLOC(CTALLOCATOR, IfStmtBranch, 1);

    *branch = (IfStmtBranch){
        .branch_token = branch_token,
        .condition_expr = condition,
        .stmts = stmts
    };

    return branch;
}

Stmt *parse_if_stmt(Parser *parser){
    IfStmtBranch *if_branch = NULL;
    DynArr *elif_branches = NULL;
	DynArr *else_stmts = NULL;
    Token *if_branch_token = previous(parser);

    if_branch = parse_if_stmt_branch(if_branch_token, parser);

    if(check(parser, ELIF_TOKTYPE)){
        elif_branches = MEMORY_DYNARR_PTR(CTALLOCATOR);

        while (match(parser, 1, ELIF_TOKTYPE)){
            Token *elif_branch_token = previous(parser);
            IfStmtBranch *branch = parse_if_stmt_branch(elif_branch_token, parser);

            dynarr_insert_ptr(elif_branches, branch);
        }
    }

	if(match(parser, 1, ELSE_TOKTYPE)){
		if(match(parser, 1, COLON_TOKTYPE)){
			else_stmts = MEMORY_DYNARR_PTR(CTALLOCATOR);

            Stmt *unique_stmt = NULL;

			if(match(parser, 1, RET_TOKTYPE)){
				unique_stmt = parse_return_stmt(parser);
			}else{
				unique_stmt = parse_expr_stmt(parser);
			}

            dynarr_insert_ptr(else_stmts, unique_stmt);
		}else{
			consume(
				parser,
				LEFT_BRACKET_TOKTYPE,
				"Expect '{' at start of else body"
			);

			else_stmts = parse_block_stmt(parser);
		}
	}

	IfStmt *if_stmt = MEMORY_ALLOC(CTALLOCATOR, IfStmt, 1);

    *if_stmt = (IfStmt){
        .if_branch = if_branch,
        .elif_branches = elif_branches,
        .else_stmts = else_stmts
    };

	return create_stmt(IF_STMT_TYPE, if_stmt, parser);
}

Stmt *parse_while_stmt(Parser *parser){
	Token *while_token = NULL;
    Expr *condition = NULL;
	DynArr *stmts = NULL;

	while_token = previous(parser);

    consume(parser, LEFT_PAREN_TOKTYPE, "Expect '(' after 'while' keyword");

    condition = parse_expr(parser);

    consume(parser, RIGHT_PAREN_TOKTYPE, "Expect ')' at end of while statement condition");
	consume(parser, LEFT_BRACKET_TOKTYPE, "Expect '{' at start of while statement body");

    stmts = parse_block_stmt(parser);

	WhileStmt *while_stmt = MEMORY_ALLOC(CTALLOCATOR, WhileStmt, 1);

    *while_stmt = (WhileStmt){
        .while_token = while_token,
        .condition_expr = condition,
        .stmts = stmts
    };

	return create_stmt(WHILE_STMT_TYPE, while_stmt, parser);
}

Stmt *parse_for_stmt(Parser *parser){
    Token *for_token = NULL;
    DynArr *initializations = NULL;
    Expr *condition_expr = NULL;
    DynArr *update_exprs = NULL;
    DynArr *stmts = NULL;

    for_token = previous(parser);

    consume(parser, LEFT_PAREN_TOKTYPE, "Expect '(' after 'for' keyworkd");

    if(match(parser, 1, LET_TOKTYPE)){
        initializations = MEMORY_DYNARR_PTR(CTALLOCATOR);

        do{
            Token *identifier_token = consume(parser, IDENTIFIER_TOKTYPE, "Expect identifier");

            consume(parser, EQUALS_TOKTYPE, "Expect '=' after identifier");

            Expr *initial_value = parse_expr(parser);
            VarDeclStmt *var_decl_stmt = MEMORY_ALLOC(CTALLOCATOR, VarDeclStmt, 1);

            *var_decl_stmt = (VarDeclStmt){
                .is_mutable = 1,
                .is_initialized = 1,
                .identifier_token = identifier_token,
                .initial_value_expr = initial_value
            };

            dynarr_insert_ptr(initializations, var_decl_stmt);
        }while(match(parser, 1, COMMA_TOKTYPE));
    }

    consume(parser, SEMICOLON_TOKTYPE, "Expect ';' before 'for' condition section");

    if(!check(parser, SEMICOLON_TOKTYPE)){
        condition_expr = parse_expr(parser);
    }

    consume(parser, SEMICOLON_TOKTYPE, "Expect ';' before 'for' update section");

    if(!check(parser, RIGHT_PAREN_TOKTYPE)){
        update_exprs = MEMORY_DYNARR_PTR(CTALLOCATOR);

        do{
            Expr *update_expr = parse_expr(parser);

            dynarr_insert_ptr(update_exprs, update_expr);
        }while(match(parser, 1, COMMA_TOKTYPE));
    }

    consume(parser, RIGHT_PAREN_TOKTYPE, "Expect ')' at end of 'for' header");
    consume(parser, LEFT_BRACKET_TOKTYPE, "Expect '{' at start of 'for' body");

    stmts = parse_block_stmt(parser);

    ForStmt *for_stmt = MEMORY_ALLOC(CTALLOCATOR, ForStmt, 1);

    *for_stmt = (ForStmt){
        .for_token = for_token,
        .initializations = initializations,
        .condition_expr = condition_expr,
        .update_exprs = update_exprs,
        .stmts = stmts
    };

    return create_stmt(FOR_STMT_TYPE, for_stmt, parser);
}

Stmt *parse_throw_stmt(Parser *parser){
    Token *throw_token = NULL;
	Expr *value = NULL;

    throw_token = previous(parser);

	if(!check(parser, SEMICOLON_TOKTYPE)){
        value = parse_expr(parser);
    }

    consume(parser, SEMICOLON_TOKTYPE, "Expect ';' at end of throw statement");

    ThrowStmt *throw_stmt = MEMORY_ALLOC(CTALLOCATOR, ThrowStmt, 1);

    *throw_stmt = (ThrowStmt){
        .throw_token = throw_token,
	    .value_expr = value
    };

    return create_stmt(THROW_STMT_TYPE, throw_stmt, parser);
}

Stmt *parse_try_stmt(Parser *parser){
    Token *try_token = NULL;
    DynArr *try_stmts = NULL;
	Token *catch_token = NULL;
	Token *err_identifier = NULL;
	DynArr *catch_stmts = NULL;

    try_token = previous(parser);

    consume(
        parser,
        LEFT_BRACKET_TOKTYPE,
        "Expect '{' after 'try' keyword"
    );

    try_stmts = parse_block_stmt(parser);
    catch_token = consume(
        parser,
        CATCH_TOKTYPE,
        "Expect 'catch' keyword after try block, but got: '%s'",
        peek(parser)->lexeme
    );

    if(!check(parser, LEFT_BRACKET_TOKTYPE)){
        err_identifier = consume(
            parser,
            IDENTIFIER_TOKTYPE,
            "Expect error identifier after 'catch' keyword"
        );
    }

    consume(
        parser,
        LEFT_BRACKET_TOKTYPE,
        "Expect '{' after 'catch' keyword"
    );

    catch_stmts = parse_block_stmt(parser);

    TryStmt *try_stmt = MEMORY_ALLOC(CTALLOCATOR, TryStmt, 1);

    *try_stmt = (TryStmt){
        .try_token = try_token,
        .try_stmts = try_stmts,
	    .catch_token = catch_token,
	    .err_identifier = err_identifier,
	    .catch_stmts = catch_stmts
    };

    return create_stmt(TRY_STMT_TYPE, try_stmt, parser);
}

Stmt *parse_return_stmt(Parser *parser){
    Token *return_token = NULL;
    Expr *value = NULL;

    return_token = previous(parser);

    if(!check(parser, SEMICOLON_TOKTYPE)){
        value = parse_expr(parser);
    }

    consume(
        parser,
        SEMICOLON_TOKTYPE,
        "Expect ';' at end of return statement"
    );

    ReturnStmt *return_stmt = MEMORY_ALLOC(CTALLOCATOR, ReturnStmt, 1);

    *return_stmt = (ReturnStmt){
        .return_token = return_token,
        .ret_expr = value
    };

    return create_stmt(RETURN_STMT_TYPE, return_stmt, parser);
}

Stmt *parse_var_decl_stmt(Parser *parser){
    char is_mutable = 0;
    char is_initialized = 0;
    Token *identifier_token = NULL;
    Expr *initializer_expr = NULL;

    is_mutable = match(parser, 1, MUT_TOKTYPE);
    identifier_token = consume(
        parser,
        IDENTIFIER_TOKTYPE,
        "Expect symbol name after 'let' or 'let mut' keyword(s)");

    if(match(parser, 1, EQUALS_TOKTYPE)){
        is_initialized = 1;
        initializer_expr = parse_expr(parser);
    }

    consume(
        parser,
        SEMICOLON_TOKTYPE,
        "Expect ';' at end of symbol declaration");

    VarDeclStmt *var_decl_stmt = MEMORY_ALLOC(CTALLOCATOR, VarDeclStmt, 1);

    *var_decl_stmt = (VarDeclStmt){
        .is_mutable = is_mutable,
        .is_initialized = is_initialized,
        .identifier_token = identifier_token,
        .initial_value_expr = initializer_expr
    };

    return create_stmt(VAR_DECL_STMT_TYPE, var_decl_stmt, parser);
}

Stmt *parse_function_stmt(Parser *parser){
    if(parser->fns_stack_count == 255){
        error(
            parser,
            peek(parser),
            "Can not nest more than 255 functions"
        );
    }

    parser->fns_stack_count++;

    Token *name_token = NULL;
    DynArr *params = NULL;
    DynArr *stmts = NULL;

    name_token = consume(
        parser,
        IDENTIFIER_TOKTYPE,
        "Expect function name after 'proc' keyword"
    );

    if(match(parser, 1, LEFT_PAREN_TOKTYPE)){
        if(!check(parser, RIGHT_PAREN_TOKTYPE)){
            params = MEMORY_DYNARR_PTR(CTALLOCATOR);

            do{
                unsigned char is_mutable = match(parser, 1, MUT_TOKTYPE);
                Token *identifier = consume(
                    parser,
                    IDENTIFIER_TOKTYPE,
                    "Expect function parameter name"
                );
                ProcParam *param = MEMORY_ALLOC(CTALLOCATOR, ProcParam, 1);

                *param = (ProcParam){
                    .is_mutable = is_mutable,
                    .identifier = identifier
                };

                dynarr_insert_ptr(params, param);
            } while (match(parser, 1, COMMA_TOKTYPE));
        }

        consume(
            parser,
            RIGHT_PAREN_TOKTYPE,
            "Expect ')' at end of function parameters"
        );
    }

    if(match(parser, 1, COLON_TOKTYPE)){
		stmts = MEMORY_DYNARR_PTR(CTALLOCATOR);

        Stmt *unique_stmt = parse_return_stmt(parser);

        dynarr_insert_ptr(stmts, unique_stmt);
	}else{
		consume(
            parser,
            LEFT_BRACKET_TOKTYPE,
            "Expect '{' at start of function body"
        );

        stmts = parse_block_stmt(parser);
	}

    parser->fns_stack_count--;

    if(parser->fns_stack_count == 0){
        ProcPrototype prototype = {
            .identifier_token = name_token,
            .arity = params ? dynarr_len(params) : 0
        };

        dynarr_insert(parser->fns_prototypes, &prototype);
    }

    ProcStmt *proc_stmt = MEMORY_ALLOC(CTALLOCATOR, ProcStmt, 1);

    *proc_stmt = (ProcStmt){
        .identifier = name_token,
        .params = params,
        .stmts = stmts
    };

    return create_stmt(PROC_STMT_TYPE, proc_stmt, parser);
}

Stmt *parse_import_stmt(Parser *parser){
    Token *import_token = NULL;
    DynArr *names = NULL;
    Token *alt_name = NULL;

    import_token = previous(parser);
    names = MEMORY_DYNARR_PTR(CTALLOCATOR);

    do{
        Token *name = consume(parser, IDENTIFIER_TOKTYPE, "Expect module name");

        dynarr_insert_ptr(names, name);
    }while(match(parser, 1, DOT_TOKTYPE));

    if(match(parser, 1, AS_TOKTYPE)){
        alt_name = consume(
            parser,
            IDENTIFIER_TOKTYPE,
            "Expect module alternative name after 'as' keyword"
        );
    }

    consume(
        parser,
        SEMICOLON_TOKTYPE,
        "Expect ';' at end of import statement"
    );

    ImportStmt *import_stmt = MEMORY_ALLOC(CTALLOCATOR, ImportStmt, 1);

    *import_stmt = (ImportStmt){
        .import_token = import_token,
        .names = names,
        .alt_name = alt_name
    };

    return create_stmt(IMPORT_STMT_TYPE, import_stmt, parser);
}

Stmt *parse_export_stmt(Parser *parser){
    Token *export_token = NULL;
    DynArr *symbols = NULL;

    export_token = previous(parser);
    symbols = MEMORY_DYNARR_PTR(CTALLOCATOR);

    consume(parser, LEFT_BRACKET_TOKTYPE, "Expect '{' at start of export symbols");

    do{
        Token *identifier = consume(parser, IDENTIFIER_TOKTYPE, "Expect symbol name");
        dynarr_insert_ptr(symbols, identifier);
    } while (match(parser, 1, COMMA_TOKTYPE));

    consume(parser, RIGHT_BRACKET_TOKTYPE, "Expect '}' at end of export symbols");

    ExportStmt *export_stmt = MEMORY_ALLOC(CTALLOCATOR, ExportStmt, 1);

    *export_stmt = (ExportStmt){
        .export_token = export_token,
        .symbols = symbols
    };

    return create_stmt(EXPORT_STMT_TYPE, export_stmt, parser);
}
//--------------------------------------------------------------------------//
//                          PUBLIC IMPLEMENTATION                           //
//--------------------------------------------------------------------------//
Parser *parser_create(const Allocator *ctallocator){
    Parser *parser = MEMORY_ALLOC(ctallocator, Parser, 1);

    if(!parser){
        return NULL;
    }

	memset(parser, 0, sizeof(Parser));

    parser->ctallocator = ctallocator;

	return parser;
}

int parser_parse(DynArr *tokens, DynArr *fns_prototypes, DynArr *stmts, Parser *parser){
	if(setjmp(parser->err_buf) == 0){
        parser->fns_stack_count = 0;
        parser->current = 0;
        parser->tokens = tokens;
        parser->fns_prototypes = fns_prototypes;

        while(!is_at_end(parser)){
            Stmt *stmt = parse_stmt(parser);
            dynarr_insert_ptr(stmts, stmt);
        }

        return 0;
    }else {
        return 1;
    }
}

int parser_parse_str_interpolation(DynArr *tokens, DynArr *exprs, Parser *parser){
    if(setjmp(parser->err_buf) == 0){
        parser->fns_stack_count = 0;
        parser->current = 0;
        parser->tokens = tokens;
        parser->fns_prototypes = NULL;

        while(!is_at_end(parser)){
            Expr *expr = parse_tenary_expr(parser);
            dynarr_insert_ptr(exprs, expr);
        }

        return 0;
    }else{
        return 1;
    }
}
