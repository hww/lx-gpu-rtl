// tools/asm.cpp — CLI: asm file.asm > file.hex
#include "asm.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s file.asm\n", argv[0]);
        return 1;
    }
    auto words = asm_assemble_file(argv[1]);
    for (auto w : words) printf("%08X\n", w);
    return 0;
}