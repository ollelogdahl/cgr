#include "randomalloc.h"

// This is not the most optimal implementation.
// We could enforce some alignment rules.

RandomAllocator::RandomAllocator(u32 capacity)
: m_capacity(capacity) {
    m_free_blocks.push_back({0, capacity});
}

u32 RandomAllocator::allocate(u32 size) {
    // Find best fitting block
    Block* best_block = nullptr;
    size_t best_block_index = 0;
    u32 smallest_valid_size = UINT32_MAX;

    for (size_t i = 0; i < m_free_blocks.size(); i++) {
        Block& block = m_free_blocks[i];
        if (block.size >= size && block.size < smallest_valid_size) {
            best_block = &block;
            best_block_index = i;
            smallest_valid_size = block.size;
        }
    }

    if (!best_block) {
        return UINT32_MAX; // Or throw exception - allocation failed
    }

    u32 alloc_offset = best_block->offset;

    // If the block is exactly the right size, remove it
    // Otherwise, reduce its size and adjust offset
    if (best_block->size == size) {
        m_free_blocks.erase(m_free_blocks.begin() + best_block_index);
    } else {
        best_block->offset += size;
        best_block->size -= size;
    }

    return alloc_offset;
}

void RandomAllocator::deallocate(u32 offset, u32 size) {
    Block new_block{offset, size};

    // Find where to insert the new free block
    auto it = std::lower_bound(
        m_free_blocks.begin(), m_free_blocks.end(), new_block);

    // Try to merge with adjacent blocks
    bool merged_prev = false;
    bool merged_next = false;

    // Check previous block
    if (it != m_free_blocks.begin()) {
        auto prev = it - 1;
        if (prev->offset + prev->size == offset) {
            prev->size += size;
            merged_prev = true;
        }
    }

    // Check next block
    if (it != m_free_blocks.end()) {
        if (offset + size == it->offset) {
            if (merged_prev) {
                auto prev = it - 1;
                prev->size += it->size;
                m_free_blocks.erase(it);
            } else {
                it->offset = offset;
                it->size += size;
            }
            merged_next = true;
        }
    }

    // If we couldn't merge with either neighbor, insert new block
    if (!merged_prev && !merged_next) {
        m_free_blocks.insert(it, new_block);
    }
}
