// tools/dis.cpp — CLI: dis file.hex
#include "disasm.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s file.hex\n", argv[0]);
        return 1;
    }
    FILE* f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    char buf[256];
    int pc = 0;
    while (fgets(buf, sizeof buf, f)) {
        char* p = buf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;
        uint32_t w = (uint32_t)strtoul(p, nullptr, 16);
        printf("%03X: %08X  %s\n", pc++, w, disasm32(w));
    }
    fclose(f);
    return 0;
}