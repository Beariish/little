#include "little_std.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#if defined(__APPLE__)
  #define sprintf_s snprintf
#endif

void ltstd_open_all(lt_VM* vm)
{
	ltstd_open_io(vm);
	ltstd_open_math(vm);
	ltstd_open_array(vm);
    ltstd_open_string(vm);
    ltstd_open_gc(vm);
}

char* ltstd_tostring(lt_VM* vm, lt_Value val)
{
    char scratch[256];
    uint8_t len = 0;

    if (LT_IS_NUMBER(val)) len = sprintf_s(scratch, 256, "%f", LT_GET_NUMBER(val));
    if (LT_IS_NULL(val)) len = sprintf_s(scratch, 256, "null");
    if (LT_IS_TRUE(val)) len = sprintf_s(scratch, 256, "true");
    if (LT_IS_FALSE(val)) len = sprintf_s(scratch, 256, "false");
    if (LT_IS_STRING(val)) len = sprintf_s(scratch, 256, "%s", lt_get_string(vm, val));;

    if (LT_IS_OBJECT(val))
    {
        lt_Object* obj = LT_GET_OBJECT(val);
        switch (obj->type)
        {
        case LT_OBJECT_CHUNK: len = sprintf_s(scratch, 256, "chunk 0x%llx", (uintptr_t)obj); break;
        case LT_OBJECT_CLOSURE: len = sprintf_s(scratch, 256, "closure 0x%llx | %d upvals", (uintptr_t)LT_GET_OBJECT(obj->closure.function), obj->closure.captures.length); break;
        case LT_OBJECT_FN: len = sprintf_s(scratch, 256, "function 0x%llx", (uintptr_t)obj); break;
        case LT_OBJECT_TABLE: len = sprintf_s(scratch, 256, "table 0x%llx", (uintptr_t)obj); break;
        case LT_OBJECT_ARRAY: len = sprintf_s(scratch, 256, "array | %d", lt_array_length(val)); break;
        case LT_OBJECT_NATIVEFN: len = sprintf_s(scratch, 256, "native 0x%llx", (uintptr_t)obj); break;
        }
    }

    char* str = vm->alloc(len + 1);
    memcpy(str, scratch, len);
    str[len] = 0;

    return str;
}

static uint8_t _lt_print(lt_VM* vm, uint8_t argc)
{
    for (int16_t i = argc - 1; i >= 0; --i)
    {
        char* str = ltstd_tostring(vm, *(vm->top - 1 - i));
        printf("%s", str);
        vm->free(str);

        if (i > 0) printf(" ");
    }

    for (int16_t i = argc - 1; i >= 0; --i) lt_pop(vm);

    printf("\n");
    return 0;
}

static uint8_t _lt_clock(lt_VM* vm, uint8_t argc)
{
    if (argc != 0) lt_runtime_error(vm, "Expected no arguments to io.clock!");

    clock_t time = clock();
    double in_seconds = (double)time / (double)CLOCKS_PER_SEC;
    lt_push(vm, lt_make_number(in_seconds));

    return 1;
}

static lt_Value req_table_string;

static uint8_t _lt_require(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected path argument to io.require!");
    lt_Value reqtable = lt_table_get(vm, vm->global, req_table_string);
    if (LT_IS_NULL(reqtable))
    {
        reqtable = lt_make_table(vm);
        lt_table_set(vm, vm->global, req_table_string, reqtable);
    }

    lt_Value path = lt_pop(vm);
    lt_Value result = lt_table_get(vm, reqtable, path);
    if (!LT_IS_NULL(result))
    {
        lt_push(vm, result);
        return 1;
    }

    FILE* fp = fopen(lt_get_string(vm, path), "rb");
    if (!fp) lt_runtime_error(vm, "Failed to open file for require!");
    
    static char text[1 << 20];
    fread(text, 1, sizeof(text), fp);
    fclose(fp);

    uint32_t n_results = lt_dostring(vm, text, lt_get_string(vm, path));
    if (n_results == 1)
    {
        result = lt_pop(vm);
        lt_table_set(vm, reqtable, path, result);
        lt_push(vm, result);
        return 1;
    }
    else
    {
        lt_table_set(vm, reqtable, path, LT_VALUE_TRUE);
        return 0;
    }
}

