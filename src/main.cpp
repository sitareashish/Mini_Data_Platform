#include "executor.h"
#include "llm_copilot.h"
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>

using namespace MiniDB;

// ─── ANSI colours ────────────────────────────────────────────────────────────
#ifdef _WIN32
#define CLR_RESET  ""
#define CLR_CYAN   ""
#define CLR_YELLOW ""
#define CLR_GREEN  ""
#define CLR_RED    ""
#define CLR_BOLD   ""
#else
#define CLR_RESET  "\033[0m"
#define CLR_CYAN   "\033[36m"
#define CLR_YELLOW "\033[33m"
#define CLR_GREEN  "\033[32m"
#define CLR_RED    "\033[31m"
#define CLR_BOLD   "\033[1m"
#endif

static void print_banner() {
    std::cout << CLR_CYAN << CLR_BOLD;
    std::cout << R"(
  ╔══════════════════════════════════════════════════════╗
  ║         Mini Data Platform  v1.0                     ║
  ║   Custom C++ SQL Engine + LLM Copilot               ║
  ╚══════════════════════════════════════════════════════╝
)" << CLR_RESET;
    std::cout << "  Type " << CLR_YELLOW << "\\help" << CLR_RESET
              << " for commands, " << CLR_YELLOW << "\\demo" << CLR_RESET
              << " to run demo, " << CLR_YELLOW << "\\quit" << CLR_RESET
              << " to exit\n\n";
}

static void print_help() {
    std::cout << CLR_YELLOW << "\n─── Commands ──────────────────────────────────────\n" << CLR_RESET;
    std::cout << "  \\help              Show this help\n";
    std::cout << "  \\demo              Run built-in demo dataset\n";
    std::cout << "  \\tables            List all tables\n";
    std::cout << "  \\ask <question>    NL → SQL via LLM Copilot\n";
    std::cout << "  \\explain <sql>     Explain SQL in plain English\n";
    std::cout << "  \\source <file>     Execute SQL from file\n";
    std::cout << "  \\quit              Exit\n";
    std::cout << "\n  SQL statements end with ; or press Enter twice\n\n";
}

static void run_demo(SQLExecutor& exec) {
    std::cout << CLR_GREEN << "\n▶ Running Demo Dataset...\n" << CLR_RESET;

    std::vector<std::string> demo_sql = {
        // Create tables
        "CREATE TABLE employees ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  department TEXT,"
        "  salary FLOAT,"
        "  active BOOLEAN"
        ");",

        "CREATE TABLE departments ("
        "  dept_id INTEGER PRIMARY KEY,"
        "  dept_name TEXT NOT NULL,"
        "  budget FLOAT"
        ");",

        "CREATE TABLE products ("
        "  product_id INTEGER PRIMARY KEY,"
        "  product_name TEXT NOT NULL,"
        "  category TEXT,"
        "  price FLOAT,"
        "  stock INTEGER"
        ");",

        // Insert employees
        "INSERT INTO employees (id, name, department, salary, active) VALUES "
        "(1, 'Alice Johnson', 'Engineering', 95000.00, true),"
        "(2, 'Bob Smith', 'Engineering', 88000.00, true),"
        "(3, 'Carol White', 'Marketing', 72000.00, true),"
        "(4, 'David Brown', 'HR', 65000.00, false),"
        "(5, 'Eve Davis', 'Engineering', 105000.00, true),"
        "(6, 'Frank Miller', 'Marketing', 78000.00, true),"
        "(7, 'Grace Lee', 'Finance', 91000.00, true),"
        "(8, 'Henry Wilson', 'HR', 68000.00, true);",

        // Insert departments
        "INSERT INTO departments VALUES "
        "(1, 'Engineering', 500000.00),"
        "(2, 'Marketing', 200000.00),"
        "(3, 'HR', 150000.00),"
        "(4, 'Finance', 300000.00);",

        // Insert products
        "INSERT INTO products VALUES "
        "(101, 'Laptop Pro', 'Electronics', 1299.99, 50),"
        "(102, 'Mechanical Keyboard', 'Electronics', 149.99, 200),"
        "(103, 'USB-C Hub', 'Electronics', 49.99, 500),"
        "(104, 'Ergonomic Chair', 'Furniture', 599.99, 30),"
        "(105, 'Standing Desk', 'Furniture', 899.99, 15),"
        "(106, 'Monitor 27\"', 'Electronics', 449.99, 75);",

        // Build indexes
        "CREATE INDEX idx_dept ON employees (department);",
        "CREATE INDEX idx_salary ON employees (salary);",
        "CREATE INDEX idx_category ON products (category);",
    };

    for (auto& sql : demo_sql) {
        auto res = exec.execute(sql);
        if (!res.success)
            std::cout << CLR_RED << "  Error: " << res.error << CLR_RESET << "\n";
    }
    std::cout << CLR_GREEN << "  ✓ Demo data loaded: employees(8), departments(4), products(6)\n\n" << CLR_RESET;

    // Run sample queries
    std::vector<std::pair<std::string, std::string>> queries = {
        {"Show all active engineers sorted by salary",
         "SELECT name, salary FROM employees WHERE department = Engineering AND active = true ORDER BY salary DESC;"},
        {"Count employees per department",
         "SELECT department FROM employees WHERE active = true ORDER BY department;"},
        {"Find expensive electronics",
         "SELECT product_name, price FROM products WHERE category = Electronics AND price > 200 ORDER BY price DESC;"},
        {"Find employees with high salaries",
         "SELECT name, department, salary FROM employees WHERE salary >= 90000 ORDER BY salary DESC;"},
        {"Search employees by name pattern",
         "SELECT name, department FROM employees WHERE name LIKE %son%;"},
    };

    for (auto& [desc, sql] : queries) {
        std::cout << CLR_YELLOW << "  ▷ " << desc << CLR_RESET << "\n";
        std::cout << "    SQL: " << sql << "\n";
        auto res = exec.execute(sql);
        res.print();
    }
}

