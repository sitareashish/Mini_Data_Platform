#include "executor.h"
#include "btree.h"
#include "memory_pool.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <vector>
#include <string>
#include <chrono>

using namespace MiniDB;

// ─── Test helpers ─────────────────────────────────────────────────────────────

static int passed = 0, failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "  ✗ FAIL: " << msg << " [" << __FILE__ << ":" << __LINE__ << "]\n"; \
        failed++; \
    } else { \
        std::cout << "  ✓ " << msg << "\n"; \
        passed++; \
    } \
} while(0)

static void section(const std::string& name) {
    std::cout << "\n── " << name << " ──────────────\n";
}

// ─── BTree Tests ─────────────────────────────────────────────────────────────

static void test_btree() {
    section("B-Tree Index");

    BTree bt;
    ASSERT(bt.size() == 0, "Empty tree has 0 keys");

    // Insert
    bt.insert("alice", 1);
    bt.insert("bob",   2);
    bt.insert("carol", 3);
    bt.insert("david", 4);
    bt.insert("eve",   5);
    bt.insert("alice", 10); // duplicate key, different row_id
    ASSERT(bt.size() == 6, "Tree has 6 keys after inserts");

    // Exact search
    auto res = bt.search("alice");
    ASSERT(res.size() == 2, "Found 2 rows for 'alice'");
    ASSERT(res[0] == 1 || res[0] == 10, "First alice row_id is 1 or 10");

    res = bt.search("bob");
    ASSERT(res.size() == 1 && res[0] == 2, "Found bob=2");

    res = bt.search("zzzz");
    ASSERT(res.empty(), "Not found returns empty");

    // Range search
    res = bt.range_search("bob", "david");
    ASSERT(res.size() == 3, "Range bob-david returns 3 rows (bob,carol,david)");

    res = bt.range_search("a", "z");
    ASSERT(res.size() == 6, "Full range returns all 6 rows");

    // Stress test: insert many
    BTree bt2;
    for (int i = 0; i < 500; i++)
        bt2.insert("key" + std::to_string(i), i);
    ASSERT(bt2.size() == 500, "Stress: 500 inserts");

    auto found = bt2.search("key123");
    ASSERT(found.size() == 1 && found[0] == 123, "Stress: found key123=123");
}

// ─── Memory Pool Tests ───────────────────────────────────────────────────────

static void test_memory_pool() {
    section("Memory Pool");

    MemoryPool pool(64, 16);
    ASSERT(pool.get_block_size() == 64, "Block size = 64");

    void* p1 = pool.allocate();
    void* p2 = pool.allocate();
    ASSERT(p1 != nullptr, "Allocated block 1");
    ASSERT(p2 != nullptr, "Allocated block 2");
    ASSERT(p1 != p2, "Two distinct blocks");
    ASSERT(pool.get_total_allocated() == 2, "2 blocks allocated");

    pool.deallocate(p1);
    ASSERT(pool.get_total_allocated() == 1, "1 block after dealloc");

    void* p3 = pool.allocate();
    ASSERT(p3 == p1, "Reuses deallocated block");

    // RowDataPool
    RowDataPool rdp(256);
    char* r1 = rdp.allocate_row(32);
    char* r2 = rdp.allocate_row(64);
    ASSERT(r1 != nullptr, "Row pool alloc 1");
    ASSERT(r2 != nullptr, "Row pool alloc 2");
    ASSERT(r2 == r1 + 32, "Row pool is contiguous");
    ASSERT(rdp.bytes_used() == 96, "Row pool used = 96");

    rdp.reset();
    ASSERT(rdp.bytes_used() == 0, "Row pool reset");
}

// ─── Parser Tests ────────────────────────────────────────────────────────────

