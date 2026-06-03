#include "llm_copilot.h"
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace MiniDB {

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

static std::string extract_field(const std::string& json, const std::string& field) {
    std::string needle = "\"" + field + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    std::string value;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '"') break;
        if (c == '\\' && pos + 1 < json.size()) {
            char next = json[++pos];
            switch (next) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case '"':  value += '"';  break;
                case '\\': value += '\\'; break;
                case '/':  value += '/';  break;
                default:   value += next; break;
            }
        } else {
            value += c;
        }
        pos++;
    }
    return value;
}

// ─── LLMCopilot ──────────────────────────────────────────────────────────────

LLMCopilot::LLMCopilot(CopilotConfig config) : config(std::move(config)) {
    // Load from GEMINI_API_KEY first, then ANTHROPIC_API_KEY as fallback
    if (this->config.api_key.empty()) {
        const char* gemini = std::getenv("GEMINI_API_KEY");
        if (gemini) this->config.api_key = gemini;
    }
    if (this->config.api_key.empty()) {
        const char* anthropic = std::getenv("ANTHROPIC_API_KEY");
        if (anthropic) this->config.api_key = anthropic;
    }
    // Detect which provider based on key prefix
    if (!this->config.api_key.empty() &&
        this->config.api_key.substr(0, 7) != "sk-ant-") {
        this->config.model = "gemini"; // use gemini endpoint
    }
}


std::string LLMCopilot::build_schema_context(const Database& db) const {
    std::ostringstream ss;
    ss << "Database: " << db.get_name() << "\n\nTables:\n";
    for (auto& tname : db.list_tables()) {
        auto* tbl = db.get_table(tname);
        if (!tbl) continue;
        ss << "\nCREATE TABLE " << tname << " (\n";
        auto& cols = tbl->get_schema().columns;
        for (size_t i = 0; i < cols.size(); i++) {
            ss << "  " << cols[i].name << " " << data_type_to_string(cols[i].type);
            if (cols[i].primary_key) ss << " PRIMARY KEY";
            if (cols[i].not_null)    ss << " NOT NULL";
            if (i + 1 < cols.size()) ss << ",";
            ss << "\n";
        }
        ss << ");\n-- " << tbl->row_count() << " rows\n";
    }
    return ss.str();
}

std::string LLMCopilot::build_nl_to_sql_prompt(const std::string& user_prompt,
                                                  const std::string& schema_context) const {
    return R"(You are a SQL expert for a custom engine called MiniDB.

Supported SQL:
- SELECT [cols|*] FROM table [WHERE cond] [ORDER BY col [ASC|DESC]] [LIMIT n]
- INSERT INTO table [(cols)] VALUES (vals)
- UPDATE table SET col=val [WHERE cond]
- DELETE FROM table [WHERE cond]
- CREATE TABLE table (col type [NOT NULL] [PRIMARY KEY], ...)
- DROP TABLE table / CREATE INDEX name ON table (col) / SHOW TABLES / DESCRIBE table

Types: INTEGER, FLOAT, TEXT, BOOLEAN
WHERE operators: =, !=, <, >, <=, >=, LIKE, IS NULL, IS NOT NULL
Multiple WHERE conditions: AND only.

)" + schema_context + R"(
User request: )" + user_prompt + R"(

Reply with ONLY this JSON, no markdown, no extra text:
{"sql": "YOUR_SQL_HERE", "explanation": "one sentence description"})";
}

