#include "memory_pool.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace MiniDB {

// ─── MemoryPool ─────────────────────────────────────────────────────────────

MemoryPool::MemoryPool(size_t block_size, size_t initial_blocks)
    : block_size(block_size), total_allocated(0), blocks_per_chunk(initial_blocks) {
    expand(); // Pre-allocate first chunk
}

MemoryPool::~MemoryPool() {
    // chunks unique_ptr cleanup handles memory
}

void MemoryPool::expand() {
    size_t chunk_bytes = block_size * blocks_per_chunk;
    auto chunk = std::make_unique<char[]>(chunk_bytes);
    char* base = chunk.get();

    // Slice chunk into blocks and add to free list
    for (size_t i = 0; i < blocks_per_chunk; i++)
        free_list.push_back(base + i * block_size);

    chunks.push_back(std::move(chunk));

    // Double chunk size for next expansion (amortized O(1) growth)
    blocks_per_chunk *= 2;
}

void* MemoryPool::allocate() {
    if (free_list.empty())
        expand();

    void* ptr = free_list.back();
    free_list.pop_back();
    total_allocated++;
    return ptr;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    free_list.push_back(ptr);
    total_allocated--;
}

void MemoryPool::reset() {
    free_list.clear();
    for (auto& chunk : chunks) {
        char* base = chunk.get();
        // Recalculate blocks per chunk (stored in order, first chunk = original size)
        size_t sz = block_size;
        // Re-slice all chunks
        for (size_t i = 0; i < blocks_per_chunk / (size_t)chunks.size(); i++)
            free_list.push_back(base + i * sz);
    }
    total_allocated = 0;
}

// ─── RowDataPool ────────────────────────────────────────────────────────────

RowDataPool::RowDataPool(size_t initial_capacity) : used(0) {
    data.resize(initial_capacity);
}

char* RowDataPool::allocate_row(size_t size) {
    if (used + size > data.size()) {
        // Grow by doubling or by size, whichever is larger
        size_t new_cap = std::max(data.size() * 2, used + size);
        data.resize(new_cap);
    }
    char* ptr = data.data() + used;
    used += size;
    return ptr;
}

void RowDataPool::reset() {
    used = 0;
    // Keep capacity, just reset used pointer
}

// ─── PoolManager ────────────────────────────────────────────────────────────

PoolManager& PoolManager::instance() {
    static PoolManager mgr;
    return mgr;
}

MemoryPool& PoolManager::get_pool(size_t size) {
    // Round up size to power of 2 for pool reuse
    size_t rounded = 8;
    while (rounded < size) rounded *= 2;

    auto it = pools.find(rounded);
    if (it == pools.end()) {
        pools[rounded] = std::make_unique<MemoryPool>(rounded, 64);
        return *pools[rounded];
    }
    return *it->second;
}

void* PoolManager::allocate(size_t size) {
    return get_pool(size).allocate();
}

void PoolManager::deallocate(void* ptr, size_t size) {
    size_t rounded = 8;
    while (rounded < size) rounded *= 2;
    auto it = pools.find(rounded);
    if (it != pools.end())
        it->second->deallocate(ptr);
}

} // namespace MiniDB
