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
    // hello_world_txt, 13 bytes:"hello world\n"
    // n, 8 bytes:69420
}