std::string LLMCopilot::call_anthropic_api(const std::string& prompt) const {
    std::string tmp_payload  = "/tmp/minidb_payload.json";
    std::string tmp_response = "/tmp/minidb_response.json";

    std::string cmd;

    // ── Gemini API ────────────────────────────────────────────────────────
    if (config.model == "gemini") {
        std::string payload =
            "{\"contents\":[{\"parts\":[{\"text\":\""
            + json_escape(prompt) + "\"}]}],"
            "\"generationConfig\":{\"maxOutputTokens\":1024}}";

        {
            std::ofstream f(tmp_payload);
            if (!f) throw std::runtime_error("Cannot write temp file");
            f << payload;
        }

        cmd = "curl -s -X POST "
              "\"https://generativelanguage.googleapis.com/v1beta/models/"
              "gemini-2.0-flash-lite:generateContent?key=" + config.api_key + "\""
              " -H \"Content-Type: application/json\""
              " -d @" + tmp_payload +
              " -o " + tmp_response +
              " 2>/dev/null";

    // ── Anthropic API ─────────────────────────────────────────────────────
    } else {
        std::string payload =
            "{\"model\":\"claude-haiku-4-5-20251001\","
            "\"max_tokens\":1024,"
            "\"messages\":[{\"role\":\"user\",\"content\":\""
            + json_escape(prompt) + "\"}]}";

        {
            std::ofstream f(tmp_payload);
            if (!f) throw std::runtime_error("Cannot write temp file");
            f << payload;
        }

        cmd = "curl -s -X POST https://api.anthropic.com/v1/messages"
              " -H \"Content-Type: application/json\""
              " -H \"x-api-key: " + config.api_key + "\""
              " -H \"anthropic-version: 2023-06-01\""
              " -d @" + tmp_payload +
              " -o " + tmp_response +
              " 2>/dev/null";
    }

    int rc = system(cmd.c_str());
    (void)rc;

    std::ifstream f(tmp_response);
    if (!f) throw std::runtime_error("No response from API");
    std::string result((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    if (config.verbose)
        std::cout << "[LLM] Raw response: " << result << "\n";

    return result;
}

CopilotResult LLMCopilot::parse_sql_response(const std::string& api_response) const {
    CopilotResult res;

    // ── Extract model text ────────────────────────────────────────────────
    // Gemini response: {"candidates":[{"content":{"parts":[{"text":"..."}]}}]}
    // Anthropic response: {"content":[{"type":"text","text":"..."}]}
    std::string model_text = extract_field(api_response, "text");

    // ── Parse {"sql":"...","explanation":"..."} from model output ─────────
    for (const std::string& src : {model_text, api_response}) {
        if (src.empty()) continue;
        std::string sql  = extract_field(src, "sql");
        std::string expl = extract_field(src, "explanation");
        if (!sql.empty()) {
            res.sql = sql;
            res.explanation = expl;
            res.success = true;
            return res;
        }
    }

    // ── Last resort: find a raw SQL keyword in the text ───────────────────
    for (const std::string& kw : {"SELECT ", "INSERT ", "UPDATE ", "DELETE ", "CREATE ", "DROP "}) {
        size_t p = model_text.find(kw);
        if (p == std::string::npos) p = api_response.find(kw);
        if (p != std::string::npos) {
            const std::string& src = (model_text.find(kw) != std::string::npos)
                                     ? model_text : api_response;
            size_t end = src.find('\n', p);
            if (end == std::string::npos) end = src.size();
            res.sql = src.substr(p, end - p);
            while (!res.sql.empty() && std::isspace(res.sql.back()))
                res.sql.pop_back();
            res.explanation = "Extracted from response";
            res.success = true;
            return res;
        }
    }

    res.success = false;
    res.error = "Could not extract SQL.\nAPI said: " + api_response.substr(0, 300);
    return res;
}

CopilotResult LLMCopilot::nl_to_sql(const std::string& prompt, const Database& db) {
    if (!is_available()) {
        CopilotResult res;
        res.success = false;
        res.error = "LLM Copilot unavailable: set GEMINI_API_KEY environment variable";
        return res;
    }
    try {
        std::string schema      = build_schema_context(db);
        std::string full_prompt = build_nl_to_sql_prompt(prompt, schema);
        std::string response    = call_anthropic_api(full_prompt);
        return parse_sql_response(response);
    } catch (const std::exception& e) {
        CopilotResult res;
        res.success = false;
        res.error = std::string("API error: ") + e.what();
        return res;
    }
}

std::string LLMCopilot::explain_sql(const std::string& sql, const Database& db) {
    if (!is_available()) return "LLM unavailable";
    std::string schema = build_schema_context(db);
    std::string prompt = "Given this schema:\n" + schema + "\nExplain in one sentence:\n" + sql;
    try {
        std::string response = call_anthropic_api(prompt);
        std::string text = extract_field(response, "text");
        return text.empty() ? response.substr(0, 300) : text;
    } catch (...) { return "Could not explain query"; }
}

std::string LLMCopilot::suggest_optimizations(const std::string& sql, const Database& db) {
    if (!is_available()) return "LLM unavailable";
    std::string schema = build_schema_context(db);
    std::string prompt = "Given this schema:\n" + schema + "\nSuggest optimizations:\n" + sql;
    try {
        std::string response = call_anthropic_api(prompt);
        std::string text = extract_field(response, "text");
        return text.empty() ? response.substr(0, 500) : text;
    } catch (...) { return "Could not generate suggestions"; }
}

} // namespace MiniDB
