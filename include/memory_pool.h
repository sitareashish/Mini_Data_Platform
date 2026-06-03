#pragma once
#include <cstddef>
#include <vector>
#include <cassert>
#include <memory>
#include <unordered_map>

namespace MiniDB {

// Fixed-size block memory pool for fast allocation
class MemoryPool {
public:
    explicit MemoryPool(size_t block_size, size_t initial_blocks = 64);
    ~MemoryPool();

    void* allocate();
    void deallocate(void* ptr);
    size_t get_block_size() const { return block_size; }
    size_t get_total_allocated() const { return total_allocated; }
    size_t get_free_count() const { return free_list.size(); }
    void reset(); // Return all memory to pool (fast bulk free)

private:
    size_t block_size;
    std::vector<void*> free_list;
    std::vector<std::unique_ptr<char[]>> chunks;
    size_t total_allocated;
    size_t blocks_per_chunk;

    void expand();
};

// Row data pool for storing variable-length row data
class RowDataPool {
public:
    explicit RowDataPool(size_t initial_capacity = 4096);

    char* allocate_row(size_t size);
    void reset();
    size_t bytes_used() const { return used; }
    size_t capacity() const { return data.size(); }

private:
    std::vector<char> data;
    size_t used;
};

// Pool manager: manages pools of different block sizes
class PoolManager {
public:
    static PoolManager& instance();

    MemoryPool& get_pool(size_t size);
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

private:
    PoolManager() = default;
    std::unordered_map<size_t, std::unique_ptr<MemoryPool>> pools;
};

} // namespace MiniDB
