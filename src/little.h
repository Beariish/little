#pragma once

#include <stdint.h>
#include <stddef.h> // size_t

typedef uint64_t lt_Value;

// IEEE 756 DOUBLE       S[Exponent-][Mantissa------------------------------------------]
#define LT_SIGN_BIT   (0b1000000000000000000000000000000000000000000000000000000000000000)
#define LT_EXPONENT   (0b0111111111110000000000000000000000000000000000000000000000000000)
#define LT_QNAN_BIT   (0b0000000000001000000000000000000000000000000000000000000000000000)
#define LT_TYPE_MASK  (0b0000000000000111000000000000000000000000000000000000000000000000)
#define LT_VALUE_MASK (0b0000000000000000111111111111111111111111111111111111111111111111)

#define LT_NAN_MASK (LT_EXPONENT | LT_QNAN_BIT)

#define LT_TYPE_NULL    (0b0000000000000011000000000000000000000000000000000000000000000000)
#define LT_TYPE_BOOL    (0b0000000000000001000000000000000000000000000000000000000000000000)
#define LT_TYPE_STRING  (0b0000000000000010000000000000000000000000000000000000000000000000)
#define LT_TYPE_OBJECT  (0b0000000000000101000000000000000000000000000000000000000000000000)

#define LT_VALUE_NULL       ((lt_Value)(LT_NAN_MASK | LT_TYPE_NULL))
#define LT_VALUE_FALSE      ((lt_Value)(LT_NAN_MASK | LT_TYPE_BOOL))
#define LT_VALUE_TRUE       ((lt_Value)(LT_NAN_MASK | (LT_TYPE_BOOL | 1)))
#define LT_VALUE_NUMBER(x)  ((lt_Value)(lt_make_number((double)x)))
#define LT_VALUE_OBJECT(x)  ((lt_Value)(LT_NAN_MASK | (LT_TYPE_OBJECT | (uint64_t)x)))

#define LT_IS_NUMBER(x)  (((x) & LT_NAN_MASK) != LT_NAN_MASK)
#define LT_IS_NULL(x)    ((x) == LT_TYPE_NULL)
#define LT_IS_BOOL(x)    (x == LT_VALUE_TRUE || x == LT_VALUE_FALSE)
#define LT_IS_TRUE(x)    (x == LT_VALUE_TRUE)
#define LT_IS_FALSE(x)   (x == LT_VALUE_FALSE)
#define LT_IS_TRUTHY(x) (!(x == LT_VALUE_FALSE || x == LT_VALUE_NULL))
#define LT_IS_STRING(x)  (!LT_IS_NUMBER(x) && (x & LT_TYPE_MASK) == LT_TYPE_STRING)
#define LT_IS_OBJECT(x)  (!LT_IS_NUMBER(x) && (x & LT_TYPE_MASK) == LT_TYPE_OBJECT)
#define LT_IS_TABLE(x)    (LT_IS_OBJECT(x) && LT_GET_OBJECT(x)->type == LT_OBJECT_TABLE)
#define LT_IS_ARRAY(x)    (LT_IS_OBJECT(x) && LT_GET_OBJECT(x)->type == LT_OBJECT_ARRAY)
#define LT_IS_FUNCTION(x) (LT_IS_OBJECT(x) && LT_GET_OBJECT(x)->type == LT_OBJECT_FN)
#define LT_IS_CLOSURE(x)  (LT_IS_OBJECT(x) && LT_GET_OBJECT(x)->type == LT_OBJECT_CLOSURE)
#define LT_IS_NATIVE(x)   (LT_IS_OBJECT(x) && LT_GET_OBJECT(x)->type == LT_OBJECT_NATIVEFN)
#define LT_IS_PTR(x)      (LT_IS_OBJECT(x) && LT_GET_OBJECT(x)->type == LT_OBJECT_PTR)

#define LT_GET_NUMBER(x) lt_get_number(x)
#define LT_GET_STRING(vm, x) lt_get_string(vm, x)
#define LT_GET_OBJECT(x) ((lt_Object*)(x & LT_VALUE_MASK))

typedef enum {
	LT_TOKEN_TRUE_LITERAL,
	LT_TOKEN_FALSE_LITERAL,
	LT_TOKEN_STRING_LITERAL,
	LT_TOKEN_NULL_LITERAL,
	LT_TOKEN_NUMBER_LITERAL,

	LT_TOKEN_IDENTIFIER,

	LT_TOKEN_PERIOD,
	LT_TOKEN_COMMA,
	LT_TOKEN_COLON,

	LT_TOKEN_OPENPAREN,
	LT_TOKEN_CLOSEPAREN,

	LT_TOKEN_OPENBRACKET,
	LT_TOKEN_CLOSEBRACKET,

	LT_TOKEN_OPENBRACE,
	LT_TOKEN_CLOSEBRACE,

	LT_TOKEN_FN,
	LT_TOKEN_BREAK,
	LT_TOKEN_VAR,
	LT_TOKEN_IF,
	LT_TOKEN_ELSE,
	LT_TOKEN_ELSEIF,
	LT_TOKEN_FOR,
	LT_TOKEN_IN,
	LT_TOKEN_WHILE,
	LT_TOKEN_RETURN,

	LT_TOKEN_PLUS,
	LT_TOKEN_MINUS,
	LT_TOKEN_NEGATE,
	LT_TOKEN_MULTIPLY,
	LT_TOKEN_DIVIDE,
	LT_TOKEN_ASSIGN,
	LT_TOKEN_EQUALS,
	LT_TOKEN_NOTEQUALS,
	LT_TOKEN_GT,
	LT_TOKEN_GTE,
	LT_TOKEN_LT,
	LT_TOKEN_LTE,
	LT_TOKEN_AND,
	LT_TOKEN_OR,
	LT_TOKEN_NOT,

	LT_TOKEN_END,
} lt_TokenType;

