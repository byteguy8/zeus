#ifndef OPCODE_H
#define OPCODE_H

typedef enum opcode{
    // PRIMITIVES
    OP_EMPTY,    // push NULL value equivalent
    OP_FALSE,    // push FALSE value
    OP_TRUE,     // push TRUE value
    OP_INT,      // push integer of 8 bytes
    OP_FLOAT,    // push float
    OP_STRING,   // push string
    OP_START_TEMPLATE,     // start constructing template
    OP_END_TEMPLATE,     // end constructing template and push it
    OP_ARRAY,
	OP_LIST,
    OP_DICT,
	OP_RECORD,

    OP_WRITE_TEMPLATE,
    OP_INIT_ARRAY,
    OP_INIT_LIST,
    OP_INIT_DICT,
    OP_INIT_RECORD,

    OP_CONCAT,
    OP_MULSTR,

    // ARITHMETIC
    OP_ADD,    // add two integers or floats and push the result
    OP_SUB,    // subtract two integers or floats and push the result
    OP_MUL,    // muliply two integers or floats and push the result
    OP_DIV,    // divide two integers or floats and push the result
	OP_MOD,    // calculate module from two integers and push the result

    // BITWISE
    OP_BNOT,    // not bitwise
    OP_LSH,     // left shift
    OP_RSH,     // right shift
    OP_BAND,    // and bitwise
    OP_BXOR,    // xor bitwise
    OP_BOR,     // or bitwise

    // COMPARISON
    OP_LT,    // less
    OP_GT,    // greater
    OP_LE,    // less equals
    OP_GE,    // greater equals
    OP_EQ,    // equals
    OP_NE,    // not equals

    // LOGICAL
    OP_OR,
    OP_AND,
    OP_NOT,
    OP_NNOT,

    OP_LOCAL_SET,    // set local symbol
    OP_LOCAL_GET,    // get local symbol
    OP_FOREIGN_SET,    // set out value symbol (for closures)
    OP_FOREIGN_GET,    // get out value symbol (for closures)
    OP_GLOBAL_DEF,    // define a global symbol
    OP_GLOBAL_ACCESS_SET,   // set global symbol access
    OP_GLOBAL_SET,    // get global symbol
    OP_GLOBAL_GET,    // set global symbol
    OP_NATIVE_GET,    // get native symbol
    OP_FN,
    OP_CLOSURE,    // get symbol from list of symbols
	OP_ARRAY_SET,    // set a value inside array
    OP_RECORD_SET,    // set a value inside record

    OP_POP,
    OP_OFFSET,

    OP_JMP,
	OP_JIF,
	OP_JIT,

    OP_IMPORT,
    OP_CALL,
    OP_ACCESS,
    OP_INDEX,
    OP_EXIT,
    OP_RET,
	OP_IS,
    OP_TRY_IN,
    OP_TRY_OUT,
    OP_THROW,
    OP_HALT,
}OPCode;

#endif
