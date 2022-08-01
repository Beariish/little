#include "little.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>

#if defined(__APPLE__)
  #define sprintf_s snprintf
  #define strncpy_s(dst, dstsz, src, count) strncpy(dst, src, count)
#endif

static lt_Value LT_NULL = LT_VALUE_NULL;

typedef struct {
	uint64_t hash;
	char* string;
	lt_Value value;
	uint32_t refcount;
	uint32_t len;
} lt_StringDedupEntry;

typedef enum {
	LT_OP_NOP,

	LT_OP_PUSH, LT_OP_DUP,

	LT_OP_PUSHS, LT_OP_PUSHC, LT_OP_PUSHN, LT_OP_PUSHT, LT_OP_PUSHF,

	LT_OP_ADD, LT_OP_SUB, LT_OP_MUL, LT_OP_DIV, LT_OP_NEG,
	LT_OP_EQ, LT_OP_NEQ, LT_OP_GT, LT_OP_GTE,
	LT_OP_AND, LT_OP_OR, LT_OP_NOT,

	LT_OP_LOAD, LT_OP_STORE,
	LT_OP_LOADUP, LT_OP_STOREUP,

	LT_OP_CLOSE, LT_OP_CALL,

	LT_OP_MAKET, LT_OP_MAKEA, LT_OP_SETT, LT_OP_GETT, LT_OP_GETG,

	LT_OP_JMP, LT_OP_JMPC, LT_OP_JMPN,

	LT_OP_RET,
} lt_OpCode;

uint64_t static MurmurOAAT64(const char* key)
{
	uint64_t h = 525201411107845655ull;
	for (; *key; ++key) {
		h ^= *key;
		h *= 0x5bd1e9955bd1e995;
		h ^= h >> 47;
	}
	return h;
}

typedef union {
	double flt;
	uint64_t bits;
} _lt_conversion_union;

lt_Buffer lt_buffer_new(uint32_t element_size)
{
	lt_Buffer buf;
	buf.element_size = element_size;
	buf.capacity = 0;
	buf.length = 0;
	buf.data = 0;

	return buf;
}

void lt_buffer_destroy(lt_VM* vm, lt_Buffer* buf)
{
	if (buf->data != 0) vm->free(buf->data);
	buf->data = 0;
	buf->length = 0;
	buf->capacity = 0;
}

static uint8_t lt_buffer_push(lt_VM* vm, lt_Buffer* buf, void* element)
{
	uint8_t has_allocated = 0;
	if (buf->length + 1 > buf->capacity)
	{
		has_allocated = 1;

		void* new_buffer = vm->alloc(buf->element_size * (buf->capacity + 16));

		if (buf->data != 0)
		{
			memcpy(new_buffer, buf->data, buf->element_size * buf->capacity);
			free(buf->data);
		}

		buf->data = new_buffer;
		buf->capacity += 16;
	}

	memcpy((uint8_t*)buf->data + buf->element_size * buf->length, element, buf->element_size);
	buf->length++;

	return has_allocated;
}

static void* lt_buffer_at(lt_Buffer* buf, uint32_t idx)
{
	return (uint8_t*)buf->data + buf->element_size * idx;
}

static void* lt_buffer_last(lt_Buffer* buf)
{
	return lt_buffer_at(buf, buf->length - 1);
}

static void lt_buffer_cycle(lt_Buffer* buf, uint32_t idx)
{
	memcpy(lt_buffer_at(buf, idx), lt_buffer_last(buf), buf->element_size);
	buf->length--;
}

static void lt_buffer_pop(lt_Buffer* buf)
{
	buf->length--;
}

lt_Value lt_make_number(double n)
{
	_lt_conversion_union u;
	u.flt = n;
	return (lt_Value)u.bits;
}

double lt_get_number(lt_Value v)
{
	_lt_conversion_union u;
	u.bits = v;
	return (double)u.flt;
}

#define VALTONUM(x) ((union { uint64_t u; double n; }) { x }.n)

static void _lt_tokenize_error(lt_VM* vm, const char* module, uint16_t line, uint16_t col, const char* message)
{
	char sprint_buf[128];
	sprintf_s(sprint_buf, 128, "%s|%d:%d: %s", module, line, col, message);
	lt_error(vm, sprint_buf);
}

static void _lt_parse_error(lt_VM* vm, const char* module, lt_Token* t, const char* message)
{
	char sprint_buf[128];
	sprintf_s(sprint_buf, 128, "%s|%d:%d: %s", module, t->line, t->col, message);
	lt_error(vm, sprint_buf);
}

static lt_DebugInfo* _lt_get_debuginfo(lt_Object* obj)
{
	switch (obj->type)
	{
	case LT_OBJECT_CHUNK: return obj->chunk.debug;
	case LT_OBJECT_FN: return obj->fn.debug;
	case LT_OBJECT_CLOSURE: return LT_GET_OBJECT(obj->closure.function)->fn.debug;
	}

	return 0;
}

static lt_DebugLoc _lt_get_location(lt_DebugInfo* info, uint32_t pc)
{
	if (info) return *(lt_DebugLoc*)lt_buffer_at(&info->locations, pc);
	return (lt_DebugLoc){ 0, 0 };
}

void lt_runtime_error(lt_VM* vm, const char* message)
{
	char sprint_buf[1024];

	lt_Frame* topmost = &vm->callstack[vm->depth - 1];
	lt_DebugInfo* info = _lt_get_debuginfo(topmost->callee);
	lt_DebugLoc loc = _lt_get_location(info, topmost->ip - (lt_Op*)topmost->code->data);

	const char* name = "<unknown>";
	if (info) name = info->module_name;

	uint32_t len = sprintf_s(sprint_buf, 1024, "%s|%d:%d: %s\ntraceback:", name, loc.line, loc.col, message);
	for (uint32_t i = vm->depth - 1; i >= 0; --i)
	{
		lt_Frame* frame = &vm->callstack[i];
		lt_DebugInfo* info = _lt_get_debuginfo(frame->callee);
		lt_DebugLoc loc = _lt_get_location(info, frame->ip - (lt_Op*)frame->code->data);

		const char* name = "<unknown>";
		if (info) name = info->module_name;
		len = sprintf_s(sprint_buf + len, 1024 - len, "\n(%s|%d:%d)", name, loc.line, loc.col);
	}

	lt_error(vm, sprint_buf);
}

lt_Value lt_make_string(lt_VM* vm, const char* string)
{
	uint32_t len = (uint32_t)strlen(string);
	uint64_t hash = MurmurOAAT64(string);
	uint16_t bucket = hash % LT_DEDUP_TABLE_SIZE;

	lt_Buffer* buf = vm->strings + bucket;
	if (buf->element_size == 0) *buf = lt_buffer_new(sizeof(lt_StringDedupEntry));

	int32_t first_empty = -1;
	for (uint32_t i = 0; i < buf->length; i++)
	{
		lt_StringDedupEntry* entry = lt_buffer_at(buf, i);
		if (entry->hash == hash)
		{
			return entry->value;
		}
		else if (entry->hash == 0 && first_empty == -1) first_empty = i;
	}

	lt_StringDedupEntry new_entry;
	new_entry.hash = hash;
	new_entry.len = len;
	new_entry.refcount = 0;

	new_entry.string = vm->alloc(len + 1);
	memcpy(new_entry.string, string, len);
	new_entry.string[len] = 0;

	uint32_t index = 0;
	if (first_empty != -1)
	{
		index = first_empty;
		lt_StringDedupEntry* e = lt_buffer_at(buf, first_empty);
		new_entry.value = (LT_NAN_MASK | LT_TYPE_STRING) | (bucket << 24) | (index & 0xFFFFFF);
		memcpy(e, &new_entry, sizeof(lt_StringDedupEntry));
		return new_entry.value;
	}
	else
	{
		lt_buffer_push(vm, buf, &new_entry);
		lt_StringDedupEntry* e = lt_buffer_at(buf, buf->length - 1);
		index = buf->length - 1;
		e->value = (LT_NAN_MASK | LT_TYPE_STRING) | (bucket << 24) | (index & 0xFFFFFF);
		return e->value;
	}
}

const char* lt_get_string(lt_VM* vm, lt_Value value)
{
	uint16_t bucket = (uint16_t)((value & 0xFFFFFF000000) >> 24);
	uint32_t index = value & 0xFFFFFF;
	return ((lt_StringDedupEntry*)lt_buffer_at(vm->strings + bucket, index))->string;
}

static void _lt_reference_string(lt_VM* vm, lt_Value value)
{
	uint16_t bucket = (uint16_t)((value & 0xFFFFFF000000) >> 24);
	uint32_t index = value & 0xFFFFFF;
	((lt_StringDedupEntry*)lt_buffer_at(vm->strings + bucket, index))->refcount++;
}

static uint8_t faststrcmp(const char* a, uint64_t a_len, const char* b, uint64_t b_len)
{
	if (a_len != b_len) return 0;
	for (int i = 0; i < a_len; ++i)
	{
		if (*(a + i) != *(b + i)) return 0;
	}

	return 1;
}

uint8_t lt_equals(lt_Value a, lt_Value b)
{
	if (LT_IS_NUMBER(a) != LT_IS_NUMBER(b) || (a & LT_TYPE_MASK) != (b & LT_TYPE_MASK)) return 0;
	switch (a & LT_TYPE_MASK)
	{
	case LT_TYPE_NULL:
	case LT_TYPE_BOOL:
	case LT_TYPE_STRING:
		return a == b;
	
	case LT_TYPE_OBJECT: {
		lt_Object* obja = LT_GET_OBJECT(a);
		lt_Object* objb = LT_GET_OBJECT(b);
		if (obja->type != objb->type) return 0;

		switch (obja->type)
		{
		case LT_OBJECT_CHUNK:
		case LT_OBJECT_CLOSURE:
		case LT_OBJECT_FN:
		case LT_OBJECT_TABLE:
		case LT_OBJECT_NATIVEFN:
			return obja == objb;
		}
	} break;
	}

	return 0;
}

