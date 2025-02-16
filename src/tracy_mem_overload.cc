// overloads new and delete to trace memory

#include "tracy/Tracy.hpp"
#include <cstdlib>
#include <new>

#ifdef TRACY_ENABLE

void *operator new(std::size_t size) {
    void *ptr = malloc(size);
    TracyAlloc(ptr, size);
    return ptr;
}

void *operator new(std::size_t size, const std::nothrow_t &tag) noexcept {
    void *ptr = malloc(size);
    TracyAlloc(ptr, size);
    return ptr;
}

void *operator new[](std::size_t size) {
    void *ptr = malloc(size);
    TracyAlloc(ptr, size);
    return ptr;
}

void *operator new[](std::size_t size, const std::nothrow_t &tag) noexcept {
    void *ptr = malloc(size);
    TracyAlloc(ptr, size);
    return ptr;
}

void operator delete(void *ptr) noexcept {
    TracyFree(ptr);
    free(ptr);
}

void operator delete(void *ptr, const std::nothrow_t &tag) noexcept {
    TracyFree(ptr);
    free(ptr);
}

void operator delete[](void *ptr) noexcept {
    TracyFree(ptr);
    free(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t &tag) noexcept {
    TracyFree(ptr);
    free(ptr);
}

#endif
