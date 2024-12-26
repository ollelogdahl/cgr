#include "oc.h"

#include <stdio.h>
#include <stdlib.h>

#include <sys/mman.h>
#include <signal.h>

#ifdef DEBUG
// we only provide stacktraces in debug builds.
#define B_STACKTRACE_IMPL
#include <b_stacktrace/b_stacktrace.h>
#endif

void on_segfault(int) {
    panic("caught segmentation fault (SIGSEGV).");
}

void oc_init() {
#ifdef DEBUG
    // register a signal handler for segfaults
    // @note: I don't think we want stack-traces when
    // running in memory_debug mode, as address sanitizer handles
    // those already way better...

#ifndef MEM_DEBUG
    signal(SIGSEGV, on_segfault);
#endif
#endif
}

__attribute__((noreturn))
void _panic_impl() {
#ifdef DEBUG
#ifndef MEM_DEBUG
    auto bt = b_stacktrace_get_string();
    std::cout << bt << std::endl;
#endif
#endif

    std::abort();
}

result_t<file_t, std::string> file_read(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        return std::string("failed to open file: ") + path;
    }
    defer (fclose(file));

    fseek(file, 0, SEEK_END);
    usize size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // if the file is big (idk, 1MB), we use mmap instead.
    const usize mmap_threshold = 1024 * 1024;
    if (size < mmap_threshold) {
        byte* data = (byte*)malloc(size);
        if (!data) {
            return std::string("failed to allocate memory for file: ") + path;
        }

        if (fread(data, 1, size, file) != size) {
            return std::string("failed to read file: ") + path;
        }

        return file_t { .contents = slice<byte>(data, size), .is_mmaped = false };
    }

    // mmap instead!
    void* data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fileno(file), 0);
    if (data == MAP_FAILED) {
        return std::string("failed to mmap file: ") + path;
    }

    return file_t { .contents = slice<byte>((byte*)data, size), .is_mmaped = true };
}

void file_close(file_t &file) {
    if (file.is_mmaped) {
        munmap((void*)file.contents.data, file.contents.len);
    }
    else {
        free((void*)file.contents.data);
    }
}

f32 str2f32(slice<byte> str) {
    // @speed: this is pretty slow, and also doesn't report if the string is invalid
    f32 result = 0.0f;
    bool negative = false;
    char *end = (char*)str.data + str.len;
    char *p = (char*)str.data;

    if (str[0] == '-') {
        negative = true;
        p++;
    }
    else if(str[0] == '+') {
        p++;
    }

    while (p < end && *p >= '0' && *p <= '9') {
        result = result * 10.0f + (f32)(*p - '0');
        p++;
    }

    if (p < end && *p == '.') {
        f32 decimal = 0.0f;
        f32 multiplier = 0.1f;
        p++;
        while (p < end && *p >= '0' && *p <= '9') {
            decimal += (f32)(*p - '0');
            decimal *= 10;
            multiplier *= 0.1f;
            p++;
        }

        result += decimal * multiplier;
    }

    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        bool negative_exponent = false;
        if (p < end && *p == '-') {
            negative_exponent = true;
            p++;
        }
        else if (p < end && *p == '+') {
            p++;
        }

        f32 exponent = 0.0f;
        while (p < end && *p >= '0' && *p <= '9') {
            exponent = exponent * 10.0f + (f32)(*p - '0');
            p++;
        }

        if (negative_exponent) {
            exponent = -exponent;
        }

        result *= powf(10.0f, exponent);
    }

    if (negative) {
        result = -result;
    }

    return result;
}

u32 str2u32(slice<byte> str) {
    // @todo: also kinda slow.
    u32 result = 0;
    char *end = (char*)str.data + str.len;
    char *p = (char*)str.data;

    while (p < end && *p >= '0' && *p <= '9') {
        result = result * 10 + (u32)(*p - '0');
        p++;
    }

    return result;
}
