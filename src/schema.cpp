#include "schema.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace MiniDB {

std::string data_type_to_string(DataType dt) {
    switch (dt) {
        case DataType::INTEGER: return "INTEGER";
        case DataType::FLOAT:   return "FLOAT";
        case DataType::TEXT:    return "TEXT";
        case DataType::BOOLEAN: return "BOOLEAN";
    }
    return "UNKNOWN";
}

DataType string_to_data_type(const std::string& s) {
    std::string u;
    for (char c : s) u += toupper(c);
    if (u == "INTEGER" || u == "INT")                  return DataType::INTEGER;
    if (u == "FLOAT" || u == "REAL" || u == "DOUBLE")  return DataType::FLOAT;
    if (u == "TEXT" || u == "VARCHAR" || u == "STRING") return DataType::TEXT;
    if (u == "BOOLEAN" || u == "BOOL")                 return DataType::BOOLEAN;
    throw std::invalid_argument("Unknown data type: " + s);
}

std::string value_to_string(const Value& v) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>)
            return "NULL";
        else if constexpr (std::is_same_v<T, int64_t>)
            return std::to_string(arg);
        else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream ss;
            ss << arg;
            return ss.str();
        }
        else if constexpr (std::is_same_v<T, std::string>)
            return arg;
        else if constexpr (std::is_same_v<T, bool>)
            return arg ? "true" : "false";
        return "";
    }, v);
}

Value string_to_value(const std::string& s, DataType type) {
    if (s == "NULL" || s == "null") return std::monostate{};
    try {
        switch (type) {
            case DataType::INTEGER: return (int64_t)std::stoll(s);
            case DataType::FLOAT:   return std::stod(s);
            case DataType::TEXT:    return s;
            case DataType::BOOLEAN: {
                std::string u;
                for (char c : s) u += toupper(c);
                return (u == "TRUE" || u == "1");
            }
        }
    } catch (...) {
        return std::monostate{};
    }
    return std::monostate{};
}

static bool to_double(const Value& v, double& out) {
    if (std::holds_alternative<int64_t>(v)) { out = (double)std::get<int64_t>(v); return true; }
    if (std::holds_alternative<double>(v))  { out = std::get<double>(v);           return true; }
    return false;
}

bool value_less(const Value& a, const Value& b) {
    if (std::holds_alternative<std::monostate>(a)) return true;
    if (std::holds_alternative<std::monostate>(b)) return false;
    double da, db;
    if (to_double(a, da) && to_double(b, db)) return da < db;
    if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b))
        return std::get<bool>(a) < std::get<bool>(b);
    return value_to_string(a) < value_to_string(b);
}

bool value_equal(const Value& a, const Value& b) {
    if (std::holds_alternative<std::monostate>(a) &&
        std::holds_alternative<std::monostate>(b)) return true;
    if (std::holds_alternative<std::monostate>(a) ||
        std::holds_alternative<std::monostate>(b)) return false;
    return !value_less(a, b) && !value_less(b, a);
}

void ResultSet::print() const {
    if (!success) {
        std::cout << "ERROR: " << error << "\n";
        return;
    }
    if (rows.empty() && columns.empty()) {
        std::cout << "OK (" << std::fixed << std::setprecision(2) << elapsed_ms << "ms)\n";
        return;
    }
    std::vector<size_t> widths(columns.size());
    for (size_t i = 0; i < columns.size(); i++)
        widths[i] = columns[i].size();
    for (auto& row : rows)
        for (size_t i = 0; i < row.size() && i < columns.size(); i++)
            widths[i] = std::max(widths[i], value_to_string(row[i]).size());
    std::string sep = "+";
    for (auto w : widths) sep += std::string(w + 2, '-') + "+";
    std::cout << sep << "\n| ";
    for (size_t i = 0; i < columns.size(); i++) {
        std::cout << std::left << std::setw(widths[i]) << columns[i];
        std::cout << (i + 1 < columns.size() ? " | " : " |");
    }
    std::cout << "\n" << sep << "\n";
    for (auto& row : rows) {
        std::cout << "| ";
        for (size_t i = 0; i < columns.size(); i++) {
            std::string val = (i < row.size()) ? value_to_string(row[i]) : "NULL";
            std::cout << std::left << std::setw(widths[i]) << val;
            std::cout << (i + 1 < columns.size() ? " | " : " |");
        }
        std::cout << "\n";
    }
    std::cout << sep << "\n";
    std::cout << rows.size() << " row(s) in set ("
              << std::fixed << std::setprecision(2) << elapsed_ms << "ms)\n\n";
}

} // namespace MiniDB
