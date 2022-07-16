# little - C API overview

## VM manipulation
```c
lt_VM* lt_open(lt_AllocFn, lt_FreeFn, lt_ErrorFn);
```
Creates a new little VM, allocating itself with the `lt_AllocFn` provided. This and `lt_FreeFn` have the same signatures as `malloc` and `free`, so they make for good defaults. 

`lt_ErrorFn` has the signature `void (*lt_ErrorFn)(lt_VM* vm, const char* message)`, and is called whenever the VM encounters an error. `0` can be passed if desired.

The defines `LT_STACK_SIZE 256`, `LT_CALLSTACK_SIZE 32` and `LT_DEDUP_TABLE_SIZE 64` can be set prior to including `little.h` to configure VM internals.

Additionally, the `vm->generate_debug` flag can be set to `0` to disable the generation of debug symbols for traceback, saving some memory.

---
```c
void lt_destroy(lt_VM*);
```
Destroys the VM, clearing the keepalive list, collecting all objects, and freeing it with the function provided when opened.

---
```c
void lt_nocollect(lt_VM*, lt_Object*);
```
Adds object to the VM's root object set, preventing it from being collected.

---
```c
void lt_resumecollect(lt_VM*, lt_Object*);
```
Removes the object from the root set, resuming collection for it.

---
```c
uint32_t lt_collect(lt_VM*);
```
Perform a mark-and-sweep collection pass over the VM's heap.

---
```c
void lt_push(lt_VM*, lt_Value);
```
Pushes a value to the VM's stack.

---
```c
lt_Value lt_pop(lt_VM*);
```
Pops the top value from the VM's stack to the caller

---
```c
lt_Value lt_at(lt_VM*, uint16_t);
```
Returns the N'th element of the current stack frame, useful for arg handling in C.

---
```c
void lt_close(lt_VM*, uint8_t);
```
Captures and closes over N value on the stack, followed by a function, and pushes a closure to it.

---
```c
lt_Value lt_getupval(lt_VM*, uint8_t);
```
Returns the N'th upvalue in the current execution frame.

---
```c
void lt_setupval(lt_VM*, uint8_t, lt_Value);
```
Sets the N'th upvalue in the current execution frame.

---
## Execution

```c
lt_Value lt_loadstring(lt_VM* vm, const char* source, const char* mod_name);
```
Tokenizes, parses, and compiles the source string, passing `mod_name` for debug purposes, and returns the resulting callable chunk.

---
```c
uint32_t lt_dostring(lt_VM* vm, const char* source, const char* mod_name);
```
Tokenizes, parses, and compiles the source string, passing `mod_name` for debug purposes. Then executes the resulting chunk, and returns the number of values returns onto the VM stack.

---
```c
lt_Tokenizer lt_tokenize(lt_VM* vm, const char* source, const char* mod_name);
```
Tokenizes the passed source string, with `mod_name` for debug purposes, and returns the resulting tokenizer.

---
```c
lt_Parser lt_parse(lt_VM* vm, lt_Tokenizer* tkn);
```
Parses the source string tokenized within `tkn`, and returns the resulting parse tree.

---
```c
lt_Value lt_compile(lt_VM* vm, lt_Parser* p);
```
Compiles a parse tree into bytecode, returning the resulting callable.

---
```c
void lt_free_parser(lt_VM* vm, lt_Parser* p);
```
Destroy a parser and free its memory.

---
```c
void lt_free_tokenizer(lt_VM* vm, lt_Tokenizer* tok);
```
Destroy a tokenizer and free its memory.

---
## Error handling
```c
void lt_error(lt_VM* vm, const char* msg);
```
Halts tokenizing, parsing, or executation and calls the VM's error callback with msg.

---
```c
void lt_runtime_error(lt_VM* vm, const char* msg);
```
Halts execution, and calls error callback with msg, formatted into a callstack for debugging.
This is the preferred method for native errors.

---
## Value manipulation

The `LT_IS_NULL(x)`, `LT_IS_NUMBER(x)`, `LT_IS_BOOL(x)`, `LT_IS_TRUE(x)`, `LT_IS_FALSE(x)`, `LT_IS_TRUTHY(x)`, `LT_IS_STRING(x)`, `LT_IS_OBJECT(x)`, `LT_IS_TABLE(x)`, `LT_IS_ARRAY(x)`, `LT_IS_FUNCTION(x)`, `LT_IS_CLOSURE(x)`, `LT_IS_NATIVE(x)`, and `LT_IS_PTR(x)`  macros exist to test the type of any given value `x`.

`LT_VALUE_NULL`, `LT_VALUE_FALSE`, and `LT_VALUE_TRUE` are defined as constants.

`LT_VALUE_OBJECT(x)` and `LT_GET_OBJECT(x)` exist to help bit manipulate pointers to objects.

---
The following methods exist to create values of each type:
```c
lt_Value lt_make_number(double n);
lt_Value lt_make_string(lt_VM* vm, const char* string);
lt_Value lt_make_table(lt_VM* vm);
lt_Value lt_make_array(lt_VM* vm);
lt_Value lt_make_native(lt_VM* vm, lt_NativeFn fn);
lt_Value lt_make_ptr(lt_VM* vm, void* ptr);
```

Some values can be easily retrieved as well:
```c
double lt_get_number(lt_Value v);
const char* lt_get_string(lt_VM* vm, lt_Value value);
void* lt_get_ptr(lt_Value ptr);
```

---
Tables can be manipulated with:
```c
lt_Value lt_table_set(lt_VM* vm, lt_Value table, lt_Value key, lt_Value val); 
lt_Value lt_table_get(lt_VM* vm, lt_Value table, lt_Value key);
uint8_t  lt_table_pop(lt_VM* vm, lt_Value table, lt_Value key);
```

---
Arrays can be manipulated with:
```c
lt_Value  lt_array_push(lt_VM* vm, lt_Value array, lt_Value val);
lt_Value* lt_array_at(lt_Value array, uint32_t idx);
lt_Value  lt_array_remove(lt_VM* vm, lt_Value array, uint32_t idx);
uint32_t  lt_array_length(lt_Value array);
```

---
And finally,
```c
uint8_t lt_equals(lt_Value a, lt_Value b);
```
can be used to test two values for equality.