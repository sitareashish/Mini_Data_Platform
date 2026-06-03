#pragma once
#include "storage.h"
#include <string>
#include <vector>
#include <functional>

namespace MiniDB {

struct CopilotConfig {
    std::string api_key;           // Anthropic API key from ANTHROPIC_API_KEY env
    std::string model = "claude-sonnet-4-20250514";
    int max_tokens = 1024;
    bool verbose = false;
};

struct CopilotResult {
    std::string sql;               // Generated SQL
    std::string explanation;       // Human-readable explanation
    bool success = false;
    std::string error;
};

class LLMCopilot {
public:
    explicit LLMCopilot(CopilotConfig config = {});

    // Translate natural language to SQL
    CopilotResult nl_to_sql(const std::string& prompt, const Database& db);

    // Explain a SQL query in plain English
    std::string explain_sql(const std::string& sql, const Database& db);

    // Suggest optimizations for a SQL query
    std::string suggest_optimizations(const std::string& sql, const Database& db);

    bool is_available() const { return !config.api_key.empty(); }

private:
    CopilotConfig config;

    std::string build_schema_context(const Database& db) const;
    std::string build_nl_to_sql_prompt(const std::string& user_prompt,
                                        const std::string& schema_context) const;
    std::string call_anthropic_api(const std::string& prompt) const;
    CopilotResult parse_sql_response(const std::string& response) const;
};

} // namespace MiniDB
