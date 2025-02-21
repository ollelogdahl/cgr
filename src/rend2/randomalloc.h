#pragma once

#include "oc.h"

// A generic allocator.
// It should have an interface for cleanup
// also. This is not implemented yet.

class RandomAllocator {
public:
    RandomAllocator(u32 capacity);

    u32 allocate(u32 size);
    void deallocate(u32 offset, u32 size);
private:
    struct Block {
        u32 offset;
        u32 size;
        bool operator<(const Block &other) const {
            return offset < other.offset;
        }
    };

    u32 m_capacity;
    std::vector<Block> m_free_blocks;
};
