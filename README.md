# little - tiny bytecode language
little is a _small_, _fast_, _easily embeddable_ language implemented in C.

---
```js
var speak = fn(animal) {
    if animal is "cat" { return "meow" }
    elseif animal is "dog" { return "woof" }
    elseif animal is "mouse" { return "squeak" }
    return "???"
}

var animals = [ "cat", "dog", "mouse", "monkey" ]
for animal in array.each(animals) {
    io.print(string.format("%s says %s!", animal, speak(animal)))
}
```
---
## Feature Overview
* Tiny implementation - core langauge is <2500 sloc in a single .h/.c pair
* Light embedding - compiles down to less than 20kb, 3 API calls to get started
* Reasonably fast for realtime applications
* Low memory footprint with simple mark and sweep garbage collector
* Supports null, numbers, booleans, strings, functions, closures, arrays, tables, and native procedures
* Optional, consise stdlib - an extra ~1000 sloc
* Supports 32- and 64-bit, and will likely compile anywhere!
* Feature-rich C api to integrate and interact with the VM
---
## Simple embedding example
```c
#include "little.h"
#include "little_std.h"

// this is called if the vm encounters an error, letting us react
void my_error_callback(lt_VM* vm, const char* msg)
{
    printf("LT ERROR: %s\n", msg);
}

int main(char** argv, int argc)
{
    lt_VM* vm = lt_open(malloc, free, my_error_callback);                    // open new VM
    ltstd_open_all(vm);                                                      // register stdlib
                   
    const char* my_source_code = ...                                         // read source from file/stream/string

    uint16_t n_return = lt_dostring(vm, my_source_code, "my_module")         // run code as "my_module" 
    if(n_return) printf("LT RETURNED: %s", ltstd_tostring(vm, lt_pop(vm)));  // if our code returns, print the result
}
```
---
## Links
* **[Language overview](doc/lt.md)**
* **[Standard library](doc/ltstd.md)**
* **[C API reference](doc/api.md)**
* **[C API examples](doc/example.md)**
---
## Contribution
Feel free to open an issue or pull request if you feel you have something meaninfgul to add, but keep in mind the language is minimalist by design, so any merging will be very carefully picked

---
## License
Please see [LICENSE](LICENSE) for details