int main(int argc, char* argv[]) {
    print_banner();

    Database db("minidb");
    SQLExecutor exec(db);
    LLMCopilot copilot;

    if (copilot.is_available())
        std::cout << CLR_GREEN << "  ✓ LLM Copilot active (ANTHROPIC_API_KEY found)\n\n" << CLR_RESET;
    else
        std::cout << CLR_YELLOW << "  ⚠ LLM Copilot inactive (set ANTHROPIC_API_KEY to enable \\ask)\n\n" << CLR_RESET;

    // If script file passed as argument, run it
    if (argc > 1) {
        std::ifstream f(argv[1]);
        if (!f) { std::cerr << "Cannot open: " << argv[1] << "\n"; return 1; }
        std::string sql, line;
        while (std::getline(f, line)) {
            sql += line + "\n";
            if (!line.empty() && line.back() == ';') {
                auto res = exec.execute(sql);
                res.print();
                sql.clear();
            }
        }
        return 0;
    }

    // Interactive REPL
    std::string line, accumulated;
    std::cout << "minidb> ";
    std::cout.flush();

    while (std::getline(std::cin, line)) {
        // Trim
        while (!line.empty() && std::isspace(line.front())) line.erase(0, 1);
        while (!line.empty() && std::isspace(line.back())) line.pop_back();

        if (line.empty()) {
            if (!accumulated.empty()) {
                auto res = exec.execute(accumulated);
                res.print();
                accumulated.clear();
            }
            std::cout << "minidb> ";
            std::cout.flush();
            continue;
        }

        // Special commands
        if (line == "\\quit" || line == "\\q" || line == "exit") break;
        if (line == "\\help") { print_help(); std::cout << "minidb> "; continue; }
        if (line == "\\demo") { run_demo(exec); std::cout << "minidb> "; continue; }
        if (line == "\\tables") {
            auto res = exec.execute("SHOW TABLES");
            res.print();
            std::cout << "minidb> ";
            continue;
        }

        if (line.substr(0, 4) == "\\ask") {
            std::string question = line.size() > 5 ? line.substr(5) : "";
            if (question.empty()) {
                std::cout << "Usage: \\ask <natural language question>\n";
            } else {
                std::cout << "  Calling LLM Copilot...\n";
                auto result = copilot.nl_to_sql(question, db);
                if (result.success) {
                    std::cout << CLR_CYAN << "  Generated SQL: " << CLR_RESET << result.sql << "\n";
                    std::cout << CLR_CYAN << "  Explanation:   " << CLR_RESET << result.explanation << "\n";
                    std::cout << "\n  Execute? [y/n]: ";
                    std::string yn;
                    std::getline(std::cin, yn);
                    if (!yn.empty() && (yn[0] == 'y' || yn[0] == 'Y')) {
                        auto res = exec.execute(result.sql);
                        res.print();
                    }
                } else {
                    std::cout << CLR_RED << "  Error: " << result.error << CLR_RESET << "\n";
                }
            }
            std::cout << "minidb> ";
            continue;
        }

        if (line.substr(0, 8) == "\\explain") {
            std::string sql = line.size() > 9 ? line.substr(9) : "";
            std::cout << copilot.explain_sql(sql, db) << "\n";
            std::cout << "minidb> ";
            continue;
        }

        // Accumulate SQL
        accumulated += line + " ";

        // Execute on semicolon
        if (!line.empty() && line.back() == ';') {
            auto res = exec.execute(accumulated);
            res.print();
            accumulated.clear();
        }

        std::cout << (accumulated.empty() ? "minidb> " : "      > ");
        std::cout.flush();
    }

    std::cout << "\nGoodbye!\n";
    return 0;
}
