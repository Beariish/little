# C API examples

Here are some samples of how to extend the language through the C API. For brevity, these all assume you've already opened a `lt_VM*` by the name vm.

## Native function binding
```c
    uint8_t my_add(lt_VM* vm, uint8_t argc)
    {
        if(argc != 2) lt_runtime_error(vm, "Expected two arguments!");
        lt_Value right = lt_pop(vm);
        lt_Value left = lt_pop(vm);

        if(!LT_IS_NUMBER(left) || !LT_IS_NUMBER(right))
            lt_runtime_error(vm, "Invalid types!");

        lt_push(vm, lt_make_number(lt_get_number(left) + lt_get_number(right));
        return 1; // we have pushed to the stack!
    }

    lt_table_set(vm, vm->global, lt_make_string(vm, "add"), lt_make_native(vm, my_add));
```

## Native module binding

```c
    lt_Value my_module = lt_make_table(vm);

    lt_table_set(vm, my_module, lt_make_string(vm, "func1"), lt_make_native(vm, my_func1));
    lt_table_set(vm, my_module, lt_make_string(vm, "func2"), lt_make_native(vm, my_func2));
    lt_table_set(vm, my_module, lt_make_string(vm, "func3"), lt_make_native(vm, my_func3));

    lt_table_set(vm, vm->global, lt_make_string(vm, "module"), my_module);
```