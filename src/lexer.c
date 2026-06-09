#include "lexer.h"

#include "essentials/lzarena.h"
#include "essentials/memory.h"

#include "utils.h"
#include "token.h"

#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>

#define CTALLOCATOR     (lexer->ctallocator)
#define RTALLOCATOR     (lexer->rtallocator)
#define LEXER_ALLOCATOR (lexer->lexer_allocator)
//--------------------------------------------------------------------------//
//                            PRIVATE INTERFACE                             //
//--------------------------------------------------------------------------//
static void error(Lexer *lexer, char *msg, ...);

static int is_at_end(Lexer *lexer);
static int is_dec_digit(char c);
static int is_hex_digit(char c);
static int is_alpha(char c);
static int is_alpha_numeric(char c);

static char peek(Lexer *lexer);
static int match(Lexer *lexer, char c);
static char advance(Lexer *lexer);

static char *copy_source_range(size_t start, size_t end, Lexer *lexer, size_t *out_len);
static char *create_lexeme(char *lexeme, Lexer *lexer, size_t *out_len);
static size_t current_lexeme_len(Lexer *lexer);
static char *current_lexeme(Lexer *lexer, size_t *out_len);
static void put_current_lexeme(Lexer *lexer, char *buff);

static Token *create_token_raw(
    Lexer *lexer,
	int line,
    TokenType type,
    size_t lexeme_len,
    char *lexeme,
    size_t literal_size,
    void *literal
);
static Token *create_token_literal(
    Lexer *lexer,
	TokenType type,
    size_t literal_size,
    void *literal
);
static Token *create_token(Lexer *lexer, TokenType type);
#define ADD_TOKEN_RAW(_token)(dynarr_insert_ptr(lexer->tokens, (_token)))

static void comment(Lexer *lexer);
static Token *decimal(Lexer *lexer);
static Token *identifier(Lexer *lexer);
static int string_placeholder(DynArr *tokens, Lexer *lexer);
static Token *string(Lexer *lexer);
static Token *scan_token(Lexer *lexer, char c);
//--------------------------------------------------------------------------//
//                          PRIVATE IMPLEMENTATION                          //
//--------------------------------------------------------------------------//
void error(Lexer *lexer, char *msg, ...){
    va_list args;
    int line = lexer->line;

	va_start(args, msg);

	fprintf(stderr, "ERROR at line %d:\n\t", line);
	vfprintf(stderr, msg, args);
    fprintf(stderr, "\n");

	va_end(args);
}

inline int is_at_end(Lexer *lexer){
    const DStr *source = lexer->source;

    return lexer->current >= source->len;
}

inline int is_dec_digit(char c){
    return c >= '0' && c <= '9';
}

