// overloads new and delete to trace memory

#include "tracy/Tracy.hpp"
#include <cstdlib>

void *operator new(std::size_t size) {
    void *ptr = malloc(size);
    TracyAlloc(ptr, size);
    return ptr;
}

void *operator new[](std::size_t size) {
    void *ptr = malloc(size);
    TracyAlloc(ptr, size);
    return ptr;
}

void operator delete(void *ptr) noexcept {
    TracyFree(ptr);
    free(ptr);
}

void operator delete[](void *ptr) noexcept {
    TracyFree(ptr);
    free(ptr);
}
