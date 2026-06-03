#include "parser.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace MiniDB {

std::string SQLParser::to_upper(const std::string& s) const {
    std::string u;
    for (char c : s) u += toupper(c);
    return u;
}

bool SQLParser::is_keyword(const std::string& s) const {
    static const std::vector<std::string> kws = {
        "SELECT","FROM","WHERE","INSERT","INTO","VALUES","UPDATE","SET",
        "DELETE","CREATE","DROP","TABLE","INDEX","ON","AND","OR","NOT",
        "NULL","IS","LIKE","ORDER","BY","ASC","DESC","LIMIT","SHOW",
        "TABLES","DESCRIBE","PRIMARY","KEY","UNIQUE","DEFAULT","IN",
        "DISTINCT","COUNT","AS","JOIN","INNER","LEFT","RIGHT","GROUP",
        "HAVING","BETWEEN","TRUE","FALSE","TEXT","INTEGER","INT","FLOAT",
        "BOOLEAN","BOOL","VARCHAR","REAL","DOUBLE"
    };
    std::string u = to_upper(s);
    return std::find(kws.begin(), kws.end(), u) != kws.end();
}

void SQLParser::tokenize(const std::string& sql) {
    tokens.clear();
    pos = 0;
    size_t i = 0;
    while (i < sql.size()) {
        char c = sql[i];

        // Skip whitespace
        if (std::isspace(c)) { i++; continue; }

        // Comment: -- to end of line
        if (c == '-' && i + 1 < sql.size() && sql[i+1] == '-') {
            while (i < sql.size() && sql[i] != '\n') i++;
            continue;
        }

        // String literals
        if (c == '\'' || c == '"') {
            char delim = c;
            std::string tok;
            i++;
            while (i < sql.size() && sql[i] != delim) {
                if (sql[i] == '\\' && i + 1 < sql.size()) { i++; }
                tok += sql[i++];
            }
            i++; // closing quote
            tokens.push_back(tok);
            continue;
        }

        // Operators: !=, <=, >=
        if ((c == '!' || c == '<' || c == '>') && i + 1 < sql.size() && sql[i+1] == '=') {
            tokens.push_back(std::string(1, c) + "=");
            i += 2; continue;
        }

        // Single-char tokens
        if (std::string("(),;=<>*").find(c) != std::string::npos) {
            tokens.push_back(std::string(1, c));
            i++; continue;
        }

        // Words, numbers, and LIKE patterns (allow % and _ in tokens)
        if (std::isalnum(c) || c == '_' || c == '.' || c == '%') {
            std::string tok;
            while (i < sql.size() && (std::isalnum(sql[i]) || sql[i] == '_' || sql[i] == '.' || sql[i] == '%'))
                tok += sql[i++];
            tokens.push_back(tok);
            continue;
        }

        i++; // skip unknown char
    }
}

std::string SQLParser::peek(int offset) const {
    size_t idx = pos + offset;
    if (idx >= tokens.size()) return "";
    return tokens[idx];
}

std::string SQLParser::consume() {
    if (pos >= tokens.size()) return "";
    return tokens[pos++];
}

bool SQLParser::match(const std::string& expected) {
    if (to_upper(peek()) == to_upper(expected)) {
        consume();
        return true;
    }
    return false;
}

bool SQLParser::at_end() const {
    return pos >= tokens.size() || peek() == ";";
}

// ─── Main parse entry ────────────────────────────────────────────────────────