inline int is_hex_digit(char c){
    return is_dec_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline int is_alpha(char c){
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline int is_alpha_numeric(char c){
    return is_dec_digit(c) || is_alpha(c);
}

inline char peek(Lexer *lexer){
    if(is_at_end(lexer)){
		return '\0';
	}

    const DStr *source = lexer->source;

    return source->buff[lexer->current];
}

int match(Lexer *lexer, char c){
    if(is_at_end(lexer)){
		return 0;
	}

    const DStr *source = lexer->source;
    char current_c = source->buff[lexer->current];

    if(c == current_c){
        lexer->current++;

        return 1;
	}

    return 0;
}

char advance(Lexer *lexer){
    if(is_at_end(lexer)){
		return '\0';
	}

    const DStr *source = lexer->source;

    return source->buff[lexer->current++];
}

// the output str is valid only during compilation allocated
char *copy_source_range(size_t start, size_t end, Lexer *lexer, size_t *out_len){
    const DStr *source = lexer->source;

    assert(end > start && (size_t)end <= source->len);

    size_t lexeme_len = end - start;
    char *lexeme = MEMORY_ALLOC(CTALLOCATOR, char, lexeme_len + 1);

    memcpy(lexeme, source->buff + (size_t)start, lexeme_len);
    lexeme[lexeme_len] = '\0';

    if(out_len){
        *out_len = lexeme_len;
    }

    return lexeme;
}

char *create_lexeme(char *lexeme, Lexer *lexer, size_t *out_len){
    size_t lexeme_len = strlen(lexeme);
    char *new_lexeme = MEMORY_ALLOC(CTALLOCATOR, char, lexeme_len + 1);

    memcpy(new_lexeme, lexeme, lexeme_len);
    new_lexeme[lexeme_len] = 0;

    if(out_len){
        *out_len = lexeme_len;
    }

    return new_lexeme;
}

inline size_t current_lexeme_len(Lexer *lexer){
    return (size_t)(lexer->current - lexer->start);
}

inline char *current_lexeme(Lexer *lexer, size_t *out_len){
    return copy_source_range(lexer->start, lexer->current, lexer, out_len);
}

inline void put_current_lexeme(Lexer *lexer, char *buff){
    size_t start = lexer->start;
    size_t current = lexer->current;
    size_t len = current - start;
    const DStr *source = lexer->source;

    memcpy(buff, source->buff + start, len);

    buff[len] = 0;
}

inline Token *create_token_raw(
    Lexer *lexer,
	int line,
    TokenType type,
    size_t lexeme_len,
    char *lexeme,
    size_t literal_size,
    void *literal
){
    Token *token = MEMORY_ALLOC(lexer->rtallocator, Token, 1);

    token->line = line;
    token->type = type;
    token->lexeme_len = lexeme_len;
    token->lexeme = lexeme;
    token->literal_size = literal_size;
    token->literal = literal;
    token->pathname = lexer->pathname;
    token->extra = NULL;

    return token;
}

inline Token *create_token_literal(
    Lexer *lexer,
	TokenType type,
    size_t literal_size,
    void *literal
){
    size_t lexeme_len;
	char *lexeme = current_lexeme(lexer, &lexeme_len);

	return create_token_raw(
        lexer,
        lexer->line,
        type,
        lexeme_len,
        lexeme,
        literal_size,
        literal
    );
}

inline Token *create_token(Lexer *lexer, TokenType type){
	return create_token_literal(lexer, type, 0, NULL);
}

void comment(Lexer *lexer){
	while(!is_at_end(lexer)){
		if(advance(lexer) == '\n'){
			lexer->line++;

            break;
		}
	}
}

Token *decimal(Lexer *lexer){
    TokenType type = INT_TYPE_TOKEN_TYPE;

    while (!is_at_end(lexer) && is_dec_digit(peek(lexer))){
        advance(lexer);
    }

	if(match(lexer, '.')){
        if(!is_dec_digit(peek(lexer))){
            error(lexer, "Expect digit after decimal point");
            return NULL;
        }

		type = FLOAT_TYPE_TOKEN_TYPE;

		while (!is_at_end(lexer) && is_dec_digit(peek(lexer))){
            advance(lexer);
        }
	}

    size_t lexeme_len;
    char *lexeme = current_lexeme(lexer, &lexeme_len);

    if(type == INT_TYPE_TOKEN_TYPE){
        int64_t *value = MEMORY_ALLOC(lexer->rtallocator, int64_t, 1);
        utils_decimal_str_to_i64(lexeme, value);

		return create_token_raw(
            lexer,
			lexer->line,
            type,
            lexeme_len,
			lexeme,
            sizeof(int64_t),
			value
		);
	}else{
        double *value = MEMORY_ALLOC(lexer->rtallocator, double, 1);
        utils_str_to_double(lexeme, value);

        return create_token_raw(
            lexer,
			lexer->line,
            type,
            lexeme_len,
			lexeme,
            sizeof(double),
			value
		);
	}
}

Token *hexadecimal(Lexer *lexer){
    while(is_hex_digit(peek(lexer))){
        advance(lexer);
    }

    size_t lexeme_len;
    char *lexeme = current_lexeme(lexer, &lexeme_len);

    if(lexeme_len == 2){
        error(lexer, "Expect digit(s) after '%s' prefix", lexeme);
        return NULL;
    }

    if(lexeme_len > 18){
        error(lexer, "Expect at most 18 digits after hexadecimal prefix");
        return NULL;
    }

    int64_t *value = MEMORY_ALLOC(lexer->rtallocator, int64_t, 1);

    utils_hexadecimal_str_to_i64(lexeme, value);

    return create_token_raw(
        lexer,
        lexer->line,
        INT_TYPE_TOKEN_TYPE,
        lexeme_len,
        lexeme,
        sizeof(int64_t),
        value
    );
}

Token *identifier(Lexer *lexer){
    while (!is_at_end(lexer) && is_alpha_numeric(peek(lexer))){
        advance(lexer);
    }

    size_t lexeme_len = current_lexeme_len(lexer);
    char lexeme[lexeme_len + 1];
    TokenType *type = NULL;

    put_current_lexeme(lexer, lexeme);
    lzohtable_lookup(lexeme_len, lexeme, (LZOHTable *)lexer->keywords, (void **)(&type));

    if(type){
        return create_token(lexer, *type);
    }

    return create_token(lexer, IDENTIFIER_TOKEN_TYPE);
}

int string_placeholder(DynArr *tokens, Lexer *lexer){
    while (!is_at_end(lexer) && peek(lexer) != '}'){
        char c = advance(lexer);
        Token *token = NULL;

        switch (c){
            case '?':{
                token = create_token(lexer, QUESTION_MARK_TOKEN_TYPE);

                break;
            }case ':':{
                token = create_token(lexer, COLON_TOKEN_TYPE);

                break;
            }case '[':{
                token = create_token(lexer, LEFT_SQUARE_TOKEN_TYPE);

                break;
            }case ']':{
                token = create_token(lexer, RIGHT_SQUARE_TOKEN_TYPE);

                break;
            }case '(':{
                token = create_token(lexer, LEFT_PAREN_TOKEN_TYPE);

                break;
            }case ')':{
                token = create_token(lexer, RIGHT_PAREN_TOKEN_TYPE);

                break;
            }case '{':{
                token = create_token(lexer, LEFT_BRACKET_TOKEN_TYPE);

                break;
            }case '}':{
                token = create_token(lexer, RIGHT_BRACKET_TOKEN_TYPE);

                break;
            }case '~':{
                token = create_token(lexer, NOT_BITWISE_TOKEN_TYPE);

                break;
            }case '&':{
                token = create_token(lexer, AND_BITWISE_TOKEN_TYPE);

                break;
            }case '^':{
                token = create_token(lexer, XOR_BITWISE_TOKEN_TYPE);

                break;
            }case '|':{
                token = create_token(lexer, OR_BITWISE_TOKEN_TYPE);

                break;
            }case ',':{
                token = create_token(lexer, COMMA_TOKEN_TYPE);

                break;
            }case '.':{
                if(match(lexer, '.')){
                    token = create_token(lexer, DOUBLE_DOT_TOKEN_TYPE);
                }else{
                    token = create_token(lexer, DOT_TOKEN_TYPE);
                }

                break;
            }case '+':{
                if(match(lexer, '=')){
                    token = create_token(lexer, COMPOUND_ADD_TOKEN_TYPE);
                }else{
                    token = create_token(lexer, PLUS_TOKEN_TYPE);
                }

                break;
            }case '-':{
                if(match(lexer, '=')){
                    token = create_token(lexer, COMPOUND_SUB_TOKEN_TYPE);
                }else{
                    token = create_token(lexer, MINUS_TOKEN_TYPE);
                }

                break;
            }case '*':{
                if(match(lexer, '*')){
                    token = create_token(lexer, DOUBLE_ASTERISK_TOKEN_TYPE);
                }else if(match(lexer, '=')){
                    token = create_token(lexer, COMPOUND_MUL_TOKEN_TYPE);
                }else{
                    token = create_token(lexer, ASTERISK_TOKEN_TYPE);
                }

                break;
            }case '/':{
                if(match(lexer, '=')){
                    token = create_token(lexer, COMPOUND_DIV_TOKEN_TYPE);
                }else{
                    token = create_token(lexer, SLASH_TOKEN_TYPE);
                }

                break;
            }case '<':{
                if(match(lexer, '<')){
                    token = create_token(lexer, LEFT_SHIFT_TOKEN_TYPE);
                }else if(match(lexer, '=')){
                    token = create_token(lexer, LESS_EQUALS_TOKEN_TYPE);
                }else{
                    token = create_token(lexer, LESS_TOKEN_TYPE);
                }

                break;
            }case '>':{
                if(match(lexer, '>')){
                    token = create_token(lexer, RIGHT_SHIFT_TOKEN_TYPE);
                }else if(match(lexer, '=')){
                    token = create_token(lexer, GREATER_EQUALS_TOKEN_TYPE);
                }else{
                    token = create_token(lexer, GREATER_TOKEN_TYPE);
                }

                break;
            }case '=':{
                if(match(lexer, '=')){
                    token = create_token(lexer, EQUALS_EQUALS_TOKEN_TYPE);
                }

                break;
            }case '!':{
                if(match(lexer, '=')){
                    token = create_token(lexer, NOT_EQUALS_TOKEN_TYPE);
                }else{
                    token = create_token(lexer, EXCLAMATION_TOKEN_TYPE);
                }

                break;
            }case ' ':{
                lexer->start = lexer->current;

                continue;
            }default:{
                if(is_dec_digit(c) && (match(lexer, 'x') || match(lexer, 'X'))){
                    token = hexadecimal(lexer);
                }else if(is_dec_digit(c)){
                    token = decimal(lexer);
                }else if(is_alpha_numeric(c)){
                    token = identifier(lexer);
                }else if(c == '"'){
                    token = string(lexer);
                }else{
                    error(lexer, "Unknown token inside placeholder: '%c'", c);

                    return 1;
                }
            }
        }

        if(token){
            dynarr_insert_ptr(tokens, token);

            lexer->start = lexer->current;

            continue;
        }

        return 1;
    }

    return 0;
}

Token *string(Lexer *lexer){
    DynArr *str_tokens = NULL;
    LZBStr *str_helper = MEMORY_LZBSTR(LEXER_ALLOCATOR);

    lzbstr_grow_by(str_helper, 1024);

    while(!is_at_end(lexer) && peek(lexer) != '"'){
        char c = advance(lexer);

        if(c != '{' && c != '\\'){
            lzbstr_append(str_helper, (char[]){c, 0});

            continue;
        }

        if(c == '{'){
            lexer->start = lexer->current;

            if(!str_tokens){
                str_tokens = MEMORY_DYNARR_PTR(CTALLOCATOR);
            }

            size_t lexeme_len = 0;
            char *lexeme = NULL;
            size_t literal_size = 0;
            char *literal = NULL;

            lzbstr_insert_args(str_helper, 0, "\"");
            lzbstr_append(str_helper, "\"");

            lzbstr_rclone_buff(
                str_helper,
                (LZBStrAllocator *)CTALLOCATOR,
                &lexeme_len,
                &lexeme
            );

            if(lzbstr_len(str_helper) == 2){
                literal_size = 0;
                literal = MEMORY_ALLOC(RTALLOCATOR, char, 1);
                literal[0] = 0;
            }else{
                lzbstr_rclone_buff_rng(
                    str_helper,
                    (LZBStrAllocator *)RTALLOCATOR,
                    1,
                    lzbstr_len(str_helper) - 2,
                    &literal_size,
                    &literal
                );
            }

            lzbstr_reset(str_helper);

            Token *str_token = create_token_raw(
                lexer,
                lexer->line,
                STR_TYPE_TOKEN_TYPE,
                lexeme_len,
                lexeme,
                literal_size,
                literal
            );

            dynarr_insert_ptr(str_tokens, str_token);

            if(string_placeholder(str_tokens, lexer)){
                return NULL;
            }

            if(!match(lexer, '}')){
                error(lexer, "Unclosed placeholder");
            }

            continue;
        }

        if(c == '\\'){
            char espace = advance(lexer);

            switch (espace){
                case 't':{
                    lzbstr_append(str_helper, (char[]){'\t', 0});

                    break;
                }case 'n':{
                    lzbstr_append(str_helper, (char[]){'\n', 0});

                    break;
                }case 'r':{
                    lzbstr_append(str_helper, (char[]){'\r', 0});

                    break;
                }case '"':{
                    lzbstr_append(str_helper, (char[]){'"', 0});

                    break;
                }case '{':{
                    lzbstr_append(str_helper, (char[]){'{', 0});

                    break;
                }case '\\':{
                    lzbstr_append(str_helper, (char[]){'\\', 0});

                    break;
                }default:{
                    error(lexer, "Unknown espace sequence: \\%c", espace);

                    return NULL;
                }
            }
        }
    }

    if(!match(lexer, '"')){
        error(lexer, "Unterminated string literal");

        return NULL;
    }

    if(str_tokens){
        if(lzbstr_len(str_helper) > 0){
            size_t lexeme_len;
            char *lexeme;
            size_t literal_size;
            char *literal;

            lzbstr_insert_args(str_helper, 0, "\"");
            lzbstr_append(str_helper, "\"");

            lzbstr_rclone_buff(
                str_helper,
                (LZBStrAllocator *)CTALLOCATOR,
                &lexeme_len,
                &lexeme
            );

            lzbstr_rclone_buff_rng(
                str_helper,
                (LZBStrAllocator *)RTALLOCATOR,
                1,
                lzbstr_len(str_helper) - 2,
                &literal_size,
                &literal
            );

            Token *token = create_token_raw(
                lexer,
                lexer->line,
                STR_TYPE_TOKEN_TYPE,
                lexeme_len,
                lexeme,
                literal_size,
                literal
            );

            dynarr_insert_ptr(str_tokens, token);
        }

        size_t lexeme_len;
        char *lexeme = create_lexeme("EOF", lexer, &lexeme_len);
        Token *eof_token = create_token_raw(
            lexer,
            lexer->line,
            EOF_TOKEN_TYPE,
            lexeme_len,
            lexeme,
            0,
            NULL
        );

        dynarr_insert_ptr(str_tokens, eof_token);

        return create_token_literal(
            lexer,
            TEMPLATE_TYPE_TOKEN_TYPE,
            sizeof(DynArr *),
            str_tokens
        );
    }

    size_t literal_size;
    char *literal = NULL;

    if(lzbstr_len(str_helper) == 0){
        literal_size = 0;
        literal = MEMORY_ALLOC(RTALLOCATOR, char, 1);
        literal[0] = 0;
    }else{
        lzbstr_rclone_buff(
            str_helper,
            (LZBStrAllocator *)RTALLOCATOR,
            &literal_size,
            &literal
        );
    }

    return create_token_literal(lexer, STR_TYPE_TOKEN_TYPE, literal_size, literal);
}

Token *scan_token(Lexer *lexer, char c){
    switch (c){
        case '?':{
        	return create_token(lexer, QUESTION_MARK_TOKEN_TYPE);
        }case ':':{
			return create_token(lexer, COLON_TOKEN_TYPE);
		}case ';':{
            return create_token(lexer, SEMICOLON_TOKEN_TYPE);
        }case '[':{
            return create_token(lexer, LEFT_SQUARE_TOKEN_TYPE);
        }case ']':{
            return create_token(lexer, RIGHT_SQUARE_TOKEN_TYPE);
        }case '(':{
            return create_token(lexer, LEFT_PAREN_TOKEN_TYPE);
        }case ')':{
            return create_token(lexer, RIGHT_PAREN_TOKEN_TYPE);
        }case '{':{
            return create_token(lexer, LEFT_BRACKET_TOKEN_TYPE);
        }case '}':{
            return create_token(lexer, RIGHT_BRACKET_TOKEN_TYPE);
        }case '~':{
            return create_token(lexer, NOT_BITWISE_TOKEN_TYPE);
        }case '&':{
            return create_token(lexer, AND_BITWISE_TOKEN_TYPE);
        }case '^':{
            return create_token(lexer, XOR_BITWISE_TOKEN_TYPE);
        }case '|':{
            return create_token(lexer, OR_BITWISE_TOKEN_TYPE);
        }case ',':{
			return create_token(lexer, COMMA_TOKEN_TYPE);
		}case '.':{
            if(match(lexer, '.')){
                return create_token(lexer, DOUBLE_DOT_TOKEN_TYPE);
            }else{
                return create_token(lexer, DOT_TOKEN_TYPE);
            }
		}case '+':{
			if(match(lexer, '=')){
				return create_token(lexer, COMPOUND_ADD_TOKEN_TYPE);
			}else{
				return create_token(lexer, PLUS_TOKEN_TYPE);
			}
        }case '-':{
			if(match(lexer, '=')){
				return create_token(lexer, COMPOUND_SUB_TOKEN_TYPE);
			}else{
				return create_token(lexer, MINUS_TOKEN_TYPE);
			}
        }case '*':{
			if(match(lexer, '*')){
                return create_token(lexer, DOUBLE_ASTERISK_TOKEN_TYPE);
            }else if(match(lexer, '=')){
				return create_token(lexer, COMPOUND_MUL_TOKEN_TYPE);
			}else{
				return create_token(lexer, ASTERISK_TOKEN_TYPE);
			}
        }case '/':{
			if(match(lexer, '/')){
				comment(lexer);

                return NULL;
			}else if(match(lexer, '=')){
				return create_token(lexer, COMPOUND_DIV_TOKEN_TYPE);
			}else{
				return create_token(lexer, SLASH_TOKEN_TYPE);
			}
        }case '<':{
            if(match(lexer, '<')){
                return create_token(lexer, LEFT_SHIFT_TOKEN_TYPE);
            }else if(match(lexer, '=')){
				return create_token(lexer, LESS_EQUALS_TOKEN_TYPE);
			}else{
				return create_token(lexer, LESS_TOKEN_TYPE);
			}
        }case '>':{
            if(match(lexer, '>')){
                return create_token(lexer, RIGHT_SHIFT_TOKEN_TYPE);
            }else if(match(lexer, '=')){
				return create_token(lexer, GREATER_EQUALS_TOKEN_TYPE);
			}else{
				return create_token(lexer, GREATER_TOKEN_TYPE);
			}
        }case '=':{
            if(match(lexer, '=')){
				return create_token(lexer, EQUALS_EQUALS_TOKEN_TYPE);
			}else{
				return create_token(lexer, EQUALS_TOKEN_TYPE);
			}
        }case '!':{
            if(match(lexer, '=')){
				return create_token(lexer, NOT_EQUALS_TOKEN_TYPE);
			}else{
				return create_token(lexer, EXCLAMATION_TOKEN_TYPE);
			}
        }case '\n':{
            lexer->line++;

            return NULL;
        }case ' ':
         case '\r':
         case '\t':
         case '\0':{
            return NULL;
        }default:{
            if(is_dec_digit(c)){
                if((match(lexer, 'x') || match(lexer, 'X'))){
                    return hexadecimal(lexer);
                }

                return decimal(lexer);
            }else if(is_alpha_numeric(c)){
				return identifier(lexer);
			}else if(c == '"'){
				return string(lexer);
			}else{
				error(lexer, "Unknown token '%c':%d", c, c);

                return NULL;
			}
        }
    }

    return NULL;
}
//--------------------------------------------------------------------------//
//                          PUBLIC IMPLEMENTATION                           //
//--------------------------------------------------------------------------//
Lexer *lexer_create(const Allocator *ctallocator, const Allocator *rtallocator){
    Lexer *lexer = (Lexer *)MEMORY_ALLOC(ctallocator, Lexer, 1);

    if(!lexer){
        return NULL;
    }

    memset(lexer, 0, sizeof(Lexer));
    lexer->ctallocator = ctallocator;
    lexer->rtallocator = rtallocator;

    return lexer;
}

int lexer_lex(
    Lexer *lexer,
    const DStr *source,
    const char *pathname,
    const LZOHTable *keywords,
    DynArr *tokens
){
    if(setjmp(lexer->err_buf) == 0){
    	LZArena *lexer_arena = NULL;
  		Allocator *lexer_arena_allocator = memory_arena_allocator(CTALLOCATOR, &lexer_arena);

    	lexer->line = 1;
        lexer->start = 0;
        lexer->current = 0;
        lexer->source = source;
        lexer->tokens = tokens;
        lexer->keywords = keywords;
        lexer->pathname = pathname;
        lexer->lexer_allocator = lexer_arena_allocator;

        while (!is_at_end(lexer)){
            char c = advance(lexer);
            Token *token = scan_token(lexer, c);

            if(token){
                ADD_TOKEN_RAW(token);
            }

            lexer->start = lexer->current;

            if(lzarena_used_memory(lexer_arena) > 0){
            	lzarena_free_all(lexer_arena);
            }
        }

        size_t lexeme_len;
        char *lexeme = create_lexeme("EOF", lexer, &lexeme_len);

        ADD_TOKEN_RAW(
        	create_token_raw(
                lexer,
         		-1,
           		EOF_TOKEN_TYPE,
             	lexeme_len,
              	lexeme,
               	0,
                NULL
         	)
        );
        memory_destroy_arena_allocator(lexer_arena_allocator);

        return 0;
    }else{
        return 1;
    }
}