static void test_parser() {
    section("SQL Parser");
    SQLParser p;

    // CREATE TABLE
    auto s = p.parse("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER);");
    ASSERT(s.type == StatementType::CREATE_TABLE, "Parsed CREATE TABLE");
    ASSERT(s.create_table->table_name == "users", "Table name = users");
    ASSERT(s.create_table->columns.size() == 3, "3 columns");
    ASSERT(s.create_table->columns[0].primary_key, "id is PK");
    ASSERT(s.create_table->columns[1].not_null, "name is NOT NULL");

    // SELECT
    s = p.parse("SELECT name, age FROM users WHERE age > 18 ORDER BY name ASC LIMIT 10;");
    ASSERT(s.type == StatementType::SELECT, "Parsed SELECT");
    ASSERT(s.select->table == "users", "FROM users");
    ASSERT(s.select->columns.size() == 2, "2 columns");
    ASSERT(s.select->conditions.size() == 1, "1 condition");
    ASSERT(s.select->conditions[0].op == ">", "WHERE op = >");
    ASSERT(s.select->order_by.has_value(), "Has ORDER BY");
    ASSERT(s.select->limit == 10, "LIMIT 10");

    // INSERT
    s = p.parse("INSERT INTO users (id, name, age) VALUES (1, Alice, 25);");
    ASSERT(s.type == StatementType::INSERT, "Parsed INSERT");
    ASSERT(s.insert->table_name == "users", "INSERT INTO users");
    ASSERT(s.insert->value_rows.size() == 1, "1 value row");

    // UPDATE
    s = p.parse("UPDATE users SET age = 26 WHERE id = 1;");
    ASSERT(s.type == StatementType::UPDATE, "Parsed UPDATE");
    ASSERT(s.update->assignments.size() == 1, "1 assignment");

    // DELETE
    s = p.parse("DELETE FROM users WHERE id = 5;");
    ASSERT(s.type == StatementType::DELETE, "Parsed DELETE");
    ASSERT(s.delete_stmt->conditions.size() == 1, "1 condition in DELETE");

    // IS NULL
    s = p.parse("SELECT * FROM users WHERE age IS NULL;");
    ASSERT(s.type == StatementType::SELECT, "IS NULL query parsed");
    ASSERT(s.select->conditions[0].op == "IS NULL", "IS NULL op");

    // LIKE
    s = p.parse("SELECT * FROM users WHERE name LIKE %ali%;");
    ASSERT(s.type == StatementType::SELECT, "LIKE query parsed");
    ASSERT(s.select->conditions[0].op == "LIKE", "LIKE op");
}

// ─── Executor Integration Tests ──────────────────────────────────────────────

