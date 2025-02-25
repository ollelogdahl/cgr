#pragma once

#include "oc.h"

// A generic allocator for getting an index into an array.
// @todo: some arrays are growable, and some should have
// a replacement strategy. These should be settable some way.
class SlotAllocator {
public:

    const u32 UNALLOCATED = ~0u;
    SlotAllocator(u32 capacity);

    u32 allocate();
    void deallocate(u32 id);
    bool is_occupied(u32 id) const;

    u32 capacity() const { return m_capacity; }
    u32 size() const { return m_num_used; }
private:
    std::vector<u64> m_bitmap;
    u32 m_capacity;
    u32 m_num_used;
    u32 m_first_free;
};