lt_Tokenizer lt_tokenize(lt_VM* vm, const char* source, const char* mod_name)
{
	lt_Tokenizer t;
	t.module = mod_name;
	t.is_valid = 0;
	t.source = source;
	t.token_buffer = lt_buffer_new(sizeof(lt_Token));
	t.identifier_buffer = lt_buffer_new(sizeof(lt_Identifier));
	t.literal_buffer = lt_buffer_new(sizeof(lt_Literal));

	if (!setjmp(*(jmp_buf*)vm->error_buf))
	{
		const char* current = source;
		uint16_t line = 1, col = 0;

#define PUSH_TOKEN(new_type) { \
	lt_Token _t; _t.type = new_type; _t.line = line; _t.col = col++; _t.idx = 0; \
	lt_buffer_push(vm, &t.token_buffer, &_t); current++; found = 1;\
};

		while (*current)
		{
			uint8_t found = 0;
			switch (*current)
			{
			case ' ': case '\t': { col++; current++; } found = 1; break;
			case '\n': { col = 0; line++; current++; } found = 1; break;
			case '\r': { current++; } found = 1; break;
			case ';': { while (*current++ != '\n'); col = 1; line++; found = 1; } break;
			case '.': PUSH_TOKEN(LT_TOKEN_PERIOD)		   break;
			case ',': PUSH_TOKEN(LT_TOKEN_COMMA)		   break;
			case ':': PUSH_TOKEN(LT_TOKEN_COLON)		   break;
			case '(': PUSH_TOKEN(LT_TOKEN_OPENPAREN)	   break;
			case ')': PUSH_TOKEN(LT_TOKEN_CLOSEPAREN)	   break;
			case '[': PUSH_TOKEN(LT_TOKEN_OPENBRACKET)	   break;
			case ']': PUSH_TOKEN(LT_TOKEN_CLOSEBRACKET)	   break;
			case '{': PUSH_TOKEN(LT_TOKEN_OPENBRACE)	   break;
			case '}': PUSH_TOKEN(LT_TOKEN_CLOSEBRACE)	   break;
			case '+': PUSH_TOKEN(LT_TOKEN_PLUS)			   break;
			case '-': PUSH_TOKEN(LT_TOKEN_MINUS)		   break;
			case '*': PUSH_TOKEN(LT_TOKEN_MULTIPLY)		   break;
			case '/': PUSH_TOKEN(LT_TOKEN_DIVIDE)		   break;
			case '=': PUSH_TOKEN(LT_TOKEN_ASSIGN)		   break;
			}

			if (!found)
			{
				if (*current == '>')
				{
					if (*(current + 1) == '=')
					{
						current++;
						PUSH_TOKEN(LT_TOKEN_GTE);
					}
					else PUSH_TOKEN(LT_TOKEN_GT);
				}
				else if (*current == '<')
				{
					if (*(current + 1) == '=')
					{
						current++;
						PUSH_TOKEN(LT_TOKEN_LTE);
					}
					else PUSH_TOKEN(LT_TOKEN_LT);
				}
				else if (*current == '"')
				{
					const char* start = ++current;
					while (*current++ != '"') if (*current == '\n') { col = 0; line++; }

					uint32_t length = (uint32_t)(current - start - 1);

					lt_Literal newlit;
					newlit.type = LT_TOKEN_STRING_LITERAL;
					newlit.string = vm->alloc(length + 1);
					strncpy_s(newlit.string, length + 1, start, length);
					newlit.string[length] = 0;

					lt_buffer_push(vm, &t.literal_buffer, &newlit);

					lt_Token tok;
					tok.type = LT_TOKEN_STRING_LITERAL;
					tok.line = line;
					tok.col = col; col += length;
					tok.idx = t.literal_buffer.length - 1;
					lt_buffer_push(vm, &t.token_buffer, &tok);
				}
				else if (isdigit(*current))
				{
					const char* start = current;
					uint8_t has_decimal = 0;

					while ((isalnum(*current) && !isalpha(*current)) || *current == '.')
					{
						if (*current == '.')
						{
							if (has_decimal) _lt_tokenize_error(vm, t.module, line, col, "Can't have multiple decimals in number literal!");
							has_decimal = 1;
						}

						current++;
					}

					uint32_t length = (uint32_t)(current - start);
					char* end = 0;
					double number = strtod(start, &end);

					if (end != current) _lt_tokenize_error(vm, t.module, line, col, "Failed to parse number!");

					lt_Literal newlit;
					newlit.type = LT_TOKEN_NUMBER_LITERAL;
					newlit.number = number;

					lt_buffer_push(vm, &t.literal_buffer, &newlit);

					lt_Token tok;
					tok.type = LT_TOKEN_NUMBER_LITERAL;
					tok.line = line;
					tok.col = col; col += length;
					tok.idx = t.literal_buffer.length - 1;
					lt_buffer_push(vm, &t.token_buffer, &tok);
				}
				else if (isalpha(*current) || *current == '_')
				{
					const char* start = current;
					uint8_t search = 1;
					while (search)
					{
						current++;
						if (!isalnum(*current) && *current != '_')
						{
							search = 0;
						}
					}

					uint16_t length = (uint16_t)(current - start);

#define PUSH_STR_TOKEN(name, new_type) \
	if(faststrcmp(name, sizeof(name) - 1, start, length)) { \
		lt_Token _t; _t.type = new_type; _t.line = line; _t.col = col; col += length; _t.idx = 0; \
		lt_buffer_push(vm, &t.token_buffer, &_t); found = 1; }

				PUSH_STR_TOKEN("fn", LT_TOKEN_FN)
				else PUSH_STR_TOKEN("var", LT_TOKEN_VAR)
				else PUSH_STR_TOKEN("if", LT_TOKEN_IF)
				else PUSH_STR_TOKEN("else", LT_TOKEN_ELSE)
				else PUSH_STR_TOKEN("elseif", LT_TOKEN_ELSEIF)
				else PUSH_STR_TOKEN("for", LT_TOKEN_FOR)
				else PUSH_STR_TOKEN("in", LT_TOKEN_IN)
				else PUSH_STR_TOKEN("while", LT_TOKEN_WHILE)
				else PUSH_STR_TOKEN("break", LT_TOKEN_BREAK)
				else PUSH_STR_TOKEN("return", LT_TOKEN_RETURN)
				else PUSH_STR_TOKEN("is", LT_TOKEN_EQUALS)
				else PUSH_STR_TOKEN("isnt", LT_TOKEN_NOTEQUALS)
				else PUSH_STR_TOKEN("and", LT_TOKEN_AND)
				else PUSH_STR_TOKEN("or", LT_TOKEN_OR)
				else PUSH_STR_TOKEN("not", LT_TOKEN_NOT)
				else PUSH_STR_TOKEN("true", LT_TOKEN_TRUE_LITERAL)
				else PUSH_STR_TOKEN("false", LT_TOKEN_FALSE_LITERAL)
				else PUSH_STR_TOKEN("null", LT_TOKEN_NULL_LITERAL)

				if (!found)
				{
					for (uint32_t i = 0; i < t.identifier_buffer.length; i++)
					{
						lt_Identifier* id = lt_buffer_at(&t.identifier_buffer, i);
						if (faststrcmp(start, length, id->name, strlen(id->name)))
						{
							found = 1;
							id->num_references++;

							lt_Token tok;
							tok.type = LT_TOKEN_IDENTIFIER;
							tok.line = line;
							tok.col = col; col += length;
							tok.idx = i;
							lt_buffer_push(vm, &t.token_buffer, &tok);
							break;
						}
					}

					if (!found)
					{
						lt_Identifier newid;
						newid.num_references = 1;
						newid.name = vm->alloc(length + 1);
						strncpy_s(newid.name, length + 1, start, length);
						newid.name[length] = 0;

						lt_buffer_push(vm, &t.identifier_buffer, &newid);

						lt_Token tok;
						tok.type = LT_TOKEN_IDENTIFIER;
						tok.line = line;
						tok.col = col; col += length;
						tok.idx = t.identifier_buffer.length - 1;
						lt_buffer_push(vm, &t.token_buffer, &tok);
					}
				}
				}
				else _lt_tokenize_error(vm, t.module, line, col, "Unrecognized token!");
			}
		}

		lt_Token tok;
		tok.type = LT_TOKEN_END;
		tok.line = line;
		tok.col = col;
		lt_buffer_push(vm, &t.token_buffer, &tok);
	
		t.is_valid = 1;
	}
	return t;
}

lt_AstNode* _lt_get_node_of_type(lt_VM* vm, lt_Token* current, lt_Parser* p, lt_AstNodeType type)
{
	lt_AstNode* new_node = vm->alloc(sizeof(lt_AstNode));
	memset(new_node, 0, sizeof(lt_AstNode));
	new_node->type = type;
	new_node->loc.line = current->line;
	new_node->loc.col = current->col;

	lt_buffer_push(vm, &p->ast_nodes, &new_node);
	return *(void**)lt_buffer_last(&p->ast_nodes);
}

uint32_t _lt_tokens_equal(lt_Token* a, lt_Token* b)
{
	return (a->type == LT_TOKEN_IDENTIFIER && b->type == LT_TOKEN_IDENTIFIER && a->idx == b->idx);
}

uint16_t _lt_make_local(lt_VM* vm, lt_Scope* scope, lt_Token* t)
{
	lt_Scope* current = scope;
	
	for (uint32_t i = 0; i < current->locals.length; ++i)
	{
		if (_lt_tokens_equal((lt_Token*)lt_buffer_at(&current->locals, i), t)) return i;
	}

	lt_buffer_push(vm, &current->locals, t);
	return current->locals.length - 1;
}


#define UPVAL_BIT 0x07000000
#define NOT_FOUND ((uint32_t)-1)

uint32_t _lt_find_local(lt_VM* vm, lt_Scope* scope, lt_Token* t)
{
	lt_Scope* current = scope;
	
	for (uint32_t i = 0; i < current->locals.length; ++i)
		if (_lt_tokens_equal((lt_Token*)lt_buffer_at(&current->locals, i), t)) return i;

	for (uint32_t i = 0; i < current->upvals.length; ++i)
		if (_lt_tokens_equal((lt_Token*)lt_buffer_at(&current->upvals, i), t)) return i | UPVAL_BIT;

	lt_Scope* test = current->last;
	while (test)
	{
		uint8_t found = 0;
		for (uint32_t i = 0; i < test->locals.length; ++i)
		{
			if (_lt_tokens_equal((lt_Token*)lt_buffer_at(&test->locals, i), t)) { found = 1; break; }
		}

		if(!found)
			for (uint32_t i = 0; i < test->upvals.length; ++i)
			{
				if (_lt_tokens_equal((lt_Token*)lt_buffer_at(&test->upvals, i), t)) { found = 1; break; }
			}

		if (found)
		{
			lt_buffer_push(vm, &current->upvals, t);
			return (current->upvals.length - 1) | UPVAL_BIT;
		}

		test = test->last;
	}

	return NOT_FOUND;
}

