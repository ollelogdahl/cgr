#include "slotalloc.h"

SlotAllocator::SlotAllocator(u32 capacity)
: m_capacity(capacity) , m_num_used(0) {
    m_bitmap.resize((capacity + 63) / 64, 0);
    m_first_free = 0;
}

u32 SlotAllocator::allocate() {
    if (m_num_used >= m_capacity) {
        return UNALLOCATED;
    }

    // Find first free slot starting from m_first_free
    while (m_first_free < m_capacity) {
        u32 block_idx = m_first_free / 64;
        u64 block = m_bitmap[block_idx];

        if (block != ~0ull) { // If not all bits are set
            u32 bit_idx = m_first_free % 64;
            u64 mask = 1ull << bit_idx;

            while (mask) {
                if ((block & mask) == 0) {
                    // Found a free slot
                    m_bitmap[block_idx] |= mask;
                    m_num_used++;
                    return block_idx * 64 + bit_idx;
                }
                mask <<= 1;
                bit_idx++;
            }
        }

        m_first_free = (block_idx + 1) * 64;
    }

    return UNALLOCATED;
}

void SlotAllocator::deallocate(u32 idx) {
    if (idx >= m_capacity) {
        return;
    }

    u32 block_idx = idx / 64;
    u32 bit_idx = idx % 64;
    u64 mask = 1ull << bit_idx;

    if (m_bitmap[block_idx] & mask) {
        m_bitmap[block_idx] &= ~mask;
        m_num_used--;
        m_first_free = std::min(m_first_free, idx);
    }
}
