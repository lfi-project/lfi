#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "lfi.h"

static size_t gb(size_t n) {
    return n * 1024 * 1024 * 1024;
}

enum {
    STACK_SIZE = 2UL * 1024 * 1024,
};

static void readfile(FILE* f, void** buf, size_t* size) {
    if (fseek(f, 0, SEEK_END) != 0) {
        perror("fseek");
        exit(1);
    }
    long sz = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        perror("fseek");
        exit(1);
    }
    void* b = malloc(sz);
    assert(b);
    size_t n = fread(b, sz, 1, f);
    assert(n == 1);

    *buf = b;
    *size = sz;
}

uint64_t syshandler(void* ctxp, uint64_t sysno, uint64_t a0, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    //printf("received syscall %ld: %s\n", sysno, (char*) a0);
    lfi_proc_exit(*((struct lfi_proc**) ctxp), 42);
}

int main(int argc, char** argv) {
    struct lfi* lfi = lfi_new((struct lfi_options) {
        .pagesize = getpagesize(),
        .stacksize = STACK_SIZE,
        .syshandler = &syshandler,
    });

    int err;
    if ((err = lfi_auto_add_vaspaces(lfi, 0)) < 0) {
        fprintf(stderr, "error adding address spaces %d\n", err);
        exit(1);
    }

    printf("max procs: %ld\n", lfi_max_procs(lfi));

    if (argc <= 1) {
        fprintf(stderr, "no input binary\n");
        exit(1);
    }

    FILE* f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "error opening %s: %s\n", argv[1], strerror(errno));
        exit(1);
    }

    void* buf;
    size_t size;
    readfile(f, &buf, &size);

    struct lfi_proc* proc;
    err = lfi_add_proc(lfi, &proc, (void*) &proc);
    if (err < 0) {
        fprintf(stderr, "error adding: %d\n", err);
        exit(1);
    }

    clock_t begin = clock();
    int code;
    for (size_t i = 0; i < 100000; i++) {

    struct lfi_proc_info info;
    err = lfi_proc_exec(proc, buf, size, &info);
    if (err < 0) {
        fprintf(stderr, "error loading: %d\n", err);
        exit(1);
    }

    lfi_proc_init_regs(proc, info.elfentry, (uintptr_t) info.stack + info.stacksize - 16);

        code = lfi_proc_start(proc);
    }

    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    printf("%.3lf\n", time_spent);

    printf("exited with code %d\n", code);

    return 0;
}