lt_Token* _lt_parse_expression(lt_VM* vm, lt_Parser* p, lt_Token* start, lt_AstNode* dst);
lt_Scope* _lt_parse_block(lt_VM* vm, lt_Parser* p, lt_Token* start, lt_Buffer* dst, uint8_t expects_terminator, uint8_t makes_scope, lt_Token** argnames)
{
	if (makes_scope)
	{
		lt_Scope* new_scope = vm->alloc(sizeof(lt_Scope));
		new_scope->last = p->current;
		p->current = new_scope;

		new_scope->start = start;
		new_scope->end = start;

		new_scope->locals = lt_buffer_new(sizeof(lt_Token));
		new_scope->upvals = lt_buffer_new(sizeof(lt_Token));

		if (argnames) while (*argnames) _lt_make_local(vm, p->current, *argnames++);
	}

	lt_Token* current = start;

#define PEEK() (current + 1)
#define NEXT() (last = current, current++)

	while (current->type != LT_TOKEN_END)
	{
		switch (current->type)
		{
		case LT_TOKEN_CLOSEBRACE:
			if (expects_terminator) { current++; goto end_block; }
			_lt_parse_error(vm, p->tkn->module, current, "Unexpected closing brace!");
		case LT_TOKEN_END:
			_lt_parse_error(vm, p->tkn->module, current, "Unexpected end of file!");
		case LT_TOKEN_IF: {
			lt_AstNode* if_statement = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_IF);
			current++;
			lt_AstNode* expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
			current = _lt_parse_expression(vm, p, current, expr);

			if (current->type != LT_TOKEN_OPENBRACE) _lt_parse_error(vm, p->tkn->module, current, "Expeceted open brace to follow if expression!");
			current++;

			lt_Buffer body = lt_buffer_new(sizeof(lt_AstNode*));
			_lt_parse_block(vm, p, current, &body, 1, 0, 0);
			current = p->current->end;

			if_statement->branch.expr = expr;
			if_statement->branch.body = body;
			if_statement->branch.next = 0;

			lt_AstNode* last = 0;
			while (current->type == LT_TOKEN_ELSEIF || current->type == LT_TOKEN_ELSE)
			{

				lt_AstNode* node = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
				if (if_statement->branch.next == 0) if_statement->branch.next = node;
				if (last) last->branch.next = node;

				if (current->type == LT_TOKEN_ELSEIF)
				{
					current++;
					node->type = LT_AST_NODE_ELSEIF;

					lt_AstNode* expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
					current = _lt_parse_expression(vm, p, current, expr);

					node->branch.expr = expr;
				}
				else
				{
					current++;
					node->type = LT_AST_NODE_ELSE;
				}

				if (current->type != LT_TOKEN_OPENBRACE) _lt_parse_error(vm, p->tkn->module, current, "Expected open brace to follow else expression!");
				current++;

				lt_Buffer body = lt_buffer_new(sizeof(lt_AstNode*));
				_lt_parse_block(vm, p, current, &body, 1, 0, 0);
				current = p->current->end;

				node->branch.body = body;

				last = node;
			}

			lt_buffer_push(vm, dst, &if_statement);
		} break;

		case LT_TOKEN_FOR: {
			lt_AstNode* for_expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_FOR);
			current++; // eat for

			lt_Token* ident = current++;
			uint16_t iteridx = _lt_make_local(vm, p->current, ident);

			if (current->type != LT_TOKEN_IN) _lt_parse_error(vm, p->tkn->module, current, "Expected 'in' to follow 'for' iterator!");
			current++;

			lt_AstNode* iter_expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
			current = _lt_parse_expression(vm, p, current, iter_expr);

			for_expr->loop.identifier = iteridx;
			
			const char* FOR_ITER_NAME = "__iter";
			uint16_t len = (uint16_t)strlen(FOR_ITER_NAME);

			lt_Identifier newid;
			newid.num_references = 1;
			newid.name = vm->alloc(len + 1);
			strncpy_s(newid.name, len + 1, FOR_ITER_NAME, len);
			newid.name[len] = 0;

			lt_buffer_push(vm, &p->tkn->identifier_buffer, &newid);

			lt_Token tok;
			tok.type = LT_TOKEN_IDENTIFIER;
			tok.line = ident->line;
			tok.col = ident->col;
			tok.idx = p->tkn->identifier_buffer.length - 1;

			for_expr->loop.closureidx = _lt_make_local(vm, p->current, &tok);
			for_expr->loop.iterator = iter_expr;

			if (current->type != LT_TOKEN_OPENBRACE) _lt_parse_error(vm, p->tkn->module, current, "Expected open brace to follow 'for' header!");
			current++;

			lt_Buffer body = lt_buffer_new(sizeof(lt_AstNode*));
			_lt_parse_block(vm, p, current, &body, 1, 0, 0);
			current = p->current->end;
			for_expr->loop.body = body;

			lt_buffer_push(vm, dst, &for_expr);
		} break;

		case LT_TOKEN_WHILE: {
			lt_AstNode* while_expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_WHILE);
			current++; // eat while

			lt_AstNode* iter_expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
			current = _lt_parse_expression(vm, p, current, iter_expr);

			while_expr->loop.iterator = iter_expr;

			if (current->type != LT_TOKEN_OPENBRACE) _lt_parse_error(vm, p->tkn->module, current, "Expected open brace to follow 'while' header!");
			current++;

			lt_Buffer body = lt_buffer_new(sizeof(lt_AstNode*));
			_lt_parse_block(vm, p, current, &body, 1, 0, 0);
			current = p->current->end;
			while_expr->loop.body = body;

			lt_buffer_push(vm, dst, &while_expr);
		} break;

		case LT_TOKEN_RETURN: {
			lt_AstNode* ret = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_RETURN);
			ret->ret.expr = 0;
			current++; // eat 'return'

			lt_AstNode* expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
			lt_Token* new_current = _lt_parse_expression(vm, p, current, expr);
			if (current != new_current)
			{
				ret->ret.expr = expr;
				current = new_current;
			}

			lt_buffer_push(vm, dst, &ret);
		} break;
		case LT_TOKEN_BREAK: {
			lt_AstNode* brk = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_BREAK);
			current++; // eat 'break'

			lt_buffer_push(vm, dst, &brk);
		}
		case LT_TOKEN_VAR: {
			lt_AstNode* declare = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_DECLARE);
			current++;
			if (current->type == LT_TOKEN_IDENTIFIER)
			{
				declare->declare.identifier = current++;
				_lt_make_local(vm, p->current, declare->declare.identifier);
			}
			else _lt_parse_error(vm, p->tkn->module, current, "Expected identifier to follow 'var'!");

			lt_AstNode* rhs = 0;
			if (current->type == LT_TOKEN_ASSIGN)
			{
				current++;
				rhs = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
				current = _lt_parse_expression(vm, p, current, rhs);
			}

			declare->declare.expr = rhs;
			lt_buffer_push(vm, dst, &declare);
		} break;
		default: {
			lt_AstNode* result = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
			current = _lt_parse_expression(vm, p, current, result);

			if (current->type == LT_TOKEN_ASSIGN)
			{
				current++;
				lt_AstNode* expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
				current = _lt_parse_expression(vm, p, current, expr);

				lt_AstNode* lhs = result;

				result = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_ASSIGN);
				result->assign.left = lhs;
				result->assign.right = expr;
			}

			lt_buffer_push(vm, dst, &result);
		} break;
		}
	}
	
end_block:
	p->current->end = current;
	lt_Scope* new_scope = p->current;
	
	if(makes_scope) p->current = p->current->last;
	
	return new_scope;
}

uint8_t _lt_get_prec(lt_TokenType op)
{
	switch (op)
	{
	case LT_TOKEN_NOT: case LT_TOKEN_NEGATE: return 5;
	case LT_TOKEN_MULTIPLY: case LT_TOKEN_DIVIDE: return 4;
	case LT_TOKEN_PLUS: case LT_TOKEN_MINUS: return 3;
	case LT_TOKEN_GT: case LT_TOKEN_GTE: case LT_TOKEN_LT: case LT_TOKEN_LTE: case LT_TOKEN_EQUALS: case LT_TOKEN_NOTEQUALS: return 2;
	case LT_TOKEN_AND: case LT_TOKEN_OR: return 1;
	}

	return 0;
}

#define LT_TOKEN_ANY_LITERAL   \
	 LT_TOKEN_NULL_LITERAL:    \
case LT_TOKEN_FALSE_LITERAL:   \
case LT_TOKEN_TRUE_LITERAL:	   \
case LT_TOKEN_NUMBER_LITERAL:  \
case LT_TOKEN_STRING_LITERAL:  \
case LT_TOKEN_FN

lt_Token* _lt_parse_expression(lt_VM* vm, lt_Parser* p, lt_Token* start, lt_AstNode* dst)
{
	uint8_t n_open = 0;
	lt_Token* last = 0;
	lt_Token* current = start;

	lt_Buffer result = lt_buffer_new(sizeof(lt_AstNode*));
	lt_Buffer operator_stack = lt_buffer_new(sizeof(lt_TokenType));

#define PUSH_EXPR_FROM_OP(op) \
	if (op == LT_TOKEN_NOT || op == LT_TOKEN_NEGATE)                                          \
	{																				          \
		lt_AstNode* unaryop = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_UNARYOP);	  \
		unaryop->unary_op.type = op;											              \
		lt_buffer_push(vm, &result, &unaryop);											      \
	}																				          \
	else																			          \
	{																				          \
		lt_AstNode* binaryop = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_BINARYOP);	  \
		binaryop->binary_op.type = op;											              \
		lt_buffer_push(vm, &result, &binaryop);											      \
	}