typedef struct {
	lt_TokenType type;
	uint16_t line, col, idx;
} lt_Token;

typedef struct {
	void* data;
	uint32_t length, capacity, element_size;
} lt_Buffer;

typedef struct {
	lt_TokenType type;
	union {
		char* string;
		double number;
	};
} lt_Literal;

typedef struct {
	char* name;
	uint32_t num_references;
} lt_Identifier;

typedef struct {
	lt_Buffer token_buffer;
	lt_Buffer literal_buffer;
	lt_Buffer identifier_buffer;

	const char* source;
	const char* module;

	uint8_t is_valid;
} lt_Tokenizer;

typedef enum {
	LT_AST_NODE_ERROR,
	LT_AST_NODE_EMPTY,
	LT_AST_NODE_CHUNK,

	LT_AST_NODE_LITERAL,
	LT_AST_NODE_TABLE,
	LT_AST_NODE_ARRAY,
	LT_AST_NODE_IDENTIFIER,
	LT_AST_NODE_INDEX,
	LT_AST_NODE_BINARYOP,
	LT_AST_NODE_UNARYOP,
	LT_AST_NODE_DECLARE,
	LT_AST_NODE_ASSIGN,
	LT_AST_NODE_FN,
	LT_AST_NODE_CALL,
	LT_AST_NODE_RETURN,
	LT_AST_NODE_IF,
	LT_AST_NODE_ELSE,
	LT_AST_NODE_ELSEIF,
	LT_AST_NODE_FOR,
	LT_AST_NODE_WHILE,
	LT_AST_NODE_BREAK,
} lt_AstNodeType;

struct lt_AstNode;
struct lt_Scope;

typedef struct
{
	uint16_t line, col;
} lt_DebugLoc;

typedef struct
{
	const char* module_name;
	lt_Buffer locations;
} lt_DebugInfo;

typedef struct lt_AstNode {
	lt_AstNodeType type;
	lt_DebugLoc loc;

	union {
		struct {
			char* name;
			lt_Buffer body;
			struct lt_Scope* scope;
		} chunk;

		struct {
			lt_Token* token;
		} literal;

		struct {
			lt_Buffer keys;
			lt_Buffer values;
		} table;

		struct {
			lt_Buffer values;
		} array;

		struct {
			lt_Token* token;
		} identifier;

		struct {
			struct lt_AstNode* source;
			struct lt_AstNode* idx;
		} index;

		struct {
			lt_TokenType type;
			struct lt_AstNode* left;
			struct lt_AstNode* right;
		} binary_op;

		struct {
			lt_TokenType type;
			struct lt_AstNode* expr;
		} unary_op;

		struct {
			lt_Token* identifier;
			struct lt_AstNode* expr;
		} declare;

		struct {
			struct lt_AstNode* left;
			struct lt_AstNode* right;
		} assign;

		struct {
			lt_Token* args[16];
			struct lt_Scope* scope;
			lt_Buffer body;
		} fn;

		struct {
			struct lt_AstNode* callee;
			struct lt_AstNode* args[16];
		} call;

		struct {
			struct lt_AstNode* expr;
		} ret;

		struct {
			struct lt_AstNode* expr;
			lt_Buffer body;
			struct lt_AstNode* next;
		} branch;

		struct {
			uint16_t identifier, closureidx;
			struct lt_AstNode* iterator;
			lt_Buffer body;
		} loop;
	};
} lt_AstNode;

typedef struct lt_Scope {
	struct lt_Scope* last;

	lt_Token* start;
	lt_Buffer locals;
	lt_Buffer upvals;
	lt_Token* end;
} lt_Scope;

typedef struct {
	lt_Buffer ast_nodes;
	lt_AstNode* root;

	lt_Tokenizer* tkn;
	lt_Scope* current;

	uint8_t is_valid;
} lt_Parser;

typedef struct {
	lt_Value key, value;
} lt_TablePair;

typedef struct {
	lt_Buffer buckets[16];
} lt_Table;

typedef enum {
	LT_OBJECT_CHUNK,
	LT_OBJECT_FN,
	LT_OBJECT_CLOSURE,
	LT_OBJECT_TABLE,
	LT_OBJECT_ARRAY,
	LT_OBJECT_NATIVEFN,
	LT_OBJECT_PTR,
} lt_ObjectType;

struct lt_VM;

