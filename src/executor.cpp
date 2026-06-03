#include "executor.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace MiniDB {

SQLExecutor::SQLExecutor(Database& db) : db(db) {}

ResultSet SQLExecutor::execute(const std::string& sql) {
    auto stmt = parser.parse(sql);
    if (!stmt.error.empty()) return make_error(stmt.error);
    return execute_parsed(stmt);
}

ResultSet SQLExecutor::execute_parsed(const ParsedStatement& stmt) {
    auto t0 = std::chrono::high_resolution_clock::now();
    ResultSet res;

    switch (stmt.type) {
        case StatementType::CREATE_TABLE:
            res = exec_create_table(*stmt.create_table); break;
        case StatementType::DROP_TABLE:
            res = exec_drop_table(*stmt.drop_table); break;
        case StatementType::INSERT:
            res = exec_insert(*stmt.insert); break;
        case StatementType::SELECT:
            res = exec_select(*stmt.select); break;
        case StatementType::UPDATE:
            res = exec_update(*stmt.update); break;
        case StatementType::DELETE:
            res = exec_delete(*stmt.delete_stmt); break;
        case StatementType::CREATE_INDEX:
            res = exec_create_index(*stmt.create_index); break;
        case StatementType::SHOW_TABLES:
            res = exec_show_tables(); break;
        case StatementType::DESCRIBE:
            res = exec_describe(*stmt.describe_table); break;
        default:
            res = make_error("Unknown statement type");
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    res.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

// ─── DDL ─────────────────────────────────────────────────────────────────────

ResultSet SQLExecutor::exec_create_table(const CreateTableStmt& stmt) {
    if (db.table_exists(stmt.table_name))
        return make_error("Table '" + stmt.table_name + "' already exists");

    TableSchema schema;
    schema.table_name = stmt.table_name;
    schema.columns = stmt.columns;

    for (auto& col : stmt.columns)
        if (col.primary_key) schema.primary_key_col = col.name;

    db.create_table(schema);
    return make_ok("Table '" + stmt.table_name + "' created");
}

ResultSet SQLExecutor::exec_drop_table(const std::string& table) {
    if (!db.table_exists(table))
        return make_error("Table '" + table + "' does not exist");
    db.drop_table(table);
    return make_ok("Table '" + table + "' dropped");
}

ResultSet SQLExecutor::exec_create_index(const CreateIndexStmt& stmt) {
    auto* tbl = db.get_table(stmt.table_name);
    if (!tbl) return make_error("Table not found: " + stmt.table_name);
    if (!tbl->get_schema().has_column(stmt.column_name))
        return make_error("Column not found: " + stmt.column_name);
    tbl->build_index(stmt.column_name);
    return make_ok("Index '" + stmt.index_name + "' created on " +
                   stmt.table_name + "." + stmt.column_name);
}

// ─── DML ─────────────────────────────────────────────────────────────────────

ResultSet SQLExecutor::exec_insert(const InsertStmt& stmt) {
    auto* tbl = db.get_table(stmt.table_name);
    if (!tbl) return make_error("Table not found: " + stmt.table_name);

    const TableSchema& schema = tbl->get_schema();
    int inserted = 0;

    for (auto& val_row : stmt.value_rows) {
        Row row(schema.columns.size(), std::monostate{});

        if (!stmt.columns.empty()) {
            // Named columns
            for (size_t i = 0; i < stmt.columns.size() && i < val_row.size(); i++) {
                int idx = schema.column_index(stmt.columns[i]);
                if (idx < 0) return make_error("Unknown column: " + stmt.columns[i]);
                row[idx] = string_to_value(val_row[i], schema.columns[idx].type);
            }
        } else {
            // Positional
            for (size_t i = 0; i < val_row.size() && i < schema.columns.size(); i++)
                row[i] = string_to_value(val_row[i], schema.columns[i].type);
        }

        // Check NOT NULL constraints
        for (size_t i = 0; i < schema.columns.size(); i++) {
            if (schema.columns[i].not_null &&
                std::holds_alternative<std::monostate>(row[i])) {
                // Apply default if available
                if (schema.columns[i].default_value)
                    row[i] = string_to_value(*schema.columns[i].default_value,
                                             schema.columns[i].type);
                else
                    return make_error("NOT NULL constraint failed: " + schema.columns[i].name);
            }
        }

        tbl->insert_row(row);
        inserted++;
    }

    ResultSet res;
    res.success = true;
    res.rows.push_back({Value((int64_t)inserted)});
    res.columns = {"rows_inserted"};
    return res;
}

ResultSet SQLExecutor::exec_select(const SelectQuery& stmt) {
    auto* tbl = db.get_table(stmt.table);
    if (!tbl) return make_error("Table not found: " + stmt.table);

    const TableSchema& schema = tbl->get_schema();

    // Determine output columns
    std::vector<std::string> out_cols;
    if (stmt.columns.empty() || (stmt.columns.size() == 1 && stmt.columns[0] == "*")) {
        for (auto& c : schema.columns) out_cols.push_back(c.name);
    } else {
        out_cols = stmt.columns;
    }

    // COUNT(*) shortcut
    if (stmt.count_only) {
        auto rows = tbl->scan(stmt.conditions, {});
        ResultSet res;
        res.success = true;
        res.columns = {"COUNT(*)"};
        res.rows.push_back({Value((int64_t)rows.size())});
        return res;
    }

    auto raw_rows = tbl->scan(stmt.conditions, out_cols);

    if (stmt.order_by) {
        // Find sort column in projected columns (may or may not be there)
        int sort_col_in_proj = -1;
        for (int i = 0; i < (int)out_cols.size(); i++)
            if (out_cols[i] == stmt.order_by->column) { sort_col_in_proj = i; break; }

        int schema_col_idx = tbl->get_schema().column_index(stmt.order_by->column);
        DataType sort_type = (schema_col_idx >= 0) ?
            tbl->get_schema().columns[schema_col_idx].type : DataType::TEXT;
        bool asc = stmt.order_by->ascending;

        if (sort_col_in_proj >= 0) {
            // Sort column is in projection - sort directly
            std::stable_sort(raw_rows.begin(), raw_rows.end(),
                [&](const auto& a, const auto& b) {
                    const Value& va = a.second[sort_col_in_proj];
                    const Value& vb = b.second[sort_col_in_proj];
                    if (sort_type == DataType::FLOAT || sort_type == DataType::INTEGER) {
                        double da = 0, db = 0;
                        if (std::holds_alternative<double>(va)) da = std::get<double>(va);
                        else if (std::holds_alternative<int64_t>(va)) da = (double)std::get<int64_t>(va);
                        if (std::holds_alternative<double>(vb)) db = std::get<double>(vb);
                        else if (std::holds_alternative<int64_t>(vb)) db = (double)std::get<int64_t>(vb);
                        return asc ? da < db : da > db;
                    }
                    return asc ? value_less(va, vb) : value_less(vb, va);
                });
        } else if (schema_col_idx >= 0) {
            // Sort column not in projection - use row_id to fetch from storage
            std::stable_sort(raw_rows.begin(), raw_rows.end(),
                [&](const auto& a, const auto& b) {
                    auto ra = tbl->get_row(a.first);
                    auto rb = tbl->get_row(b.first);
                    if (!ra || !rb) return false;
                    const Value& va = (*ra)[schema_col_idx];
                    const Value& vb = (*rb)[schema_col_idx];
                    if (sort_type == DataType::FLOAT || sort_type == DataType::INTEGER) {
                        double da = 0, db = 0;
                        if (std::holds_alternative<double>(va)) da = std::get<double>(va);
                        else if (std::holds_alternative<int64_t>(va)) da = (double)std::get<int64_t>(va);
                        if (std::holds_alternative<double>(vb)) db = std::get<double>(vb);
                        else if (std::holds_alternative<int64_t>(vb)) db = (double)std::get<int64_t>(vb);
                        return asc ? da < db : da > db;
                    }
                    return asc ? value_less(va, vb) : value_less(vb, va);
                });
        }
    }

    // LIMIT
    if (stmt.limit && (int)raw_rows.size() > *stmt.limit)
        raw_rows.resize(*stmt.limit);

    ResultSet res;
    res.success = true;
    res.columns = out_cols;
    for (auto& [id, row] : raw_rows)
        res.rows.push_back(row);
    return res;
}

ResultSet SQLExecutor::exec_update(const UpdateStmt& stmt) {
    auto* tbl = db.get_table(stmt.table_name);
    if (!tbl) return make_error("Table not found: " + stmt.table_name);

    const TableSchema& schema = tbl->get_schema();
    auto rows = tbl->scan(stmt.conditions, {});

    int updated = 0;
    for (auto& [id, _] : rows) {
        std::vector<std::pair<std::string, Value>> updates;
        for (auto& [col, val_str] : stmt.assignments) {
            int idx = schema.column_index(col);
            if (idx < 0) return make_error("Unknown column: " + col);
            updates.emplace_back(col, string_to_value(val_str, schema.columns[idx].type));
        }
        if (tbl->update_row(id, updates)) updated++;
    }

    ResultSet res;
    res.success = true;
    res.columns = {"rows_updated"};
    res.rows.push_back({Value((int64_t)updated)});
    return res;
}

ResultSet SQLExecutor::exec_delete(const DeleteStmt& stmt) {
    auto* tbl = db.get_table(stmt.table_name);
    if (!tbl) return make_error("Table not found: " + stmt.table_name);

    auto rows = tbl->scan(stmt.conditions, {});
    int deleted = 0;
    for (auto& [id, _] : rows)
        if (tbl->delete_row(id)) deleted++;

    ResultSet res;
    res.success = true;
    res.columns = {"rows_deleted"};
    res.rows.push_back({Value((int64_t)deleted)});
    return res;
}

// ─── Utility ─────────────────────────────────────────────────────────────────

ResultSet SQLExecutor::exec_show_tables() {
    ResultSet res;
    res.success = true;
    res.columns = {"Tables in " + db.get_name()};
    for (auto& t : db.list_tables())
        res.rows.push_back({Value(t)});
    return res;
}

ResultSet SQLExecutor::exec_describe(const std::string& table) {
    auto* tbl = db.get_table(table);
    if (!tbl) return make_error("Table not found: " + table);

    ResultSet res;
    res.success = true;
    res.columns = {"Field", "Type", "Null", "Key", "Default"};
    for (auto& col : tbl->get_schema().columns) {
        std::string null_str = col.not_null ? "NO" : "YES";
        std::string key_str  = col.primary_key ? "PRI" :
                               (tbl->has_index(col.name) ? "IDX" : "");
        std::string def_str  = col.default_value.value_or("");
        res.rows.push_back({
            Value(col.name),
            Value(data_type_to_string(col.type)),
            Value(null_str),
            Value(key_str),
            Value(def_str)
        });
    }
    return res;
}

ResultSet SQLExecutor::make_error(const std::string& msg) {
    ResultSet r;
    r.success = false;
    r.error = msg;
    return r;
}

ResultSet SQLExecutor::make_ok(const std::string& msg) {
    ResultSet r;
    r.success = true;
    r.columns = {"status"};
    r.rows.push_back({Value(msg)});
    return r;
}

} // namespace MiniDB