ParsedStatement SQLParser::parse(const std::string& sql) {
    tokenize(sql);

    if (tokens.empty()) {
        ParsedStatement ps;
        ps.error = "Empty statement";
        return ps;
    }

    std::string kw = to_upper(peek());

    if (kw == "CREATE")  return parse_create();
    if (kw == "INSERT")  return parse_insert();
    if (kw == "SELECT")  return parse_select();
    if (kw == "UPDATE")  return parse_update();
    if (kw == "DELETE")  return parse_delete();

    if (kw == "DROP") {
        consume(); // DROP
        match("TABLE");
        ParsedStatement ps;
        ps.type = StatementType::DROP_TABLE;
        ps.drop_table = consume();
        return ps;
    }

    if (kw == "SHOW") {
        consume(); consume(); // SHOW TABLES
        ParsedStatement ps;
        ps.type = StatementType::SHOW_TABLES;
        return ps;
    }

    if (kw == "DESCRIBE" || kw == "DESC") {
        consume();
        ParsedStatement ps;
        ps.type = StatementType::DESCRIBE;
        ps.describe_table = consume();
        return ps;
    }

    ParsedStatement ps;
    ps.error = "Unknown statement: " + peek();
    return ps;
}

// ─── CREATE ──────────────────────────────────────────────────────────────────

ParsedStatement SQLParser::parse_create() {
    consume(); // CREATE
    std::string what = to_upper(consume());

    if (what == "TABLE") {
        ParsedStatement ps;
        ps.type = StatementType::CREATE_TABLE;
        CreateTableStmt stmt;
        stmt.table_name = consume();
        if (peek() != "(") { ps.error = "Expected '(' after table name"; return ps; }
        consume(); // (
        stmt.columns = parse_column_defs();
        ps.create_table = stmt;
        return ps;
    }

    if (what == "INDEX") {
        ParsedStatement ps;
        ps.type = StatementType::CREATE_INDEX;
        CreateIndexStmt stmt;
        stmt.index_name = consume();
        match("ON");
        stmt.table_name = consume();
        consume(); // (
        stmt.column_name = consume();
        consume(); // )
        ps.create_index = stmt;
        return ps;
    }

    ParsedStatement ps;
    ps.error = "Unknown CREATE target: " + what;
    return ps;
}

std::vector<ColumnDef> SQLParser::parse_column_defs() {
    std::vector<ColumnDef> cols;
    while (!at_end() && peek() != ")") {
        ColumnDef col;

        // Check for PRIMARY KEY (col_name) shorthand
        std::string first = to_upper(peek());
        if (first == "PRIMARY") {
            consume(); consume(); // PRIMARY KEY
            consume(); // (
            std::string pk_col = consume();
            consume(); // )
            // Mark column
            for (auto& c : cols)
                if (c.name == pk_col) { c.primary_key = true; break; }
            if (peek() == ",") consume();
            continue;
        }

        col.name = consume();
        col.type = string_to_data_type(consume());

        // Parse constraints
        while (!at_end() && peek() != "," && peek() != ")") {
            std::string kw = to_upper(peek());
            if (kw == "NOT") {
                consume(); consume(); // NOT NULL
                col.not_null = true;
            } else if (kw == "PRIMARY") {
                consume(); consume(); // PRIMARY KEY
                col.primary_key = true;
            } else if (kw == "DEFAULT") {
                consume(); // DEFAULT
                col.default_value = consume();
            } else if (kw == "UNIQUE") {
                consume();
            } else {
                break;
            }
        }
        cols.push_back(col);
        if (peek() == ",") consume();
    }
    consume(); // closing )
    return cols;
}

// ─── INSERT ──────────────────────────────────────────────────────────────────

ParsedStatement SQLParser::parse_insert() {
    consume(); // INSERT
    match("INTO");
    ParsedStatement ps;
    ps.type = StatementType::INSERT;
    InsertStmt stmt;
    stmt.table_name = consume();

    // Optional column list
    if (peek() == "(") {
        consume(); // (
        while (peek() != ")" && !at_end()) {
            stmt.columns.push_back(consume());
            if (peek() == ",") consume();
        }
        consume(); // )
    }

    match("VALUES");

    // Parse one or more value tuples
    while (peek() == "(" || (!at_end() && !stmt.value_rows.empty() && peek() == ",")) {
        if (peek() == ",") consume(); // between tuples
        if (peek() != "(") break;
        consume(); // (
        std::vector<std::string> vals;
        while (peek() != ")" && !at_end()) {
            vals.push_back(consume());
            if (peek() == ",") consume();
        }
        consume(); // )
        stmt.value_rows.push_back(vals);
    }
    ps.insert = stmt;
    return ps;
}

