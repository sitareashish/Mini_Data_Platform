#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <stdexcept>

namespace MiniDB {

enum class DataType {
    INTEGER,
    FLOAT,
    TEXT,
    BOOLEAN
};

std::string data_type_to_string(DataType dt);
DataType string_to_data_type(const std::string& s);

struct ColumnDef {
    std::string name;
    DataType type;
    bool not_null    = false;
    bool primary_key = false;
    std::optional<std::string> default_value;
};

struct TableSchema {
    std::string table_name;
    std::vector<ColumnDef> columns;
    std::string primary_key_col;

    int column_index(const std::string& name) const {
        for (int i = 0; i < (int)columns.size(); i++)
            if (columns[i].name == name) return i;
        return -1;
    }

    bool has_column(const std::string& name) const {
        return column_index(name) >= 0;
    }
};

// A single cell value
using Value = std::variant<std::monostate, int64_t, double, std::string, bool>;

std::string value_to_string(const Value& v);
Value string_to_value(const std::string& s, DataType type);
bool value_less(const Value& a, const Value& b);
bool value_equal(const Value& a, const Value& b);

// A row is a vector of values in column order
using Row = std::vector<Value>;

// Result set from a query
struct ResultSet {
    std::vector<std::string> columns;
    std::vector<Row> rows;
    std::string error;
    bool success = true;
    double elapsed_ms = 0.0;

    void print() const;
    size_t row_count() const { return rows.size(); }
};

} // namespace MiniDB