static void test_executor() {
    section("SQL Executor");

    Database db("test_db");
    SQLExecutor exec(db);

    // Create table
    auto r = exec.execute("CREATE TABLE products (id INTEGER PRIMARY KEY, name TEXT NOT NULL, price FLOAT, stock INTEGER);");
    ASSERT(r.success, "CREATE TABLE success");

    r = exec.execute("CREATE TABLE products (id INTEGER);");
    ASSERT(!r.success, "Duplicate CREATE TABLE fails");

    // Insert
    r = exec.execute("INSERT INTO products VALUES (1, Widget, 9.99, 100);");
    ASSERT(r.success, "INSERT 1 row");

    r = exec.execute("INSERT INTO products VALUES "
                     "(2, Gadget, 24.99, 50),"
                     "(3, Doohickey, 4.99, 200),"
                     "(4, Thingamajig, 14.99, 75);");
    ASSERT(r.success, "INSERT 3 rows");
    ASSERT(value_to_string(r.rows[0][0]) == "3", "3 rows reported inserted");

    // SELECT *
    r = exec.execute("SELECT * FROM products;");
    ASSERT(r.success, "SELECT * success");
    ASSERT(r.rows.size() == 4, "SELECT * returns 4 rows");
    ASSERT(r.columns.size() == 4, "4 columns");

    // SELECT with WHERE
    r = exec.execute("SELECT name, price FROM products WHERE price > 10;");
    ASSERT(r.success, "SELECT with WHERE");
    ASSERT(r.rows.size() == 2, "2 rows price>10");

    // ORDER BY
    r = exec.execute("SELECT name, price FROM products ORDER BY price ASC;");
    ASSERT(r.success, "ORDER BY ASC");
    ASSERT(value_to_string(r.rows[0][0]) == "Doohickey", "Cheapest first");

    r = exec.execute("SELECT name FROM products ORDER BY price DESC;");
    ASSERT(value_to_string(r.rows[0][0]) == "Gadget", "Most expensive first");

    // LIMIT
    r = exec.execute("SELECT name FROM products LIMIT 2;");
    ASSERT(r.rows.size() == 2, "LIMIT 2 returns 2 rows");

    // LIKE
    r = exec.execute("SELECT name FROM products WHERE name LIKE %et%;");
    ASSERT(r.success, "LIKE query");
    ASSERT(r.rows.size() == 2, "LIKE %et% matches Widget,Gadget");

    // UPDATE
    r = exec.execute("UPDATE products SET price = 19.99 WHERE id = 1;");
    ASSERT(r.success, "UPDATE success");
    ASSERT(value_to_string(r.rows[0][0]) == "1", "1 row updated");

    r = exec.execute("SELECT price FROM products WHERE id = 1;");
    ASSERT(value_to_string(r.rows[0][0]) == "19.99", "Price updated correctly");

    // DELETE
    r = exec.execute("DELETE FROM products WHERE stock > 150;");
    ASSERT(r.success, "DELETE success");
    ASSERT(value_to_string(r.rows[0][0]) == "1", "1 row deleted (stock=200)");

    r = exec.execute("SELECT * FROM products;");
    ASSERT(r.rows.size() == 3, "3 rows remain after delete");

    // COUNT
    r = exec.execute("SELECT COUNT(*) FROM products;");
    ASSERT(r.success, "COUNT(*) success");
    ASSERT(value_to_string(r.rows[0][0]) == "3", "COUNT(*) = 3");

    // CREATE INDEX
    r = exec.execute("CREATE INDEX idx_price ON products (price);");
    ASSERT(r.success, "CREATE INDEX");

    // Index lookup
    r = exec.execute("SELECT name FROM products WHERE price = 24.99;");
    ASSERT(r.success && r.rows.size() == 1, "Index lookup by price");

    // DROP TABLE
    r = exec.execute("DROP TABLE products;");
    ASSERT(r.success, "DROP TABLE");
    r = exec.execute("SELECT * FROM products;");
    ASSERT(!r.success, "SELECT from dropped table fails");

    // SHOW TABLES & DESCRIBE
    exec.execute("CREATE TABLE t1 (a INTEGER, b TEXT);");
    r = exec.execute("SHOW TABLES;");
    ASSERT(r.success && r.rows.size() == 1, "SHOW TABLES shows t1");

    r = exec.execute("DESCRIBE t1;");
    ASSERT(r.success && r.rows.size() == 2, "DESCRIBE shows 2 columns");
}

// ─── Performance benchmark ───────────────────────────────────────────────────

static void test_performance() {
    section("Performance Benchmark");

    Database db("bench");
    SQLExecutor exec(db);

    exec.execute("CREATE TABLE bench (id INTEGER PRIMARY KEY, val INTEGER, name TEXT);");
    exec.execute("CREATE INDEX idx_val ON bench (val);");

    const int N = 10000;
    auto t0 = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < N; i++) {
        exec.execute("INSERT INTO bench VALUES (" + std::to_string(i) + ", " +
                     std::to_string(i % 100) + ", item" + std::to_string(i) + ");");
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double insert_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    t0 = std::chrono::high_resolution_clock::now();
    auto r = exec.execute("SELECT * FROM bench WHERE val = 42;");
    t1 = std::chrono::high_resolution_clock::now();
    double select_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    t0 = std::chrono::high_resolution_clock::now();
    r = exec.execute("SELECT * FROM bench WHERE name LIKE %500%;");
    t1 = std::chrono::high_resolution_clock::now();
    double scan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << "  " << N << " inserts:         " << std::fixed << std::setprecision(1)
              << insert_ms << "ms  (" << (int)(N / insert_ms * 1000) << " rows/sec)\n";
    std::cout << "  Index point query:  " << select_ms << "ms  ("
              << r.rows.size() << " rows)\n";
    std::cout << "  Full scan + LIKE:   " << scan_ms << "ms\n";

    ASSERT(insert_ms < 30000, "10k inserts under 30s");
    ASSERT(select_ms < 100, "Index lookup under 100ms");
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "═══════════════════════════════════════\n";
    std::cout << "  MiniDB Test Suite\n";
    std::cout << "═══════════════════════════════════════\n";

    test_btree();
    test_memory_pool();
    test_parser();
    test_executor();
    test_performance();

    std::cout << "\n═══════════════════════════════════════\n";
    std::cout << "  Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "═══════════════════════════════════════\n";

    return failed > 0 ? 1 : 0;
}
