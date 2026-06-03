#pragma once
#include "schema.h"
#include "btree.h"
#include "memory_pool.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <functional>

namespace MiniDB {

// Condition for WHERE clause
struct Condition {
    std::string column;
    std::string op;   // =, !=, <, >, <=, >=, LIKE, IS NULL, IS NOT NULL
    std::string value;
    bool is_null_check = false;
};

// ORDER BY clause
struct OrderBy {
    std::string column;
    bool ascending = true;
};

// SELECT query parameters
struct SelectQuery {
    std::string table;
    std::vector<std::string> columns;  // empty = *
    std::vector<Condition> conditions;
    std::optional<OrderBy> order_by;
    std::optional<int> limit;
    bool count_only = false;
};

// Table storage: holds all rows and indexes
class TableStorage {
public:
    explicit TableStorage(const TableSchema& schema);

    int insert_row(const Row& row);
    bool delete_row(int row_id);
    bool update_row(int row_id, const std::vector<std::pair<std::string, Value>>& updates);
    std::optional<Row> get_row(int row_id) const;

    std::vector<std::pair<int, Row>> scan(
        const std::vector<Condition>& conditions,
        const std::vector<std::string>& columns) const;

    std::vector<int> index_lookup(const std::string& column, const std::string& value) const;
    std::vector<int> index_range(const std::string& column, const std::string& low, const std::string& high) const;

    void build_index(const std::string& column);
    bool has_index(const std::string& column) const;

    const TableSchema& get_schema() const { return schema; }
    size_t row_count() const;
    size_t deleted_count() const { return deleted_rows.size(); }

private:
    TableSchema schema;
    std::vector<Row> rows;            // row_id -> row data
    std::vector<bool> deleted;        // tombstone flags
    std::unordered_set<int> deleted_rows;
    std::unordered_map<std::string, std::unique_ptr<BTree>> indexes;
    RowDataPool row_pool;
    int next_id = 0;

    bool matches_condition(const Row& row, const Condition& cond) const;
    bool evaluate_like(const std::string& value, const std::string& pattern) const;
};

// Database: collection of tables
class Database {
public:
    explicit Database(const std::string& name);

    bool create_table(const TableSchema& schema);
    bool drop_table(const std::string& name);
    bool table_exists(const std::string& name) const;
    TableStorage* get_table(const std::string& name);
    const TableStorage* get_table(const std::string& name) const;

    std::vector<std::string> list_tables() const;
    const std::string& get_name() const { return db_name; }

private:
    std::string db_name;
    std::unordered_map<std::string, std::unique_ptr<TableStorage>> tables;
};

} // namespace MiniDB
