#pragma once

#include <string>
#include <variant>
#include <vector>

namespace server::command {

struct SelectCommand {
    std::string table{};
    std::vector<std::string> columns{};
};

struct InsertCommand {
    std::string table{};
    std::vector<std::string> values{};
};

using Command = std::variant<SelectCommand, InsertCommand>;

} // namespace server::command

namespace server::result {

struct Row {
    std::vector<std::string> cells{};
};

struct Result {
    bool ok = false;
    std::string message{};
    std::vector<Row> rows{};
};

} // namespace server::result