#define BREAK_ON_EXPR_BOUNDRY       \
	if (last) switch (last->type)   \
	{								\
		case LT_TOKEN_IDENTIFIER:	\
		case LT_TOKEN_CLOSEBRACE:	\
		case LT_TOKEN_CLOSEBRACKET:	\
		case LT_TOKEN_CLOSEPAREN:	\
		case LT_TOKEN_ANY_LITERAL:	\
			goto expr_end;			\
	}

	while (current->type != LT_TOKEN_END)
	{
		switch (current->type)
		{
		case LT_TOKEN_IDENTIFIER: {
			BREAK_ON_EXPR_BOUNDRY

			lt_AstNode* ident = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_IDENTIFIER);
			ident->identifier.token = NEXT();

			if (_lt_find_local(vm, p->current, ident->identifier.token) == NOT_FOUND) {} // ERROR!

			lt_buffer_push(vm, &result, &ident);
			} break;
		case LT_TOKEN_OPENBRACKET: {
			uint8_t is_index = last != 0;
			if (last) switch(last->type)
			{
			case LT_TOKEN_CLOSEBRACE:
			case LT_TOKEN_CLOSEBRACKET:
			case LT_TOKEN_CLOSEPAREN:
			case LT_TOKEN_IDENTIFIER:
				is_index = 1;
			}

			if (is_index)
			{
				NEXT(); // eat bracket
				lt_AstNode* idx_expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
				current = _lt_parse_expression(vm, p, current, idx_expr);
				if (!PEEK()->type == LT_TOKEN_CLOSEBRACKET) _lt_parse_error(vm, p->tkn->module, current, "Expected closing bracket to follow index expression!");
				NEXT();

				lt_AstNode* source = *(void**)lt_buffer_last(&result); lt_buffer_pop(&result);
				lt_AstNode* index = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_INDEX);
				index->index.source = source;
				index->index.idx = idx_expr;
				lt_buffer_push(vm, &result, &index);
			}
			else
			{
				// array literal
				NEXT(); // eat bracket
				lt_AstNode* arr_expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_ARRAY);
				arr_expr->array.values = lt_buffer_new(sizeof(lt_AstNode*));

				while (current->type != LT_TOKEN_CLOSEBRACKET)
				{
					lt_AstNode* value = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
					current = _lt_parse_expression(vm, p, current, value);
					lt_buffer_push(vm, &arr_expr->array.values, &value);

					if (current->type == LT_TOKEN_COMMA) current++;
				}

				NEXT();
				lt_buffer_push(vm, &result, &arr_expr);
			}
			} break;
		case LT_TOKEN_PERIOD: {
			uint8_t allowed = 0;
			if (last) switch (last->type)
			{
			case LT_TOKEN_CLOSEBRACE:
			case LT_TOKEN_CLOSEBRACKET:
			case LT_TOKEN_CLOSEPAREN:
			case LT_TOKEN_IDENTIFIER:
				allowed = 1;
			}

			if (!allowed) goto expr_end;

			NEXT(); // eat period
			lt_AstNode* idx_expr = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_LITERAL);
			if (current->type != LT_TOKEN_IDENTIFIER) _lt_parse_error(vm, p->tkn->module, current, "Expected identifier to follow '.' operator!");
			idx_expr->literal.token = NEXT();

			lt_AstNode* source = *(void**)lt_buffer_last(&result); lt_buffer_pop(&result);
			lt_AstNode* index = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_INDEX);
			index->index.source = source;
			index->index.idx = idx_expr;
			lt_buffer_push(vm, &result, &index);
		} break;

		case LT_TOKEN_NUMBER_LITERAL: case LT_TOKEN_NULL_LITERAL: case LT_TOKEN_TRUE_LITERAL: case LT_TOKEN_FALSE_LITERAL: case LT_TOKEN_STRING_LITERAL: {
			BREAK_ON_EXPR_BOUNDRY

			lt_AstNode* lit = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_LITERAL);
			lit->literal.token = NEXT();
			lt_buffer_push(vm, &result, &lit);
		} break;

		case LT_TOKEN_PLUS: case LT_TOKEN_MINUS:
		case LT_TOKEN_MULTIPLY: case LT_TOKEN_DIVIDE:
		case LT_TOKEN_EQUALS: case LT_TOKEN_NOTEQUALS:
		case LT_TOKEN_GT: case LT_TOKEN_GTE: case LT_TOKEN_LT: case LT_TOKEN_LTE:
		case LT_TOKEN_AND: case LT_TOKEN_OR: case LT_TOKEN_NOT: {
			lt_TokenType optype = current->type;

			if (optype == LT_TOKEN_MINUS)
			{
				optype = LT_TOKEN_NEGATE;

				if (last) switch (last->type)
				{
				case LT_TOKEN_ANY_LITERAL:
				case LT_TOKEN_IDENTIFIER:
				case LT_TOKEN_CLOSEPAREN:
				case LT_TOKEN_CLOSEBRACKET:
					optype = LT_TOKEN_MINUS;
				}
			}

			while (operator_stack.length > 0)
			{
				if (_lt_get_prec(*(lt_TokenType*)lt_buffer_last(&operator_stack)) > _lt_get_prec(optype))
				{
					lt_TokenType shunted = *(lt_TokenType*)lt_buffer_last(&operator_stack);
					lt_buffer_pop(&operator_stack);

					PUSH_EXPR_FROM_OP(shunted);
				}
				else break;
			}

			lt_buffer_push(vm, &operator_stack, &optype);
			NEXT();
		} break;

		case LT_TOKEN_OPENPAREN: {
			if (last) switch (last->type)
			{
			case LT_TOKEN_CLOSEPAREN:
			case LT_TOKEN_CLOSEBRACE:
			case LT_TOKEN_IDENTIFIER:
			case LT_TOKEN_CLOSEBRACKET: {
				NEXT();
				lt_AstNode* callee = *(lt_AstNode**)lt_buffer_last(&result); lt_buffer_pop(&result);

				lt_AstNode* call = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_CALL);
				uint8_t nargs = 0;

				while (current->type != LT_TOKEN_CLOSEPAREN)
				{
					if (current->type == LT_TOKEN_END) _lt_parse_error(vm, p->tkn->module, current, "Unexpected end of file in expression. (Unclosed parenthesis?)");
					if (current->type == LT_TOKEN_COMMA) NEXT();

					lt_AstNode* arg = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
					current = _lt_parse_expression(vm, p, current, arg);
					call->call.args[nargs++] = arg;
				}

				call->call.callee = callee;
				lt_buffer_push(vm, &result, &call);
				NEXT();
			} break;
			default:
				n_open++;
				lt_buffer_push(vm, &operator_stack, &current->type); NEXT();
			}
		} break;

		case LT_TOKEN_CLOSEPAREN: {
			if (n_open == 0) goto expr_end;
			NEXT();
			while (operator_stack.length > 0)
			{
				lt_TokenType back = *(lt_TokenType*)lt_buffer_last(&operator_stack);

				if (back == LT_TOKEN_OPENPAREN) break;

				lt_buffer_pop(&operator_stack);
				PUSH_EXPR_FROM_OP(back);
			}

			if (operator_stack.length == 0) _lt_parse_error(vm, p->tkn->module, current, "Malformed expression!");
			else
			{
				lt_buffer_pop(&operator_stack);
				n_open--;
			}
		} break;

		case LT_TOKEN_OPENBRACE: {
			BREAK_ON_EXPR_BOUNDRY

			// any time we see this, assume it's a table lieral. all other braces should be handled at block level 
			lt_AstNode* table = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_TABLE);
			table->table.keys = lt_buffer_new(sizeof(lt_AstNode*));
			table->table.values = lt_buffer_new(sizeof(lt_AstNode*));

			NEXT(); // eat brace

			while (current->type != LT_TOKEN_CLOSEBRACE)
			{
				lt_AstNode* key = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_LITERAL);
				key->literal.token = NEXT();

				if (!current->type == LT_TOKEN_COLON) lt_error(vm, "Expected colon to follow table index!");
				NEXT(); // eat colon

				lt_AstNode* value = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_EMPTY);
				current = _lt_parse_expression(vm, p, current, value);

				lt_buffer_push(vm, &table->table.keys, &key);
				lt_buffer_push(vm, &table->table.values, &value);
			}

			NEXT();
			lt_buffer_push(vm, &result, &table);
		} break;

		case LT_TOKEN_FN: {
			BREAK_ON_EXPR_BOUNDRY

			lt_AstNode* func = _lt_get_node_of_type(vm, current, p, LT_AST_NODE_FN);
			NEXT();

			if (current->type != LT_TOKEN_OPENPAREN) _lt_parse_error(vm, p->tkn->module, current, "Expected open parenthesis to follow 'fn'!");
			current++;

			uint8_t nargs = 0;
			while (current->type == LT_TOKEN_IDENTIFIER)
			{
				func->fn.args[nargs++] = current++;
				if (current->type == LT_TOKEN_COMMA) current++;
			}

			if (current->type != LT_TOKEN_CLOSEPAREN) _lt_parse_error(vm, p->tkn->module, current, "Expecetd closing parenthesis to follow argument list!");
			current++;

			if (current->type != LT_TOKEN_OPENBRACE) _lt_parse_error(vm, p->tkn->module, current, "Expected open brace to follow argument list!");
			current++;

			lt_Buffer body = lt_buffer_new(sizeof(lt_AstNode*));
			lt_Scope* fn_scope = _lt_parse_block(vm, p, current, &body, 1, 1, func->fn.args);
			current = fn_scope->end;

			func->fn.scope = fn_scope;
			func->fn.body = body;
			lt_buffer_push(vm, &result, &func);
		} break;

		default: {
			if (last) goto expr_end;
			else _lt_parse_error(vm, p->tkn->module, current, "Malformed expression!");
		}
		}
	}

