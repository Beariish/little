# little_std
The little stdlib is divided into a few modules. These can all be loaded separately if desired

## io
`io.print(...)` is a printf wrapper that evaluates each argument, `ltstd_tostring`s them, and prints.
`io.clock()` returns the current execution time of the program, in seconds. About millisecond accurate.

## math
`math.sin(x)`, `math.cos(x)`, `math.tan(x)`, `math.asin(x)`, `math.acos(x)`, `math.atan(x)`, `math.sinh(x)`, `math.cosh(x)`, `math.tanh(x)`, `math.floor(x)`, `math.ceil(x)`, `math.round(x)`, `math.exp(x)`, `math.log(x)`, `math.log10(x)`, `math.sqrt(x)`, `math.abs(x)`, `math.min(a, b)`, `math.max(a, b)`, `math.pow(a, b)`, and `math.mod(a, b)` are all very simple wrappers around their `math.h` equivalents.

`math.pi` and `math.e` also both exist as constants.

## array
`array.each(x)` returns an iterator function that returns each element in order.
`array.range([start,] end [, step])` returns an iterator function that produces a sequence of numbers.
`array.len(x)` returns the length of an array.
`array.last(x)` returns the last element of an array.
`array.pop(x)` removes, and then returns the last element of an array.
`array.push(array, element)` adds `element` to the back of `array`.
`array.remove(array, index)` cyclicly removes the element at `index`. This does not preserve order.

## string
`string.from(x)` converts argument into a string representation.
`string.concat(...)` concatenates arguments in order.
`string.len(x)` returns the length of `x`.
`string.sub(str, start [, length])` creates a substring of `str`, `length` is the remainder of the string if left out.
`string.format(format, ...)` takes a printf-style format string and a list of arguments to insert.

## gc

`gc.collect()` performs a collection sweep, and returns the number of objects freed.
`gc.addroot(x)` adds object `x` to the gc's rootset, preventing it and everything it references from being collected.
`gc.removeroot(x)` removes `x` from the rootset.