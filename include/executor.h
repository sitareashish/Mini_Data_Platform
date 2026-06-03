#pragma once
#include "parser.h"
#include "storage.h"
#include <string>
#include <memory>

namespace MiniDB {

class SQLExecutor {
public:
    explicit SQLExecutor(Database& db);

    ResultSet execute(const std::string& sql);
    ResultSet execute_parsed(const ParsedStatement& stmt);

private:
    Database& db;
    SQLParser parser;

    ResultSet exec_create_table(const CreateTableStmt& stmt);
    ResultSet exec_drop_table(const std::string& table);
    ResultSet exec_insert(const InsertStmt& stmt);
    ResultSet exec_select(const SelectQuery& stmt);
    ResultSet exec_update(const UpdateStmt& stmt);
    ResultSet exec_delete(const DeleteStmt& stmt);
    ResultSet exec_create_index(const CreateIndexStmt& stmt);
    ResultSet exec_show_tables();
    ResultSet exec_describe(const std::string& table);

    ResultSet make_error(const std::string& msg);
    ResultSet make_ok(const std::string& msg);
};

} // namespace MiniDB