expr_end:
	while (operator_stack.length > 0)
	{
		lt_TokenType back = *(lt_TokenType*)lt_buffer_last(&operator_stack);
		lt_buffer_pop(&operator_stack);
		PUSH_EXPR_FROM_OP(back);
	}

	lt_Buffer value_stack = lt_buffer_new(sizeof(lt_AstNode*));

	for (uint32_t i = 0; i < result.length; i++)
	{
		lt_AstNode* current = *(lt_AstNode**)lt_buffer_at(&result, i);
		if (current->type == LT_AST_NODE_BINARYOP)
		{
			lt_AstNode* right = *(lt_AstNode**)lt_buffer_last(&value_stack); lt_buffer_pop(&value_stack);
			lt_AstNode* left = *(lt_AstNode**)lt_buffer_last(&value_stack); lt_buffer_pop(&value_stack);

			switch (current->binary_op.type)
			{
			case LT_TOKEN_LT:
			case LT_TOKEN_LTE:
				current->binary_op.type -= 2;
				current->binary_op.left = right;
				current->binary_op.right = left;
				break;
			default:
				current->binary_op.left = left;
				current->binary_op.right = right;
				break;
			}
		}
		else if (current->type == LT_AST_NODE_UNARYOP)
		{
			lt_AstNode* right = *(lt_AstNode**)lt_buffer_last(&value_stack); lt_buffer_pop(&value_stack);
			current->unary_op.expr = right;
		}

		lt_buffer_push(vm, &value_stack, &current);
	}

	if (value_stack.length > 0)
	{
		memcpy(dst, *(void**)lt_buffer_at(&value_stack, 0), sizeof(lt_AstNode));
	}

	lt_buffer_destroy(vm, &result);
	lt_buffer_destroy(vm, &operator_stack);
	lt_buffer_destroy(vm, &value_stack);

	return current;
}

lt_Parser lt_parse(lt_VM* vm, lt_Tokenizer* tkn)
{
	lt_Parser p;
	p.is_valid = 0;

	if (!setjmp(*(jmp_buf*)vm->error_buf))
	{
		p.current = 0;
		p.tkn = tkn;
		p.ast_nodes = lt_buffer_new(sizeof(lt_AstNode*));
		p.root = _lt_get_node_of_type(vm, (lt_Token*)tkn->token_buffer.data, &p, LT_AST_NODE_CHUNK);
		p.root->chunk.body = lt_buffer_new(sizeof(lt_AstNode*));

		lt_Scope* file_scope = _lt_parse_block(vm, &p, tkn->token_buffer.data, &p.root->chunk.body, 0, 1, 0);

		p.root->chunk.scope = file_scope;
		p.is_valid = 1;
	}

	return p;
}

lt_VM* lt_open(lt_AllocFn alloc, lt_FreeFn free, lt_ErrorFn error)
{
	lt_VM* vm = alloc(sizeof(lt_VM));
	memset(vm, 0, sizeof(lt_VM));

	vm->top = vm->stack;
	
	vm->alloc = alloc;
	vm->free = free;
	vm->error = error;
	
	vm->heap = lt_buffer_new(sizeof(lt_Object*));
	vm->keepalive = lt_buffer_new(sizeof(lt_Object*));

	vm->error_buf = malloc(sizeof(jmp_buf));
	vm->generate_debug = 1;

	vm->global = LT_VALUE_OBJECT(lt_allocate(vm, LT_OBJECT_TABLE));
	lt_nocollect(vm, LT_GET_OBJECT(vm->global));
	return vm;
}

void lt_destroy(lt_VM* vm)
{
	lt_buffer_destroy(vm, &vm->keepalive);
	lt_collect(vm);
	vm->free(vm);
}

lt_Object* lt_allocate(lt_VM* vm, lt_ObjectType type)
{
	lt_Object* obj = vm->alloc(sizeof(lt_Object));
	memset(obj, 0, sizeof(lt_Object));
	obj->type = type;

	lt_buffer_push(vm, &vm->heap, &obj);

	return obj;
}

void lt_free(lt_VM* vm, uint32_t heapidx)
{
	lt_Object* obj = *(lt_Object**)lt_buffer_at(&vm->heap, heapidx);

	switch (obj->type)
	{
	case LT_OBJECT_CHUNK: {
		lt_buffer_destroy(vm, &obj->chunk.code);
		lt_buffer_destroy(vm, &obj->chunk.constants);
	} break;
	case LT_OBJECT_CLOSURE: {
		lt_buffer_destroy(vm, &obj->closure.captures);
	} break;
	case LT_OBJECT_FN: {
		lt_buffer_destroy(vm, &obj->fn.code);
		lt_buffer_destroy(vm, &obj->fn.constants);
	} break;
	case LT_OBJECT_TABLE: {
		for (uint8_t i = 0; i < 16; ++i)
			lt_buffer_destroy(vm, obj->table.buckets + i);
	} break;
	case LT_OBJECT_ARRAY: {
		lt_buffer_destroy(vm, &obj->array);
	} break;	
	case LT_OBJECT_PTR: {
		vm->free(obj->ptr);
	} break;
	}

	lt_buffer_cycle(&vm->heap, heapidx);
	vm->free(obj);
}

void lt_nocollect(lt_VM* vm, lt_Object* obj)
{
	lt_buffer_push(vm, &vm->keepalive, &obj);
}

void lt_resumecollect(lt_VM* vm, lt_Object* obj)
{
	for (uint32_t i = 0; i < vm->keepalive.length; i++)
	{
		if ((*(lt_Object**)lt_buffer_at(&vm->keepalive, i)) == obj)
		{
			lt_buffer_cycle(&vm->keepalive, i);
			return;
		}
	}
}

#define MARK(x) (x->markbit = 1)
#define CLEAR(x) (x->markbit = 0)

void lt_sweep(lt_VM* vm, lt_Object* obj);

void lt_sweep_v(lt_VM* vm, lt_Value val)
{
	if (LT_IS_OBJECT(val)) lt_sweep(vm, LT_GET_OBJECT(val));
	else if (LT_IS_STRING(val)) _lt_reference_string(vm, val);
}

void lt_sweep(lt_VM* vm, lt_Object* obj)
{
	CLEAR(obj);
	switch (obj->type)
	{
	case LT_OBJECT_CHUNK: {
		for (uint32_t i = 0; i < obj->chunk.constants.length; ++i)
		{
			lt_sweep_v(vm, *(lt_Value*)lt_buffer_at(&obj->chunk.constants, i));
		}
	} break;
	case LT_OBJECT_CLOSURE: {
		lt_sweep_v(vm, obj->closure.function);
		for (uint32_t i = 0; i < obj->closure.captures.length; ++i)
		{
			lt_sweep_v(vm, *(lt_Value*)lt_buffer_at(&obj->closure.captures, i));
		}
	}
	case LT_OBJECT_FN: {
		for (uint32_t i = 0; i < obj->fn.constants.length; ++i)
		{
			lt_sweep_v(vm, *(lt_Value*)lt_buffer_at(&obj->fn.constants, i));
		}
	} break;
	case LT_OBJECT_TABLE: {
		for (uint16_t i = 0; i < 16; ++i)
		{
			lt_Buffer* bucket = obj->table.buckets + i;
			for (uint32_t j = 0; j < bucket->length; ++j)
			{
				lt_sweep_v(vm, ((lt_TablePair*)lt_buffer_at(bucket, j))->key);
				lt_sweep_v(vm, ((lt_TablePair*)lt_buffer_at(bucket, j))->value);
			}
		}
	} break;
	case LT_OBJECT_ARRAY: {
		for (uint32_t j = 0; j < obj->array.length; ++j)
		{
			lt_sweep_v(vm, ((lt_TablePair*)lt_buffer_at(&obj->array, j))->key);
		}
	} break;
	}
}

uint32_t lt_collect(lt_VM* vm)
{
	uint32_t num_collected = 0;

	for (uint32_t i = 0; i < vm->heap.length; ++i)
	{
		lt_Object* obj = *(lt_Object**)lt_buffer_at(&vm->heap, i);
		MARK(obj);
	}

	for (uint32_t i = 0; i < LT_DEDUP_TABLE_SIZE; i++)
	{
		for (uint32_t j = 0; j < vm->strings[i].length; ++j)
		{
			lt_StringDedupEntry* e = lt_buffer_at(vm->strings + i, j);
			e->refcount = 0;
		}
	}

	for (uint32_t i = 0; i < vm->keepalive.length; ++i)
	{
		lt_sweep(vm, *(lt_Object**)lt_buffer_at(&vm->keepalive, i));
	}

	for (uint32_t i = 0; i < vm->heap.length; ++i)
	{
		lt_Object* obj = *(lt_Object**)lt_buffer_at(&vm->heap, i);
		if (obj->markbit)
		{
			lt_free(vm, i--);
			num_collected++;
		}
	}

	for (uint32_t i = 0; i < LT_DEDUP_TABLE_SIZE; i++)
	{
		for (uint32_t j = 0; j < vm->strings[i].length; ++j)
		{
			lt_StringDedupEntry* e = lt_buffer_at(vm->strings + i, j);
			if (e->refcount == 0 && e->hash != 0)
			{
				vm->free(e->string);
				e->hash = 0; // mark for reopen
			}
		}
	}

	return num_collected;
}

void lt_push(lt_VM* vm, lt_Value val)
{
	(*vm->top++ = (val));
}

lt_Value lt_pop(lt_VM* vm)
{
	return (*(--vm->top));
}

lt_Value lt_at(lt_VM* vm, uint32_t idx)
{
	return *(vm->current->start + idx);
}

void lt_close(lt_VM* vm, uint8_t count)
{
	lt_Object* closure = lt_allocate(vm, LT_OBJECT_CLOSURE);
	closure->closure.captures = lt_buffer_new(sizeof(lt_Value));
	for (int i = 0; i < count; i++)
	{
		lt_Value v = lt_pop(vm);
		lt_buffer_push(vm, &closure->closure.captures, &v);
	}
	closure->closure.function = lt_pop(vm);
	lt_push(vm, LT_VALUE_OBJECT(closure));
}

lt_Value lt_getupval(lt_VM* vm, uint8_t idx)
{
	if (vm->current->upvals == 0) return LT_VALUE_NULL;
	return *(lt_Value*)lt_buffer_at(vm->current->upvals, idx);
}

void lt_setupval(lt_VM* vm, uint8_t idx, lt_Value val)
{
	if (vm->current->upvals == 0) return;
	*(lt_Value*)lt_buffer_at(vm->current->upvals, idx) = val;
}

uint16_t _lt_exec(lt_VM* vm, lt_Value callable, uint8_t argc);

uint16_t lt_exec(lt_VM* vm, lt_Value callable, uint8_t argc)
{
	if (!setjmp(*(jmp_buf*)vm->error_buf))
	{
		return _lt_exec(vm, callable, argc);
	}
	else
	{
		vm->depth = 0;
		vm->top = vm->stack;
		return 0;
	}
}

void lt_error(lt_VM* vm, const char* msg)
{
	if (vm->error) vm->error(vm, msg);
	longjmp(vm->error_buf, 1);
}

