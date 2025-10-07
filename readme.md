# Cool resource compiler
Compiles resources in object file

## How to
1. ### Create config file(by default `crp.conf`)
    ```
    "assets/hello world.txt" s
    assets/int b n
    ```
    * Structure is: `path type name_of_var name_of_size_var`
        * `path` is path to file realtive to cwd
        * `type` is '**s**'(c string) or '**b**'(binary), if type is '**s**' `crp` will add **0** at the end. (default is **'b'**)
        * `name_of_var` is name of variable which will refer to file content(**uint8_t[]**). (default is file **basename**, where all non alpha-numeric replaced by **'_'**)
        * `name_of_size_var` is name of variable which stores size of file(**uint64_t**). (default is file **name_of_var** + **"_len"**)
    * Same names is undefined behavior
2. ### Run
    ```
    crp <output>
    ```
    * Arguments
      * -c path: path to config. (default: crp.conf)
      * -q: quiet. (default: no)
      * output file. (default: assets.o)
3. ### Link
```
clang src/hello_world.c build/assets.o -o hello_world
```

## Example
`hello_world.c`
```c
#include "../../sds.c"
#include <stdint.h>
#include <stdio.h>

extern char hello_world_txt[];
extern uint64_t hello_world_txt_len; // 13 because of 0 at the end

extern char n[];
extern uint64_t n_len;

#define nameof(v) #v

int main() {
    printf("%s, %llu bytes:%s\n", nameof(hello_world_txt), hello_world_txt_len,
           sdscatrepr(sdsempty(), hello_world_txt, hello_world_txt_len - 1));
    printf("%s, %llu bytes:%llu\n", nameof(n), n_len, *(uint64_t *)n);
}
```

### output:
```
hello_world_txt, 13 bytes:"hello world\n"
n, 8 bytes:69420
```