#define LT_SIMPLE_MATH_FN(name) \
    static uint8_t _lt_##name(lt_VM* vm, uint8_t argc) \
{ \
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to math." #name "!");                         \
    lt_Value arg = lt_pop(vm);                                                                         \
    if (!LT_IS_NUMBER(arg)) lt_runtime_error(vm, "Expected argument to math." #name " to be number!");       \
                                                                                                       \
    lt_push(vm, LT_VALUE_NUMBER(name(LT_GET_NUMBER(arg))));                                             \
    return 1;                                                                                          \
}

LT_SIMPLE_MATH_FN(sin);
LT_SIMPLE_MATH_FN(cos);
LT_SIMPLE_MATH_FN(tan);

LT_SIMPLE_MATH_FN(sinh);
LT_SIMPLE_MATH_FN(cosh);
LT_SIMPLE_MATH_FN(tanh);

LT_SIMPLE_MATH_FN(asin);
LT_SIMPLE_MATH_FN(acos);
LT_SIMPLE_MATH_FN(atan);

LT_SIMPLE_MATH_FN(round);
LT_SIMPLE_MATH_FN(ceil);
LT_SIMPLE_MATH_FN(floor);

LT_SIMPLE_MATH_FN(exp);
LT_SIMPLE_MATH_FN(log);
LT_SIMPLE_MATH_FN(log10);
LT_SIMPLE_MATH_FN(sqrt);
LT_SIMPLE_MATH_FN(fabs);

#define LT_BINARY_MATH_FN(name) \
    static uint8_t _lt_##name(lt_VM* vm, uint8_t argc) \
{ \
    if (argc != 2) lt_runtime_error(vm, "Expected two arguments to math." #name "!");                         \
    lt_Value arg1 = lt_pop(vm);                                                                         \
    lt_Value arg2 = lt_pop(vm);                                                                         \
    if (!LT_IS_NUMBER(arg1) || !LT_IS_NUMBER(arg2)) lt_runtime_error(vm, "Expected argument to math." #name " to be number!");       \
                                                                                                       \
    lt_push(vm, LT_VALUE_NUMBER(name(LT_GET_NUMBER(arg1), LT_GET_NUMBER(arg2))));                                             \
    return 1;                                                                                          \
}

LT_BINARY_MATH_FN(fmin);
LT_BINARY_MATH_FN(fmax);
LT_BINARY_MATH_FN(pow);
LT_BINARY_MATH_FN(fmod);

static uint8_t _lt_array_next(lt_VM* vm, uint8_t argc)
{
    lt_Value current = lt_getupval(vm, 1);
    lt_Value arr = lt_getupval(vm, 0);

    uint32_t idx = lt_get_number(current);
    lt_Value to_return = idx >= lt_array_length(arr) ? LT_VALUE_NULL : *lt_array_at(arr, idx);

    lt_setupval(vm, 1, LT_VALUE_NUMBER(idx + 1));
    lt_push(vm, to_return);

    return 1;
}

static uint8_t _lt_array_each(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to array.each!");

    lt_Value arr = lt_pop(vm);

    if (!LT_IS_ARRAY(arr)) lt_runtime_error(vm, "Expected argument to array.each to be array!");

    uint32_t len = lt_array_length(arr);

    lt_push(vm, lt_make_native(vm, _lt_array_next));
    lt_push(vm, LT_VALUE_NUMBER(0));
    lt_push(vm, arr);
    lt_close(vm, 2);

    return 1;
}

static uint8_t _lt_range_iter(lt_VM* vm, uint8_t argc)
{
    lt_Value start = lt_getupval(vm, 2);
    lt_Value end = lt_getupval(vm, 1);
    lt_Value step = lt_getupval(vm, 0);

    if (lt_get_number(start) >= lt_get_number(end)) { lt_push(vm, LT_VALUE_NULL); return 1; }

    lt_setupval(vm, 2, lt_make_number(lt_get_number(start) + lt_get_number(step)));

    lt_push(vm, start);
    return 1;
}

static uint8_t _lt_range(lt_VM* vm, uint8_t argc)
{
    lt_Value start = LT_VALUE_NUMBER(0);
    lt_Value end = LT_VALUE_NUMBER(0);
    lt_Value step = LT_VALUE_NUMBER(1);

    if (argc == 1)
    {
        end = lt_pop(vm);
    }
    else if (argc == 2)
    {
        end = lt_pop(vm);
        start = lt_pop(vm);
    }
    else if (argc == 3)
    {
        step = lt_pop(vm);
        end = lt_pop(vm);
        start = lt_pop(vm);
    }
    else lt_runtime_error(vm, "Expected 1-3 args for array.range([start,] end [, step]!");

    if (!LT_IS_NUMBER(start) || !LT_IS_NUMBER(end) || !LT_IS_NUMBER(step))
        lt_runtime_error(vm, "Expected all arguments to array.range to be numbers!");

    lt_push(vm, lt_make_native(vm, _lt_range_iter));
    lt_push(vm, start);
    lt_push(vm, end);
    lt_push(vm, step);
    lt_close(vm, 3);

    return 1;
}

static uint8_t _lt_array_len(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to array.len!");
    lt_Value arr = lt_pop(vm);
    if (!LT_IS_ARRAY(arr)) lt_runtime_error(vm, "Expected argument to array.len to be array!");

    lt_push(vm, lt_make_number(lt_array_length(arr)));
    return 1;
}

static uint8_t _lt_array_pop(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to array.pop!");
    lt_Value arr = lt_pop(vm);
    if (!LT_IS_ARRAY(arr)) lt_runtime_error(vm, "Expected argument to array.pop to be array!");

    lt_push(vm, lt_array_remove(vm, arr, lt_array_length(arr) - 1));
    return 1;
}

static uint8_t _lt_array_last(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to array.last!");
    lt_Value arr = lt_pop(vm);
    if (!LT_IS_ARRAY(arr)) lt_runtime_error(vm, "Expected argument to array.last to be array!");

    lt_push(vm, lt_array_at(arr, lt_array_length(arr) - 1));
    return 1;
}

static uint8_t _lt_array_push(lt_VM* vm, uint8_t argc)
{
    if (argc != 2) lt_runtime_error(vm, "Expected two arguments to array.push!");
    lt_Value arr = lt_pop(vm);
    lt_Value val = lt_pop(vm);
    if (!LT_IS_ARRAY(arr)) lt_runtime_error(vm, "Expected first argument to array.push to be array!");

    lt_array_push(vm, arr, val);
    return 0;
}

static uint8_t _lt_array_remove(lt_VM* vm, uint8_t argc)
{
    if (argc != 2) lt_runtime_error(vm, "Expected two arguments to array.remove!");
    lt_Value arr = lt_pop(vm);
    lt_Value idx = lt_pop(vm);
    if (!LT_IS_ARRAY(arr)) lt_runtime_error(vm, "Expected first argument to array.remove to be array!");
    if (!LT_IS_NUMBER(idx)) lt_runtime_error(vm, "Expected second argument to array.remove to be number!");

    lt_array_remove(vm, arr, (uint32_t)lt_get_number(idx));
    return 0;
}

static uint8_t _lt_gc_collect(lt_VM* vm, uint8_t argc)
{
    if (argc != 0) lt_runtime_error(vm, "Expected no arguments to gc.collect!");
    uint32_t num_collected = lt_collect(vm);
    lt_push(vm, LT_VALUE_NUMBER((double)num_collected));
    return 1;
}

static uint8_t _lt_gc_addroot(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to gc.addroot!");
    lt_Value val = lt_pop(vm);
    if (!LT_IS_OBJECT(val)) lt_runtime_error(vm, "Expected argument to gc.addroot to be object!");
    lt_Object* obj = LT_GET_OBJECT(val);
    lt_nocollect(vm, obj);
    return 0;
}

static uint8_t _lt_gc_removeroot(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to gc.removeroot!");
    lt_Value val = lt_pop(vm);
    if (!LT_IS_OBJECT(val)) lt_runtime_error(vm, "Expected argument to gc.removeroot to be object!");
    lt_Object* obj = LT_GET_OBJECT(val);
    lt_resumecollect(vm, obj);
    return 0;
}

static uint8_t _lt_string_from(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to string.from!");
    lt_Value val = lt_pop(vm);
    char* temp = ltstd_tostring(vm, val);
    lt_Value str = lt_make_string(vm, temp);
    vm->free(temp);
    lt_push(vm, str);
    return 1;
}

static uint8_t _lt_string_concat(lt_VM* vm, uint8_t argc)
{
    if (argc < 2) lt_runtime_error(vm, "Expected at least two arguments to string.concat!");

    char* accum = 0; uint32_t len = 0;

    for (int32_t i = argc - 1; i >= 0; --i)
    {
        lt_Value val = *(vm->top - 1 - i);
        if (!LT_IS_STRING(val)) lt_runtime_error(vm, "Non-string argument to string.concat!");
        uint32_t oldlen = len;
        const char* str = lt_get_string(vm, val);

        char* oldaccum = accum;
        len += strlen(str);

        accum = vm->alloc(len + 1);
        if (oldaccum)
        {
            memcpy(accum, oldaccum, oldlen);
            vm->free(oldaccum);
        }

        memcpy(accum + oldlen, str, len - oldlen);
        accum[len] = 0;
    }

    lt_push(vm, lt_make_string(vm, accum));
    vm->free(accum);

    return 1;
}

static uint8_t _lt_string_len(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to string.len!");
    lt_Value val = lt_pop(vm);
    if (!LT_IS_STRING(val)) lt_runtime_error(vm, "Non-string argument to string.len!");
    lt_push(vm, LT_VALUE_NUMBER(strlen(lt_get_string(vm, val))));
    return 1;
}

static uint8_t _lt_string_sub(lt_VM* vm, uint8_t argc)
{
    if (argc < 2) lt_runtime_error(vm, "Expected at least two arguments to string.sub!");
    
    lt_Value len = LT_VALUE_NULL;
    if (argc == 3) len = lt_pop(vm);

    lt_Value start = lt_pop(vm);
    lt_Value str = lt_pop(vm);

    if (!LT_IS_STRING(str)) lt_runtime_error(vm, "Non-string argument to string.sub!");
    if (!LT_IS_NUMBER(start)) lt_runtime_error(vm, "Non-number starting point to string.sub!");
    
    const char* cstr = lt_get_string(vm, str);

    if (!LT_IS_NUMBER(len))
    {
        len = LT_VALUE_NUMBER(strlen(cstr) - start);
    }

    char* newstr = vm->alloc(LT_GET_NUMBER(len) + 1);
    memcpy(newstr, cstr + start, len);

    lt_push(vm, lt_make_string(vm, newstr));
    vm->free(newstr);
    return 1;
}

static uint8_t _lt_string_format(lt_VM* vm, uint8_t argc)
{
    if (argc < 1) lt_runtime_error(vm, "Expected at least a template string to string.format!");
    lt_Value val = *(vm->top - argc);
    if (!LT_IS_STRING(val)) lt_runtime_error(vm, "Non-string argument to string.format!");

    char output[1024];
    char fmtbuf[32];
    uint16_t o_idx = 0;

    const char* format = lt_get_string(vm, val);
    uint8_t current_arg = 1;

    while (*format)
    {
        if (*format == '%')
        {
            if (*(format + 1) == '%')
            {
                output[o_idx++] = '%';
                format += 2;
            }
            else
            {
                uint8_t fmtloc = 0;
                fmtbuf[fmtloc++] = *format++;
                scan_format: switch (*format)
                {
                case 'd': case 'i': {
                    fmtbuf[fmtloc++] = *format++; fmtbuf[fmtloc] = 0;
                    o_idx += sprintf_s(output + o_idx, 1024 - o_idx, fmtbuf, (int32_t)LT_GET_NUMBER(*(vm->top - argc + current_arg++)));
                } break;
                case 'o': case 'u': case 'x': case 'X': {
                    fmtbuf[fmtloc++] = *format++; fmtbuf[fmtloc] = 0;
                    o_idx += sprintf_s(output + o_idx, 1024 - o_idx, fmtbuf, (uint32_t)LT_GET_NUMBER(*(vm->top - argc + current_arg++)));
                } break;
                case 'e': case 'E': case 'f': case 'g': case 'G': {
                    fmtbuf[fmtloc++] = *format++; fmtbuf[fmtloc] = 0;
                    o_idx += sprintf_s(output + o_idx, 1024 - o_idx, fmtbuf, LT_GET_NUMBER(*(vm->top - argc + current_arg++)));
                } break;
                case 's': {
                    fmtbuf[fmtloc++] = *format++; fmtbuf[fmtloc] = 0;
                    o_idx += sprintf_s(output + o_idx, 1024 - o_idx, fmtbuf, lt_get_string(vm, *(vm->top - argc + current_arg++)));
                } break;
                default:
                    fmtbuf[fmtloc++] = *format++;
                    goto scan_format;
                    break;
                }
            }
        }
        else
        {
            output[o_idx++] = *format++;
        }
    }

    output[o_idx] = 0;
    lt_push(vm, lt_make_string(vm, output));
    return 1;
}

static uint8_t _lt_string_typeof(lt_VM* vm, uint8_t argc)
{
    if (argc != 1) lt_runtime_error(vm, "Expected one argument to string.typeof!");
    lt_Value val = lt_pop(vm);
    if (LT_IS_NULL(val)) lt_push(vm, lt_make_string(vm, "null"));
    else if (LT_IS_NUMBER(val)) lt_push(vm, lt_make_string(vm, "number"));
    else if (LT_IS_BOOL(val)) lt_push(vm, lt_make_string(vm, "boolean"));
    else if (LT_IS_STRING(val)) lt_push(vm, lt_make_string(vm, "string"));
    else if (LT_IS_FUNCTION(val)) lt_push(vm, lt_make_string(vm, "function"));
    else if (LT_IS_CLOSURE(val)) lt_push(vm, lt_make_string(vm, "closure"));
    else if (LT_IS_ARRAY(val)) lt_push(vm, lt_make_string(vm, "array"));
    else if (LT_IS_TABLE(val)) lt_push(vm, lt_make_string(vm, "table"));
    else if (LT_IS_NATIVE(val)) lt_push(vm, lt_make_string(vm, "native"));
    else if (LT_IS_PTR(val)) lt_push(vm, lt_make_string(vm, "ptr"));
    return 1;
}

void ltstd_open_io(lt_VM* vm)
{
    req_table_string = lt_make_string(vm, "__require");

	lt_Value t = lt_make_table(vm);
    lt_table_set(vm, t, lt_make_string(vm, "print"), lt_make_native(vm, _lt_print));
    lt_table_set(vm, t, lt_make_string(vm, "clock"), lt_make_native(vm, _lt_clock));
    lt_table_set(vm, t, lt_make_string(vm, "require"), lt_make_native(vm, _lt_require));

    lt_table_set(vm, vm->global, lt_make_string(vm, "io"), t);
}

void ltstd_open_math(lt_VM* vm)
{
    lt_Value t = lt_make_table(vm);
    lt_table_set(vm, t, lt_make_string(vm, "sin"), lt_make_native(vm, _lt_sin));
    lt_table_set(vm, t, lt_make_string(vm, "cos"), lt_make_native(vm, _lt_cos));
    lt_table_set(vm, t, lt_make_string(vm, "tan"), lt_make_native(vm, _lt_tan));

    lt_table_set(vm, t, lt_make_string(vm, "asin"), lt_make_native(vm, _lt_asin));
    lt_table_set(vm, t, lt_make_string(vm, "acos"), lt_make_native(vm, _lt_acos));
    lt_table_set(vm, t, lt_make_string(vm, "atan"), lt_make_native(vm, _lt_atan));

    lt_table_set(vm, t, lt_make_string(vm, "sinh"), lt_make_native(vm, _lt_sinh));
    lt_table_set(vm, t, lt_make_string(vm, "cosh"), lt_make_native(vm, _lt_cosh));
    lt_table_set(vm, t, lt_make_string(vm, "tanh"), lt_make_native(vm, _lt_tanh));
    
    lt_table_set(vm, t, lt_make_string(vm, "floor"), lt_make_native(vm, _lt_floor));
    lt_table_set(vm, t, lt_make_string(vm, "ceil"),  lt_make_native(vm, _lt_ceil));
    lt_table_set(vm, t, lt_make_string(vm, "round"), lt_make_native(vm, _lt_round));
    
    lt_table_set(vm, t, lt_make_string(vm, "exp"),   lt_make_native(vm, _lt_exp));
    lt_table_set(vm, t, lt_make_string(vm, "log"),   lt_make_native(vm, _lt_log));
    lt_table_set(vm, t, lt_make_string(vm, "log10"), lt_make_native(vm, _lt_log10));
    lt_table_set(vm, t, lt_make_string(vm, "sqrt"),  lt_make_native(vm, _lt_sqrt));
    lt_table_set(vm, t, lt_make_string(vm, "abs"),   lt_make_native(vm, _lt_fabs));

    lt_table_set(vm, t, lt_make_string(vm, "min"), lt_make_native(vm, _lt_fmin));
    lt_table_set(vm, t, lt_make_string(vm, "max"), lt_make_native(vm, _lt_fmax));
    lt_table_set(vm, t, lt_make_string(vm, "pow"), lt_make_native(vm, _lt_pow));
    lt_table_set(vm, t, lt_make_string(vm, "mod"), lt_make_native(vm, _lt_fmod));

    lt_table_set(vm, t, lt_make_string(vm, "pi"), LT_VALUE_NUMBER(3.14159265358979323846));
    lt_table_set(vm, t, lt_make_string(vm, "e"), LT_VALUE_NUMBER(2.71828182845904523536));

    lt_table_set(vm, vm->global, lt_make_string(vm, "math"), t);
}

void ltstd_open_array(lt_VM* vm)
{
    lt_Value t = lt_make_table(vm);
    lt_table_set(vm, t, lt_make_string(vm, "each"), lt_make_native(vm, _lt_array_each));
    lt_table_set(vm, t, lt_make_string(vm, "range"), lt_make_native(vm, _lt_range));

    lt_table_set(vm, t, lt_make_string(vm, "len"), lt_make_native(vm, _lt_array_len));
    lt_table_set(vm, t, lt_make_string(vm, "last"), lt_make_native(vm, _lt_array_last));
    lt_table_set(vm, t, lt_make_string(vm, "pop"), lt_make_native(vm, _lt_array_pop));
    lt_table_set(vm, t, lt_make_string(vm, "push"), lt_make_native(vm, _lt_array_push));
    lt_table_set(vm, t, lt_make_string(vm, "remove"), lt_make_native(vm, _lt_array_remove));

    lt_table_set(vm, vm->global, lt_make_string(vm, "array"), t);
}

void ltstd_open_string(lt_VM* vm)
{
    lt_Value t = lt_make_table(vm);

    lt_table_set(vm, t, lt_make_string(vm, "from"), lt_make_native(vm, _lt_string_from));
    lt_table_set(vm, t, lt_make_string(vm, "concat"), lt_make_native(vm, _lt_string_concat));
    lt_table_set(vm, t, lt_make_string(vm, "len"), lt_make_native(vm, _lt_string_len));
    lt_table_set(vm, t, lt_make_string(vm, "sub"), lt_make_native(vm, _lt_string_sub));
    lt_table_set(vm, t, lt_make_string(vm, "format"), lt_make_native(vm, _lt_string_format));

    lt_table_set(vm, vm->global, lt_make_string(vm, "string"), t);
}

void ltstd_open_gc(lt_VM* vm)
{
    lt_Value t = lt_make_table(vm);

    lt_table_set(vm, t, lt_make_string(vm, "collect"), lt_make_native(vm, _lt_gc_collect));
    lt_table_set(vm, t, lt_make_string(vm, "addroot"), lt_make_native(vm, _lt_gc_addroot));
    lt_table_set(vm, t, lt_make_string(vm, "removeroot"), lt_make_native(vm, _lt_gc_removeroot));

    lt_table_set(vm, vm->global, lt_make_string(vm, "gc"), t);
}
