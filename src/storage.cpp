#include "storage.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <regex>

namespace MiniDB {

// ─── TableStorage ────────────────────────────────────────────────────────────

TableStorage::TableStorage(const TableSchema& schema)
    : schema(schema), row_pool(8192) {

    // Auto-build index on primary key if specified
    if (!schema.primary_key_col.empty())
        build_index(schema.primary_key_col);
}

int TableStorage::insert_row(const Row& row) {
    int id = next_id++;
    rows.push_back(row);
    deleted.push_back(false);

    // Update all indexes
    for (auto& [col_name, btree] : indexes) {
        int col_idx = schema.column_index(col_name);
        if (col_idx >= 0 && col_idx < (int)row.size()) {
            std::string key = value_to_string(row[col_idx]);
            btree->insert(key, id);
        }
    }
    return id;
}

bool TableStorage::delete_row(int row_id) {
    if (row_id < 0 || row_id >= (int)rows.size() || deleted[row_id])
        return false;
    deleted[row_id] = true;
    deleted_rows.insert(row_id);
    return true;
}

bool TableStorage::update_row(int row_id, const std::vector<std::pair<std::string, Value>>& updates) {
    if (row_id < 0 || row_id >= (int)rows.size() || deleted[row_id])
        return false;

    Row& row = rows[row_id];
    for (auto& [col_name, new_val] : updates) {
        int idx = schema.column_index(col_name);
        if (idx < 0) continue;

        // Update index if exists
        if (indexes.count(col_name)) {
            std::string old_key = value_to_string(row[idx]);
            indexes[col_name]->remove(old_key, row_id);
            std::string new_key = value_to_string(new_val);
            indexes[col_name]->insert(new_key, row_id);
        }
        row[idx] = new_val;
    }
    return true;
}

std::optional<Row> TableStorage::get_row(int row_id) const {
    if (row_id < 0 || row_id >= (int)rows.size() || deleted[row_id])
        return std::nullopt;
    return rows[row_id];
}

bool TableStorage::matches_condition(const Row& row, const Condition& cond) const {
    int col_idx = schema.column_index(cond.column);
    if (col_idx < 0) return false;

    const Value& cell = row[col_idx];

    // NULL checks
    if (cond.op == "IS NULL")
        return std::holds_alternative<std::monostate>(cell);
    if (cond.op == "IS NOT NULL")
        return !std::holds_alternative<std::monostate>(cell);

    if (std::holds_alternative<std::monostate>(cell))
        return false;

    // Determine column type and parse value
    DataType dt = schema.columns[col_idx].type;
    Value cmp_val = string_to_value(cond.value, dt);

    if (cond.op == "=")  return value_equal(cell, cmp_val);
    if (cond.op == "!=") return !value_equal(cell, cmp_val);
    if (cond.op == "<")  return value_less(cell, cmp_val);
    if (cond.op == ">")  return value_less(cmp_val, cell);
    if (cond.op == "<=") return !value_less(cmp_val, cell);
    if (cond.op == ">=") return !value_less(cell, cmp_val);

    if (cond.op == "LIKE") {
        return evaluate_like(value_to_string(cell), cond.value);
    }
    return false;
}

bool TableStorage::evaluate_like(const std::string& value, const std::string& pattern) const {
    // Convert SQL LIKE pattern to regex
    std::string regex_str = "^";
    for (char c : pattern) {
        if (c == '%')      regex_str += ".*";
        else if (c == '_') regex_str += ".";
        else if (std::string("\\^$.|?*+()[]{}").find(c) != std::string::npos)
            regex_str += std::string("\\") + c;
        else
            regex_str += c;
    }
    regex_str += "$";
    std::regex re(regex_str, std::regex::icase);
    return std::regex_match(value, re);
}

std::vector<std::pair<int, Row>> TableStorage::scan(
    const std::vector<Condition>& conditions,
    const std::vector<std::string>& select_cols) const {

    std::vector<std::pair<int, Row>> results;

    // Check if we can use index for first equality condition
    std::vector<int> candidates;
    bool using_index = false;

    for (auto& cond : conditions) {
        if ((cond.op == "=" || cond.op == "LIKE") && indexes.count(cond.column)) {
            if (cond.op == "=") {
                candidates = indexes.at(cond.column)->search(cond.value);
                using_index = true;
                break;
            }
        }
    }

    if (!using_index) {
        // Full scan
        for (int id = 0; id < (int)rows.size(); id++) {
            if (!deleted[id]) candidates.push_back(id);
        }
    }

    // Determine output columns
    std::vector<int> col_indices;
    if (select_cols.empty() || (select_cols.size() == 1 && select_cols[0] == "*")) {
        for (int i = 0; i < (int)schema.columns.size(); i++)
            col_indices.push_back(i);
    } else {
        for (auto& cn : select_cols) {
            int idx = schema.column_index(cn);
            col_indices.push_back(idx); // -1 if not found (handled later)
        }
    }

    for (int id : candidates) {
        if (id < 0 || id >= (int)rows.size() || deleted[id]) continue;
        const Row& row = rows[id];

        // Check all conditions
        bool match = true;
        for (auto& cond : conditions) {
            if (!matches_condition(row, cond)) { match = false; break; }
        }
        if (!match) continue;

        // Project columns
        Row proj;
        for (int ci : col_indices) {
            if (ci >= 0 && ci < (int)row.size())
                proj.push_back(row[ci]);
            else
                proj.push_back(std::monostate{});
        }
        results.emplace_back(id, std::move(proj));
    }
    return results;
}

std::vector<int> TableStorage::index_lookup(const std::string& column,
                                              const std::string& value) const {
    auto it = indexes.find(column);
    if (it == indexes.end()) return {};
    return it->second->search(value);
}

std::vector<int> TableStorage::index_range(const std::string& column,
                                             const std::string& low,
                                             const std::string& high) const {
    auto it = indexes.find(column);
    if (it == indexes.end()) return {};
    return it->second->range_search(low, high);
}

void TableStorage::build_index(const std::string& column) {
    if (indexes.count(column)) return;
    auto bt = std::make_unique<BTree>();

    int col_idx = schema.column_index(column);
    if (col_idx < 0) return;

    for (int id = 0; id < (int)rows.size(); id++) {
        if (!deleted[id]) {
            std::string key = value_to_string(rows[id][col_idx]);
            bt->insert(key, id);
        }
    }
    indexes[column] = std::move(bt);
}

bool TableStorage::has_index(const std::string& column) const {
    return indexes.count(column) > 0;
}

size_t TableStorage::row_count() const {
    return rows.size() - deleted_rows.size();
}

// ─── Database ────────────────────────────────────────────────────────────────

Database::Database(const std::string& name) : db_name(name) {}

bool Database::create_table(const TableSchema& schema) {
    if (tables.count(schema.table_name)) return false;
    tables[schema.table_name] = std::make_unique<TableStorage>(schema);
    return true;
}

bool Database::drop_table(const std::string& name) {
    return tables.erase(name) > 0;
}

bool Database::table_exists(const std::string& name) const {
    return tables.count(name) > 0;
}

TableStorage* Database::get_table(const std::string& name) {
    auto it = tables.find(name);
    return it == tables.end() ? nullptr : it->second.get();
}

const TableStorage* Database::get_table(const std::string& name) const {
    auto it = tables.find(name);
    return it == tables.end() ? nullptr : it->second.get();
}

std::vector<std::string> Database::list_tables() const {
    std::vector<std::string> names;
    for (auto& [name, _] : tables) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace MiniDB