// ─── SELECT ──────────────────────────────────────────────────────────────────

ParsedStatement SQLParser::parse_select() {
    consume(); // SELECT
    ParsedStatement ps;
    ps.type = StatementType::SELECT;
    SelectQuery stmt;

    // Parse column list
    if (to_upper(peek()) == "COUNT" && peek(1) == "(") {
        stmt.count_only = true;
        consume(); consume(); consume(); consume(); // COUNT ( * )
    } else if (peek() == "*") {
        consume();
    } else {
        while (!at_end() && to_upper(peek()) != "FROM") {
            stmt.columns.push_back(consume());
            if (peek() == ",") consume();
        }
    }

    match("FROM");
    stmt.table = consume();

    // WHERE
    if (to_upper(peek()) == "WHERE") {
        consume();
        stmt.conditions = parse_where();
    }

    // ORDER BY
    if (to_upper(peek()) == "ORDER") {
        consume(); // ORDER
        match("BY");
        OrderBy ob;
        ob.column = consume();
        if (to_upper(peek()) == "DESC") { consume(); ob.ascending = false; }
        else if (to_upper(peek()) == "ASC") { consume(); }
        stmt.order_by = ob;
    }

    // LIMIT
    if (to_upper(peek()) == "LIMIT") {
        consume();
        stmt.limit = std::stoi(consume());
    }

    ps.select = stmt;
    return ps;
}

// ─── UPDATE ──────────────────────────────────────────────────────────────────

ParsedStatement SQLParser::parse_update() {
    consume(); // UPDATE
    ParsedStatement ps;
    ps.type = StatementType::UPDATE;
    UpdateStmt stmt;
    stmt.table_name = consume();
    match("SET");

    while (!at_end() && to_upper(peek()) != "WHERE") {
        std::string col = consume();
        consume(); // =
        std::string val = consume();
        stmt.assignments.emplace_back(col, val);
        if (peek() == ",") consume();
    }

    if (to_upper(peek()) == "WHERE") {
        consume();
        stmt.conditions = parse_where();
    }

    ps.update = stmt;
    return ps;
}

// ─── DELETE ──────────────────────────────────────────────────────────────────

ParsedStatement SQLParser::parse_delete() {
    consume(); // DELETE
    match("FROM");
    ParsedStatement ps;
    ps.type = StatementType::DELETE;
    DeleteStmt stmt;
    stmt.table_name = consume();

    if (to_upper(peek()) == "WHERE") {
        consume();
        stmt.conditions = parse_where();
    }

    ps.delete_stmt = stmt;
    return ps;
}

// ─── WHERE ───────────────────────────────────────────────────────────────────

std::vector<Condition> SQLParser::parse_where() {
    std::vector<Condition> conditions;
    while (!at_end()) {
        conditions.push_back(parse_condition());
        if (to_upper(peek()) == "AND") { consume(); continue; }
        break;
    }
    return conditions;
}

Condition SQLParser::parse_condition() {
    Condition cond;
    cond.column = consume();

    std::string op = to_upper(peek());

    if (op == "IS") {
        consume(); // IS
        if (to_upper(peek()) == "NOT") {
            consume(); consume(); // NOT NULL
            cond.op = "IS NOT NULL";
        } else {
            consume(); // NULL
            cond.op = "IS NULL";
        }
        cond.is_null_check = true;
        return cond;
    }

    if (op == "LIKE") {
        consume();
        cond.op = "LIKE";
        cond.value = consume();
        return cond;
    }

    cond.op = consume(); // =, !=, <, >, <=, >=
    cond.value = consume();
    return cond;
}

} // namespace MiniDB