uint16_t _lt_exec(lt_VM* vm, lt_Value callable, uint8_t argc)
{
	if (!LT_IS_OBJECT(callable)) return 0;

	lt_Object* callee = LT_GET_OBJECT(callable);

	lt_Frame* frame = &vm->callstack[vm->depth++];
	memset(frame, 0, sizeof(lt_Frame));
	vm->current = frame;

	frame->callee = callee;
	frame->start = vm->top - argc;

	switch (callee->type)
	{
	case LT_OBJECT_CHUNK: {
		frame->code = &callee->chunk.code;
		frame->constants = &callee->chunk.constants;
	} break;
	case LT_OBJECT_FN: {
		frame->code = &callee->fn.code;
		frame->constants = &callee->fn.constants;
	} break;
	case LT_OBJECT_CLOSURE: {
		lt_Object* fn = LT_GET_OBJECT(callee->closure.function);
		frame->upvals = &callee->closure.captures;
		if (fn->type == LT_OBJECT_FN)
		{
			frame->code = &fn->fn.code;
			frame->constants = &fn->fn.constants;
		}
		else
		{
			uint8_t n_return = fn->native(vm, argc);

			--vm->depth;
			vm->current = vm->depth > 0 ? &vm->callstack[vm->depth - 1] : 0;
			return n_return;
		}
	} break;
	case LT_OBJECT_NATIVEFN: {
		uint8_t n_return = callee->native(vm, argc);

		--vm->depth;
		vm->current = vm->depth > 0 ? &vm->callstack[vm->depth - 1] : 0;
		return n_return;
	} break;
	}

	lt_Value retval;
	lt_Value* local_start = vm->stack + argc;
	lt_Op* ip = (lt_Op*)frame->code->data;
	frame->ip = &ip;
#undef NEXT
#define NEXT { ip++; goto inst_loop; }

#define TOP (*(vm->top - 1))
#define PUSH(x) (*vm->top++ = (x))
#define POP() (*(--vm->top))

inst_loop:
	switch (ip->op)
	{
	case LT_OP_NOP: NEXT;
	case LT_OP_PUSH: for (int i = 0; i < ip->arg; ++i) PUSH(LT_VALUE_NULL); NEXT;
	case LT_OP_DUP: PUSH(TOP); NEXT;
	case LT_OP_PUSHC: PUSH(*(lt_Value*)lt_buffer_at(frame->constants, ip->arg)); NEXT;
	case LT_OP_PUSHN: PUSH(LT_VALUE_NULL); NEXT;
	case LT_OP_PUSHT: PUSH(LT_VALUE_TRUE); NEXT;
	case LT_OP_PUSHF: PUSH(LT_VALUE_FALSE); NEXT;

	case LT_OP_MAKET: {
		lt_Value t = LT_VALUE_OBJECT(lt_allocate(vm, LT_OBJECT_TABLE));
		for (uint32_t i = 0; i < (uint32_t)ip->arg; ++i)
		{
			lt_Value value = POP();
			lt_Value key = POP();
			lt_table_set(vm, t, key, value);
		}
		PUSH(t);
	} NEXT;

	case LT_OP_MAKEA: {
		lt_Value a = LT_VALUE_OBJECT(lt_allocate(vm, LT_OBJECT_ARRAY));
		for (uint32_t i = 0; i < (uint32_t)ip->arg; ++i)
		{
			lt_Value value = POP();
			lt_array_push(vm, a, value);
		}
		PUSH(a);
	} NEXT;

	case LT_OP_SETT: {
		lt_Value value = POP();
		lt_Value key = POP();
		lt_Value t = POP();
		if (LT_IS_TABLE(t)) lt_table_set(vm, t, key, value);
		else if (LT_IS_ARRAY(t)) *lt_array_at(t, (uint32_t)lt_get_number(key)) = value;
	} NEXT;

	case LT_OP_GETT: {
		lt_Value key = POP();
		lt_Value t = POP();

		if      (LT_IS_TABLE(t)) PUSH(lt_table_get(vm, t, key));
		else if (LT_IS_ARRAY(t)) PUSH(*lt_array_at(t, (uint32_t)lt_get_number(key)));
		else                     PUSH(LT_VALUE_NULL);
	} NEXT;

	case LT_OP_GETG: PUSH(lt_table_get(vm, vm->global, POP())); NEXT;

	case LT_OP_ADD: TOP = (lt_make_number(VALTONUM(POP()) + VALTONUM(TOP))); NEXT;
	case LT_OP_SUB: TOP = (lt_make_number(VALTONUM(POP()) - VALTONUM(TOP))); NEXT;
	case LT_OP_MUL: TOP = (lt_make_number(VALTONUM(POP()) * VALTONUM(TOP))); NEXT;
	case LT_OP_DIV: TOP = (lt_make_number(VALTONUM(POP()) / VALTONUM(TOP))); NEXT;

	case LT_OP_EQ:  TOP = (lt_equals(POP(), TOP) ? LT_VALUE_TRUE : LT_VALUE_FALSE); NEXT;
	case LT_OP_NEQ: TOP = (lt_equals(POP(), TOP) ? LT_VALUE_FALSE : LT_VALUE_TRUE); NEXT;

	case LT_OP_GT:  TOP = (VALTONUM(POP()) >  VALTONUM(TOP) ? LT_VALUE_TRUE : LT_VALUE_FALSE); NEXT;
	case LT_OP_GTE: TOP = (VALTONUM(POP()) >= VALTONUM(TOP) ? LT_VALUE_TRUE : LT_VALUE_FALSE); NEXT;

	case LT_OP_NEG: TOP = (lt_make_number(VALTONUM(TOP) * -1.0)); NEXT;

	case LT_OP_AND:	PUSH(LT_IS_TRUTHY(POP()) && LT_IS_TRUTHY(POP()) ? LT_VALUE_TRUE : LT_VALUE_FALSE); NEXT;

	case LT_OP_OR: {
		lt_Value left = POP();
		lt_Value right = POP();		                                          
		if      (LT_IS_TRUTHY(left))  PUSH(left);
		else if (LT_IS_TRUTHY(right)) PUSH(right);
		else                          PUSH(LT_VALUE_FALSE);
	} NEXT;

	case LT_OP_NOT: TOP = (LT_IS_TRUTHY(TOP) ? LT_VALUE_FALSE : LT_VALUE_TRUE); NEXT;

	case LT_OP_LOAD: PUSH(local_start[ip->arg]); NEXT;
	case LT_OP_STORE: local_start[ip->arg] = POP(); NEXT;

	case LT_OP_LOADUP: PUSH(*(lt_Value*)lt_buffer_at(frame->upvals, ip->arg)); NEXT;
	case LT_OP_STOREUP: *(lt_Value*)lt_buffer_at(frame->upvals, ip->arg) = POP(); NEXT;

	case LT_OP_CLOSE: {
		lt_Object* closure = lt_allocate(vm, LT_OBJECT_CLOSURE);
		closure->closure.captures = lt_buffer_new(sizeof(lt_Value));
		for (int i = 0; i < ip->arg; i++)
		{
			lt_buffer_push(vm, &closure->closure.captures, &POP());
		}
		closure->closure.function = POP();
		PUSH(LT_VALUE_OBJECT(closure));
	} NEXT;

	case LT_OP_CALL: _lt_exec(vm, POP(), (uint8_t)ip->arg); NEXT;

	case LT_OP_JMP: ip += ip->arg; NEXT;
	case LT_OP_JMPC: {
		lt_Value cond = POP();
		if (!LT_IS_TRUTHY(cond)) ip += ip->arg;
	} NEXT;
	case LT_OP_JMPN: if (POP() == LT_VALUE_NULL) ip += ip->arg; NEXT;

	case LT_OP_RET:
		if (ip->arg) retval = POP();
		vm->top = frame->start;
		--vm->depth;
		vm->current = vm->depth > 0 ? &vm->callstack[vm->depth - 1] : 0;
		if (ip->arg) PUSH(retval);
		return ip->arg;

	default: lt_runtime_error(vm, "VM encountered unknown opcode!");
	}

	return vm->top - (frame->start + argc);
}

#define OP(op) { lt_Op op = { LT_OP_##op, 0 }; lt_buffer_push(vm, code_body, &op); if(debug) { lt_buffer_push(vm, debug, &node->loc); } }
#define OPARG(op, arg) { lt_Op op = { LT_OP_##op, arg }; lt_buffer_push(vm, code_body, &op); if(debug) { lt_buffer_push(vm, debug, &node->loc); } }

uint16_t _lt_push_constant(lt_VM* vm, lt_Buffer* constants, lt_Value constant)
{
	for (uint32_t i = 0; i < constants->length; i++)
	{
		if ((*(lt_Value*)lt_buffer_at(constants, i)) == constant) return i;
	}

	lt_buffer_push(vm, constants, &constant);
	return constants->length - 1;
}

static void _lt_compile_body(lt_VM* vm, lt_Parser* p, const char* name, lt_Buffer* debug, lt_Buffer* ast_body, lt_Scope* scope, lt_Buffer* code_body, lt_Buffer* constants);
static void _lt_compile_node(lt_VM* vm, lt_Parser* p, const char* name, lt_Buffer* debug, lt_AstNode* node, lt_Scope* scope, lt_Buffer* code_body, lt_Buffer* constants);

static void _lt_compile_index(lt_VM* vm, lt_Parser* p, const char* name, lt_Buffer* debug, lt_AstNode* node, lt_Scope* scope, lt_Buffer* code_body, lt_Buffer* constants)
{
	_lt_compile_node(vm, p, name, debug, node->index.source, scope, code_body, constants);
	_lt_compile_node(vm, p, name, debug, node->index.idx, scope, code_body, constants);
}

