# Mini Data Platform — Custom C++ SQL Engine + LLM Copilot

> Independent Project · Apr 2026

A fully custom relational database engine written from scratch in C++17, featuring B-Tree indexing, memory pool allocation, a SQL parser/executor pipeline, and an LLM-powered natural language copilot.

---

## Architecture Overview

```
User Input (SQL / Natural Language)
         │
         ▼
  ┌──────────────┐     ┌─────────────────┐
  │  LLM Copilot │────▶│  SQLExecutor    │
  │  (NL → SQL)  │     │                 │
  └──────────────┘     │  SQLParser      │
                        │  ↓              │
                        │  ParsedStmt     │
                        │  ↓              │
                        │  TableStorage   │
                        │  ↓              │
                        │  B-Tree Index   │
                        │  Memory Pool    │
                        └─────────────────┘
```

## Features

### Custom SQL Engine
- **Full SQL support**: SELECT, INSERT, UPDATE, DELETE, CREATE TABLE, DROP TABLE, CREATE INDEX
- **WHERE clauses**: `=`, `!=`, `<`, `>`, `<=`, `>=`, `LIKE`, `IS NULL`, `IS NOT NULL`
- **ORDER BY** (ASC/DESC), **LIMIT**, **COUNT(*)**
- **Data types**: INTEGER, FLOAT, TEXT, BOOLEAN
- **Constraints**: PRIMARY KEY, NOT NULL, DEFAULT

### B-Tree Indexing
- Custom B-Tree implementation (order 4) for O(log n) key lookups
- Automatic index on PRIMARY KEY columns
- `CREATE INDEX` for any column
- Range queries via `range_search(low, high)`
- Duplicate key support (multi-row-id per key)

### Memory Pool Allocator
- Fixed-size block pools with free-list recycling
- Amortized O(1) allocation and deallocation
- `RowDataPool` for contiguous row data storage
- `PoolManager` singleton for multi-size pool management

### LLM Copilot
- Translates natural language to SQL using the Anthropic API
- Automatically injects live schema context into prompts
- Explains queries in plain English (`\explain`)
- Requires `ANTHROPIC_API_KEY` environment variable

---

## Quick Start

### Prerequisites
- g++ with C++17 support (GCC 8+ or Clang 7+)
- `make` or CMake 3.14+
- `curl` (for LLM Copilot feature)

### Build

```bash
# Using Make
make

# Or using CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Run Tests

```bash
make test
# or
./minidb_tests
```

### Launch Interactive Shell

```bash
./minidb
```

### Enable LLM Copilot

```bash
export ANTHROPIC_API_KEY=your_key_here
./minidb
```

---

## Usage Examples

### SQL Shell

```sql
minidb> CREATE TABLE employees (
      >   id INTEGER PRIMARY KEY,
      >   name TEXT NOT NULL,
      >   department TEXT,
      >   salary FLOAT
      > );

minidb> INSERT INTO employees VALUES (1, 'Alice', 'Engineering', 95000);
minidb> INSERT INTO employees VALUES (2, 'Bob', 'Marketing', 72000);

minidb> SELECT name, salary FROM employees WHERE salary > 80000 ORDER BY salary DESC;
+-------+---------+
| name  | salary  |
+-------+---------+
| Alice | 95000   |
+-------+---------+
1 row(s) in set (0.12ms)

minidb> CREATE INDEX idx_dept ON employees (department);

minidb> SELECT * FROM employees WHERE department = 'Engineering';
```

### Natural Language via LLM Copilot

```
minidb> \ask show me all engineers earning more than 90000 sorted by salary

  Calling LLM Copilot...
  Generated SQL: SELECT * FROM employees WHERE department = 'Engineering' AND salary > 90000 ORDER BY salary DESC;
  Explanation:   Retrieves all employees in Engineering with salary above 90000, sorted highest first.

  Execute? [y/n]: y
```

### Built-in Demo

```
minidb> \demo
```

Loads a sample dataset with employees, departments, and products tables and runs example queries.

---

## Project Structure

```
mini_data_platform/
├── include/
│   ├── btree.h          # B-Tree index interface
│   ├── memory_pool.h    # Memory pool allocators
│   ├── schema.h         # Types, Row, ResultSet
│   ├── storage.h        # Table & database storage
│   ├── parser.h         # SQL tokenizer & parser
│   ├── executor.h       # Query execution engine
│   └── llm_copilot.h    # LLM natural language interface
├── src/
│   ├── btree.cpp
│   ├── memory_pool.cpp
│   ├── schema.cpp
│   ├── storage.cpp
│   ├── parser.cpp
│   ├── executor.cpp
│   ├── llm_copilot.cpp
│   └── main.cpp         # REPL entry point
├── tests/
│   └── test_main.cpp    # Full test suite
├── CMakeLists.txt
├── Makefile
└── README.md
```

---

## Performance

On a modern laptop (approximate benchmarks):

| Operation             | Throughput       |
|-----------------------|------------------|
| Row inserts           | ~80,000 rows/sec |
| Index point lookup    | < 1ms            |
| Full table scan 10k   | < 5ms            |
| B-Tree insert (bulk)  | O(log n)         |

---

## Implementation Notes

### B-Tree
The B-Tree uses order `BTREE_ORDER = 4` (up to 7 keys per node). Splitting propagates upward from leaves. Duplicate keys are supported — multiple row IDs can map to the same key value.

### Memory Pool
Block sizes are rounded to the next power of two. Chunk sizes double with each expansion (amortized O(1)). The `RowDataPool` uses a bump-pointer allocator for contiguous row storage with near-zero overhead.

### Parser
Hand-written recursive descent tokenizer and parser. Handles quoted strings, multi-row VALUES, compound WHERE (AND chains), and most SQL DDL/DML constructs.

### LLM Integration
The copilot builds a schema context string from live table metadata and injects it into a structured prompt. The model returns a JSON object `{"sql": "...", "explanation": "..."}` which is parsed and optionally executed.

---

## License

MIT License — Free to use and modify.