typedef uint8_t(*lt_NativeFn)(struct lt_VM* vm, uint8_t argc);

typedef struct {
	lt_ObjectType type;

	union
	{
		struct
		{
			lt_Buffer code;
			lt_Buffer constants;
			char* name;
			lt_DebugInfo* debug;
		} chunk;

		struct
		{
			uint8_t arity;
			lt_Buffer code;
			lt_Buffer constants;
			lt_DebugInfo* debug;
		} fn;

		struct
		{
			char* string;
			uint16_t len;
		} string;

		struct
		{
			lt_Value function;
			lt_Buffer captures;
		} closure;


		lt_Table table;
		lt_Buffer array;
		lt_NativeFn native;
		void* ptr;
	};

	uint8_t markbit : 1;
} lt_Object;

typedef struct lt_Frame {
	lt_Object* callee;
	lt_Buffer* code;
	lt_Buffer* constants;
	lt_Buffer* upvals;
	uint32_t pc;
	uint16_t start;
} lt_Frame;

typedef void* (*lt_AllocFn)(size_t);
typedef void (*lt_FreeFn)(void*);
typedef void (*lt_ErrorFn)(struct lt_VM* vm, const char*);

#ifndef LT_STACK_SIZE
#define LT_STACK_SIZE 256
#endif

#ifndef LT_CALLSTACK_SIZE
#define LT_CALLSTACK_SIZE 32
#endif

#ifndef LT_DEDUP_TABLE_SIZE
#define LT_DEDUP_TABLE_SIZE 64
#endif

typedef struct lt_VM {
	lt_Buffer heap;
	lt_Buffer keepalive;

	uint16_t top;
	lt_Value stack[LT_STACK_SIZE];

	uint16_t depth;
	lt_Frame callstack[LT_CALLSTACK_SIZE];
	lt_Frame* current;

	lt_Buffer strings[LT_DEDUP_TABLE_SIZE];

	lt_Value global;

	lt_AllocFn alloc;
	lt_FreeFn free;
	lt_ErrorFn error;

	void* error_buf;
	uint8_t generate_debug;
} lt_VM;

lt_VM* lt_open(lt_AllocFn alloc, lt_FreeFn free, lt_ErrorFn error);
void lt_destroy(lt_VM* vm);

lt_Buffer lt_buffer_new(uint32_t element_size);
void lt_buffer_destroy(lt_VM* vm, lt_Buffer* buf);

lt_Object* lt_allocate(lt_VM* vm, lt_ObjectType type);
void lt_free(lt_VM* vm, uint32_t heapidx);

void lt_nocollect(lt_VM* vm, lt_Object* obj);
void lt_resumecollect(lt_VM* vm, lt_Object* obj);
uint32_t lt_collect(lt_VM* vm);

void lt_push(lt_VM* vm, lt_Value val);
lt_Value lt_pop(lt_VM* vm);
lt_Value lt_at(lt_VM* vm, uint32_t idx);

void lt_close(lt_VM* vm, uint8_t count);
lt_Value lt_getupval(lt_VM* vm, uint8_t idx);
void lt_setupval(lt_VM* vm, uint8_t idx, lt_Value val);

uint16_t lt_exec(lt_VM* vm, lt_Value callable, uint8_t argc);
void lt_error(lt_VM* vm, const char* msg);
void lt_runtime_error(lt_VM* vm, const char* message);

lt_Tokenizer lt_tokenize(lt_VM* vm, const char* source, const char* mod_name);
lt_Parser lt_parse(lt_VM* vm, lt_Tokenizer* tkn);
lt_Value lt_compile(lt_VM* vm, lt_Parser* p);

void lt_free_parser(lt_VM* vm, lt_Parser* p);
void lt_free_tokenizer(lt_VM* vm, lt_Tokenizer* tok);

lt_Value lt_loadstring(lt_VM* vm, const char* source, const char* mod_name);
uint32_t lt_dostring(lt_VM* vm, const char* source, const char* mod_name);

lt_Value lt_make_number(double n);
double lt_get_number(lt_Value v);

lt_Value lt_make_string(lt_VM* vm, const char* string);
const char* lt_get_string(lt_VM* vm, lt_Value value);

uint8_t lt_equals(lt_Value a, lt_Value b);

lt_Value lt_make_table(lt_VM* vm);
lt_Value lt_table_set(lt_VM* vm, lt_Value table, lt_Value key, lt_Value val); 
lt_Value lt_table_get(lt_VM* vm, lt_Value table, lt_Value key);
uint8_t  lt_table_pop(lt_VM* vm, lt_Value table, lt_Value key);

lt_Value  lt_make_array(lt_VM* vm);
lt_Value  lt_array_push(lt_VM* vm, lt_Value array, lt_Value val);
lt_Value* lt_array_at(lt_Value array, uint32_t idx);
lt_Value  lt_array_remove(lt_VM* vm, lt_Value array, uint32_t idx);
uint32_t  lt_array_length(lt_Value array);

lt_Value lt_make_native(lt_VM* vm, lt_NativeFn fn);
lt_Value lt_make_ptr(lt_VM* vm, void* ptr);
void* lt_get_ptr(lt_Value ptr);