static void _lt_compile_node(lt_VM* vm, lt_Parser* p, const char* name, lt_Buffer* debug, lt_AstNode* node, lt_Scope* scope, lt_Buffer* code_body, lt_Buffer* constants)
{
	switch (node->type)
	{
	case LT_AST_NODE_LITERAL: {
		lt_Token* t = node->literal.token;
		switch (t->type)
		{
		case LT_TOKEN_NULL_LITERAL: OP(PUSHN); break;
		case LT_TOKEN_TRUE_LITERAL: OP(PUSHT); break;
		case LT_TOKEN_FALSE_LITERAL: OP(PUSHF); break;
		case LT_TOKEN_NUMBER_LITERAL: 
		case LT_TOKEN_STRING_LITERAL: {
			lt_Value con;
			lt_Literal* l = lt_buffer_at(&p->tkn->literal_buffer, t->idx);
			if (l->type == LT_TOKEN_NUMBER_LITERAL) con = LT_VALUE_NUMBER(l->number);
			else
			{
				con = lt_make_string(vm, l->string);
			}

			uint16_t idx = _lt_push_constant(vm, constants, con);
			OPARG(PUSHC, idx);
		} break;
		case LT_TOKEN_IDENTIFIER: {
			lt_Identifier* i = lt_buffer_at(&p->tkn->identifier_buffer, t->idx);
			lt_Value val = lt_make_string(vm, i->name);
			OPARG(PUSHC, _lt_push_constant(vm, constants, val));
		} break;
		}
	} break;
	case LT_AST_NODE_BREAK: {
		OPARG(JMP, 0);
	} break;
	case LT_AST_NODE_TABLE: {
		uint16_t size = node->table.keys.length;

		for (int i = 0; i < size; ++i)
		{
			lt_AstNode* key = *(lt_AstNode**)lt_buffer_at(&node->table.keys, i);
			lt_AstNode* value = *(lt_AstNode**)lt_buffer_at(&node->table.values, i);
			_lt_compile_node(vm, p, name, debug, key, scope, code_body, constants);
			_lt_compile_node(vm, p, name, debug, value, scope, code_body, constants);
		}

		OPARG(MAKET, size);
	} break;

	case LT_AST_NODE_ARRAY: {
		uint16_t size = node->table.keys.length;

		for (int i = size - 1; i >= 0; --i)
		{
			lt_AstNode* value = *(lt_AstNode**)lt_buffer_at(&node->array.values, i);
			_lt_compile_node(vm, p, name, debug, value, scope, code_body, constants);
		}

		OPARG(MAKEA, size);
	} break;

	case LT_AST_NODE_IDENTIFIER: {
		uint32_t idx = _lt_find_local(vm, scope, node->identifier.token);
		if (idx == NOT_FOUND) {
			lt_Identifier* i = lt_buffer_at(&p->tkn->identifier_buffer, node->identifier.token->idx);
			lt_Value val = lt_make_string(vm, i->name);
			OPARG(PUSHC, _lt_push_constant(vm, constants, val));
			OP(GETG);
		}
		else if ((idx & UPVAL_BIT) == UPVAL_BIT)
		{
			OPARG(LOADUP, idx & 0xFFFF);
		}
		else
		{
			OPARG(LOAD, idx & 0xFFFF);
		}
	} break;

	case LT_AST_NODE_INDEX: {
		_lt_compile_index(vm, p, name, debug, node, scope, code_body, constants);
		OP(GETT);
	} break;

	case LT_AST_NODE_BINARYOP: {
		_lt_compile_node(vm, p, name, debug, node->binary_op.right, scope, code_body, constants);
		_lt_compile_node(vm, p, name, debug, node->binary_op.left, scope, code_body, constants);
		switch (node->binary_op.type)
		{
		case LT_TOKEN_PLUS: OP(ADD); break;
		case LT_TOKEN_MINUS: OP(SUB); break;
		case LT_TOKEN_MULTIPLY: OP(MUL); break;
		case LT_TOKEN_DIVIDE: OP(DIV); break;
		case LT_TOKEN_AND: OP(AND); break;
		case LT_TOKEN_OR: OP(OR); break;
		case LT_TOKEN_EQUALS: OP(EQ); break;
		case LT_TOKEN_NOTEQUALS: OP(NEQ); break;
		case LT_TOKEN_GT: OP(GT); break;
		case LT_TOKEN_GTE: OP(GTE); break;
		}
	} break;

	case LT_AST_NODE_UNARYOP: {
		_lt_compile_node(vm, p, name, debug, node->unary_op.expr, scope, code_body, constants);
		switch (node->unary_op.type)
		{
		case LT_TOKEN_NEGATE: OP(NEG); break;
		case LT_TOKEN_NOT: OP(NOT); break;
		}
	} break;

	case LT_AST_NODE_DECLARE: {
		uint16_t idx = _lt_make_local(vm, scope, node->declare.identifier);
		if (node->declare.expr)
		{
			_lt_compile_node(vm, p, name, debug, node->declare.expr, scope, code_body, constants);
			OPARG(STORE, idx);
		}
	} break;

	case LT_AST_NODE_ASSIGN: {
		lt_AstNode* target = node->assign.left;
		if (target->type == LT_AST_NODE_IDENTIFIER)
		{
			_lt_compile_node(vm, p, name, debug, node->assign.right, scope, code_body, constants);
			uint32_t idx = _lt_find_local(vm, scope, target->identifier.token);
			if (idx == NOT_FOUND) _lt_parse_error(vm, name, target->identifier.token, "Can't find local to assign to!");
			else if ((idx & UPVAL_BIT) == UPVAL_BIT) OPARG(STOREUP, idx & 0xFFFF)
			else OPARG(STORE, idx & 0xFFFF);
		}
		else if (target->type == LT_AST_NODE_INDEX)
		{
			_lt_compile_index(vm, p, name, debug, target, scope, code_body, constants);
			_lt_compile_node(vm, p, name, debug, node->assign.right, scope, code_body, constants);
			OP(SETT);
		}
	} break;

	case LT_AST_NODE_FN: {
		lt_Object* fn = lt_allocate(vm, LT_OBJECT_FN);

		uint8_t narg = 0;
		lt_Token** arg = node->fn.args;
		while (*arg) { narg++; arg++; }

		fn->fn.arity = narg;
		fn->fn.code = lt_buffer_new(sizeof(lt_Op));
		fn->fn.constants = lt_buffer_new(sizeof(lt_Value));
		if (vm->generate_debug)
		{
			fn->fn.debug = vm->alloc(sizeof(lt_DebugInfo));
			fn->fn.debug->locations = lt_buffer_new(sizeof(lt_DebugLoc));
			fn->fn.debug->module_name = name;
		}

		lt_Op op = { LT_OP_PUSH, 0 };
		lt_buffer_push(vm, &fn->fn.code, &op);

		_lt_compile_body(vm, p, name, &fn->fn.debug->locations, &node->fn.body, node->fn.scope, &fn->fn.code, &fn->fn.constants);

		lt_Op op2 = { LT_OP_RET, 0 };
		lt_buffer_push(vm, &fn->fn.code, &op2);

		((lt_Op*)lt_buffer_at(&fn->fn.code, 0))->arg = node->fn.scope->locals.length;

		lt_Value as_val = LT_VALUE_OBJECT(fn);
		uint16_t idx = _lt_push_constant(vm, constants, as_val);
		OPARG(PUSHC, idx);

		if (node->fn.scope->upvals.length > 0)
		{
			lt_Buffer* upvals = &node->fn.scope->upvals;
			// this is actually a closure

			for (int i = upvals->length - 1; i >= 0; i--)
			{
				uint32_t idx = _lt_find_local(vm, scope, (lt_Token*)lt_buffer_at(upvals, i));
				if ((idx & UPVAL_BIT) == UPVAL_BIT) OPARG(LOADUP, idx & 0xFFFF)
				else OPARG(LOAD, idx & 0xFFFF);
			}

			OPARG(CLOSE, upvals->length);
		}
	} break;

	case LT_AST_NODE_CALL: {
		lt_AstNode** arg = node->call.args;
		uint8_t narg = 0;
		while (*arg)
		{
			narg++;
			_lt_compile_node(vm, p, name, debug, *arg++, scope, code_body, constants);
		}

		_lt_compile_node(vm, p, name, debug, node->call.callee, scope, code_body, constants);
		OPARG(CALL, narg);
	} break;

	case LT_AST_NODE_RETURN: {
		if (node->ret.expr)
		{
			_lt_compile_node(vm, p, name, debug, node->ret.expr, scope, code_body, constants);
			OPARG(RET, 1);
		}
		else OP(RET);
	} break;

#define REG_JMP() branch_stack[n_branches++] = code_body->length; OP(NOP);

	case LT_AST_NODE_IF: {
		uint8_t n_branches = 0;
		uint32_t branch_stack[32];

		_lt_compile_node(vm, p, name, debug, node->branch.expr, scope, code_body, constants);
		uint32_t jidx = code_body->length;
		OP(JMPC);

		_lt_compile_body(vm, p, name, debug, &node->branch.body, scope, code_body, constants);
		REG_JMP();

		((lt_Op*)lt_buffer_at(code_body, jidx))->arg = code_body->length - jidx - 1;

		uint8_t has_elseif = 0, has_else = 0;

		lt_AstNode* next = node->branch.next;
		while (next)
		{
			if (next->type == LT_AST_NODE_ELSEIF)
			{
				has_elseif = 1;

				if (has_else) lt_error(vm, "'else' must be last in if-chain!");

				_lt_compile_node(vm, p, name, debug, next->branch.expr, scope, code_body, constants);
				uint32_t jidx = code_body->length;
				OP(JMPC);

				_lt_compile_body(vm, p, name, debug, &next->branch.body, scope, code_body, constants);
				REG_JMP();

				((lt_Op*)lt_buffer_at(code_body, jidx))->arg = code_body->length - jidx - 1;
			}
			else
			{
				has_else = 1;

				_lt_compile_body(vm, p, name, debug, &next->branch.body, scope, code_body, constants);
			}

			next = next->branch.next;
		}

		if (has_elseif || has_else)
		{
			for (uint32_t i = 0; i < n_branches; i++)
			{
				uint32_t loc = branch_stack[i];
				*((lt_Op*)lt_buffer_at(code_body, loc)) = (lt_Op) { LT_OP_JMP, code_body->length - loc - 1 };
			}
		}
	} break;

	case LT_AST_NODE_FOR: {
		_lt_compile_node(vm, p, name, debug, node->loop.iterator, scope, code_body, constants);
		OPARG(STORE, node->loop.closureidx);

		uint32_t loop_header = code_body->length;
		OPARG(LOAD, node->loop.closureidx);
		OPARG(CALL, 0);
		OPARG(STORE, node->loop.identifier);
		OPARG(LOAD, node->loop.identifier);
		uint32_t loop_start = code_body->length;
		OPARG(JMPN, 0);

		_lt_compile_body(vm, p, name, debug, &node->loop.body, scope, code_body, constants);
		OPARG(JMP, loop_header - code_body->length - 1);

		lt_Op* cond = lt_buffer_at(code_body, loop_start);
		cond->arg = code_body->length - loop_start - 1;

		for (uint32_t i = loop_start; i < code_body->length; ++i)
		{
			lt_Op* current = lt_buffer_at(code_body, i);
			if (current->op == LT_OP_JMP && current->arg == 0)
				current->arg = code_body->length - i - 1;
		}
	} break;

	case LT_AST_NODE_WHILE: {
		uint32_t loop_header = code_body->length;
		_lt_compile_node(vm, p, name, debug, node->loop.iterator, scope, code_body, constants);
		uint32_t loop_start = code_body->length;
		OPARG(JMPC, 0);

		_lt_compile_body(vm, p, name, debug, &node->loop.body, scope, code_body, constants);
		OPARG(JMP, loop_header - code_body->length - 1);

		lt_Op* cond = lt_buffer_at(code_body, loop_start);
		cond->arg = code_body->length - loop_start - 1;

		for (uint32_t i = loop_start; i < code_body->length; ++i)
		{
			lt_Op* current = lt_buffer_at(code_body, i);
			if (current->op == LT_OP_JMP && current->arg == 0)
				current->arg = code_body->length - i - 1;
		}
	} break;
	}
}

