#pragma once
#include "schema.h"
#include "storage.h"
#include <string>
#include <vector>
#include <variant>
#include <memory>
#include <optional>

namespace MiniDB {

enum class StatementType {
    CREATE_TABLE,
    DROP_TABLE,
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    CREATE_INDEX,
    SHOW_TABLES,
    DESCRIBE,
    UNKNOWN
};

struct CreateTableStmt {
    std::string table_name;
    std::vector<ColumnDef> columns;
};

struct InsertStmt {
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> value_rows; // multiple rows
};

struct UpdateStmt {
    std::string table_name;
    std::vector<std::pair<std::string, std::string>> assignments; // col = val
    std::vector<Condition> conditions;
};

struct DeleteStmt {
    std::string table_name;
    std::vector<Condition> conditions;
};

struct CreateIndexStmt {
    std::string index_name;
    std::string table_name;
    std::string column_name;
};

struct ParsedStatement {
    StatementType type = StatementType::UNKNOWN;
    std::string error;

    // Statement data
    std::optional<CreateTableStmt>  create_table;
    std::optional<std::string>      drop_table;
    std::optional<InsertStmt>       insert;
    std::optional<SelectQuery>      select;
    std::optional<UpdateStmt>       update;
    std::optional<DeleteStmt>       delete_stmt;
    std::optional<CreateIndexStmt>  create_index;
    std::optional<std::string>      describe_table;
};

class SQLParser {
public:
    ParsedStatement parse(const std::string& sql);

private:
    std::vector<std::string> tokens;
    size_t pos = 0;

    void tokenize(const std::string& sql);
    std::string peek(int offset = 0) const;
    std::string consume();
    bool match(const std::string& expected);
    bool at_end() const;

    ParsedStatement parse_create();
    ParsedStatement parse_insert();
    ParsedStatement parse_select();
    ParsedStatement parse_update();
    ParsedStatement parse_delete();

    std::vector<Condition> parse_where();
    Condition parse_condition();
    std::vector<ColumnDef> parse_column_defs();
    std::string to_upper(const std::string& s) const;
    bool is_keyword(const std::string& s) const;
};

} // namespace MiniDB
