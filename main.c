#include <stdio.h>
#include <stdlib.h>

#include "src/little.h"
#include "src/little_std.h"

void error(lt_VM* vm, const char* msg)
{
    printf("LT ERROR: %s\n", msg);
}

int main(int argc, char** argv)
{
    lt_VM* vm = lt_open(malloc, free, error);
    ltstd_open_all(vm);

    uint32_t nreturn = lt_dostring(vm, ""
        "var speak = fn(animal) {                                                                      \n"
        "    if animal is \"cat\" { return \"meow\" }                                                  \n"
        "    elseif animal is \"dog\" { return \"woof\" }                                              \n"
        "    elseif animal is \"mouse\" { return \"squeak\" }                                          \n"
        "    return \"???\"                                                                            \n"
        "}                                                                                             \n"
        "                                                                                              \n"
        "var animals = [\"cat\", \"dog\", \"mouse\", \"monkey\"]                                       \n"
        "    for animal in array.each(animals) {                                                       \n"
        "            io.print(string.format(\"%s says %s!\", animal, speak(animal)))                   \n"
        "}", "module");

    while (nreturn-- > 0)
    {
        printf("Returned: %s\n", ltstd_tostring(vm, lt_pop(vm)));
    }

    lt_destroy(vm);

    return 0;
}