static void _lt_compile_body(lt_VM* vm, lt_Parser* p, const char* name, lt_Buffer* debug, lt_Buffer* ast_body, lt_Scope* scope, lt_Buffer* code_body, lt_Buffer* constants)
{
	for (uint32_t i = 0; i < ast_body->length; i++)
	{
		lt_AstNode* node = *(lt_AstNode**)lt_buffer_at(ast_body, i);
		_lt_compile_node(vm, p, name, debug, node, scope, code_body, constants);
	}
}

lt_Value lt_compile(lt_VM* vm, lt_Parser* p)
{
	lt_Object* chunk = lt_allocate(vm, LT_OBJECT_CHUNK);
	lt_nocollect(vm, chunk);

	chunk->chunk.code = lt_buffer_new(sizeof(lt_Op));
	chunk->chunk.constants = lt_buffer_new(sizeof(lt_Value));
	
	if(p->tkn->module)
	{
		uint32_t len = (uint32_t)strlen(p->tkn->module);
		chunk->chunk.name = vm->alloc(len + 1);
		memcpy(chunk->chunk.name, p->tkn->module, len);
		chunk->chunk.name[len] = 0;
	}

	if (vm->generate_debug)
	{
		chunk->chunk.debug = vm->alloc(sizeof(lt_DebugInfo));
		chunk->chunk.debug->locations = lt_buffer_new(sizeof(lt_DebugLoc));
		chunk->chunk.debug->module_name = chunk->chunk.name;
	}

	lt_Op op = { LT_OP_PUSH, 0 };
	lt_buffer_push(vm, &chunk->chunk.code, &op);

	_lt_compile_body(vm, p, chunk->chunk.name, &chunk->chunk.debug->locations, &p->root->chunk.body, p->root->chunk.scope, &chunk->chunk.code, &chunk->chunk.constants);
	
	lt_Op op2 = { LT_OP_RET, 0 };
	lt_buffer_push(vm, &chunk->chunk.code, &op2);

	((lt_Op*)lt_buffer_at(&chunk->chunk.code, 0))->arg = p->root->chunk.scope->locals.length;

	lt_Value as_val = LT_VALUE_OBJECT(chunk);
	return as_val;
}

void lt_free_scope(lt_VM* vm, lt_Scope* scope)
{
	lt_buffer_destroy(vm, &scope->locals);
	lt_buffer_destroy(vm, &scope->upvals);
}

void lt_free_parser(lt_VM* vm, lt_Parser* p)
{
	for (uint32_t i = 0; i < p->ast_nodes.length; i++)
	{
		lt_AstNode* entry = *(lt_AstNode**)lt_buffer_at(&p->ast_nodes, i);

		switch (entry->type)
		{
		case LT_AST_NODE_CHUNK: lt_buffer_destroy(vm, &entry->chunk.body); lt_free_scope(vm, entry->chunk.scope); break;
		case LT_AST_NODE_TABLE: lt_buffer_destroy(vm, &entry->table.keys); lt_buffer_destroy(vm, &entry->table.values); break;
		case LT_AST_NODE_FN: /*lt_buffer_destroy(vm, &entry->fn.body);*/ lt_free_scope(vm, entry->fn.scope); break;
		case LT_AST_NODE_IF: case LT_AST_NODE_ELSEIF: case LT_AST_NODE_ELSE: lt_buffer_destroy(vm, &entry->branch.body); break;
		}

		vm->free(entry);
	}

	lt_buffer_destroy(vm, &p->ast_nodes);
}

void lt_free_tokenizer(lt_VM* vm, lt_Tokenizer* tok)
{
	lt_buffer_destroy(vm, &tok->token_buffer);

	for (uint32_t i = 0; i < tok->identifier_buffer.length; ++i)
		vm->free(((lt_Identifier*)lt_buffer_at(&tok->identifier_buffer, i))->name);
	lt_buffer_destroy(vm, &tok->identifier_buffer);


	for (uint32_t i = 0; i < tok->literal_buffer.length; ++i)
	{
		lt_Literal* lit = lt_buffer_at(&tok->literal_buffer, i);
		if (lit->type == LT_TOKEN_STRING_LITERAL)
		{
			vm->free(lit->string);
		}
	}
	lt_buffer_destroy(vm, &tok->literal_buffer);
}

lt_Value lt_loadstring(lt_VM* vm, const char* source, const char* mod_name)
{
	lt_Tokenizer tok = lt_tokenize(vm, source, mod_name);
	if (!tok.is_valid) 
	{
		lt_free_tokenizer(vm, &tok);
		return LT_VALUE_NULL;
	}

	lt_Parser p = lt_parse(vm, &tok);
	if (!p.is_valid)
	{
		lt_free_parser(vm, &p);
		return LT_VALUE_NULL;
	}

	lt_Value c = lt_compile(vm, &p);

	lt_free_parser(vm, &p);
	lt_free_tokenizer(vm, &tok);

	return c;
}

uint32_t lt_dostring(lt_VM* vm, const char* source, const char* mod_name)
{
	lt_Value callable = lt_loadstring(vm, source, mod_name);
	return callable == LT_VALUE_NULL ? 0 : lt_exec(vm, callable, 0);
}

#define HASH(x) (LT_IS_OBJECT(x) ? ((x >> 2) % 16) : (x % 16))

lt_TablePair* _lt_table_index(lt_VM* vm, lt_Value table, lt_Value key, uint8_t alloc)
{
	uint8_t bucket = HASH(key);
	lt_Buffer* buf = LT_GET_OBJECT(table)->table.buckets + bucket;
	if (alloc && buf->element_size == 0) *buf = lt_buffer_new(sizeof(lt_TablePair));

	for (uint32_t i = 0; i < buf->length; i++)
	{
		lt_TablePair* p = lt_buffer_at(buf, i);
		if (lt_equals(p->key, key))
		{
			return p;
		}
	}

	return 0;
}

lt_Value lt_make_table(lt_VM* vm)
{
	return LT_VALUE_OBJECT(lt_allocate(vm, LT_OBJECT_TABLE));
}

lt_Value lt_table_set(lt_VM* vm, lt_Value table, lt_Value key, lt_Value val)
{
	if (!LT_IS_TABLE(table)) return LT_VALUE_NULL;
	lt_TablePair* p = _lt_table_index(vm, table, key, 1);
	if (p)
	{
		p->value = val;
		return val;
	}

	uint8_t bucket = HASH(key);
	lt_Buffer* buf = LT_GET_OBJECT(table)->table.buckets + bucket;
	lt_TablePair newpair = { key, val };
	lt_buffer_push(vm, buf, &newpair);
	return val;
}

lt_Value lt_table_get(lt_VM* vm, lt_Value table, lt_Value key)
{
	lt_TablePair* p = _lt_table_index(vm, table, key, 0);
	if (p) return p->value;
	return LT_VALUE_NULL;
}

uint8_t lt_table_pop(lt_VM* vm, lt_Value table, lt_Value key)
{
	return lt_table_set(vm, table, key, LT_VALUE_NULL) == LT_VALUE_NULL;
}

lt_Value lt_make_array(lt_VM* vm)
{
	return LT_VALUE_OBJECT(lt_allocate(vm, LT_OBJECT_ARRAY));
}

lt_Value lt_array_push(lt_VM* vm, lt_Value array, lt_Value val)
{
	if (!LT_IS_ARRAY(array)) return LT_VALUE_NULL;
	lt_Object* arr = LT_GET_OBJECT(array);
	if (arr->array.element_size == 0) arr->array = lt_buffer_new(sizeof(lt_Value));
	lt_buffer_push(vm, &arr->array, &val);
	return val;
}

lt_Value* lt_array_at(lt_Value array, uint32_t idx)
{
	if (!LT_IS_ARRAY(array)) return &LT_NULL;
	lt_Object* arr = LT_GET_OBJECT(array);
	return lt_buffer_at(&arr->array, idx);
}

lt_Value lt_array_remove(lt_VM* vm, lt_Value array, uint32_t idx)
{
	if (!LT_IS_ARRAY(array)) return LT_VALUE_NULL;
	lt_Object* arr = LT_GET_OBJECT(array);
	lt_Value old = *(lt_Value*)lt_buffer_at(&arr->array, idx);
	lt_buffer_cycle(&arr->array, idx);
	return old;
}

uint32_t lt_array_length(lt_Value array)
{
	if (!LT_IS_ARRAY(array)) return 0;
	lt_Object* arr = LT_GET_OBJECT(array);
	return arr->array.length;
}

lt_Value lt_make_native(lt_VM* vm, lt_NativeFn fn)
{
	lt_Object* obj = lt_allocate(vm, LT_OBJECT_NATIVEFN);
	obj->native = fn;
	return LT_VALUE_OBJECT(obj);
}

lt_Value lt_make_ptr(lt_VM* vm, void* ptr)
{
	lt_Object* obj = lt_allocate(vm, LT_OBJECT_PTR);
	obj->ptr = ptr;
	return LT_VALUE_OBJECT(obj);
}

void* lt_get_ptr(lt_Value ptr)
{
	lt_Object* obj = LT_GET_OBJECT(ptr);
	return obj->ptr;
}
