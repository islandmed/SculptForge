// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every editor operation is a named command with typed parameters. The UI,
// keyboard shortcuts, future scripting and the bring-your-own-AI assistant
// all go through this registry, so anything an AI does is the same undoable
// action a user could perform. `toolSchemaJson()` exports the registry in the
// JSON-schema "tools" shape used by LLM function-calling APIs.
#pragma once

#include "core/Document.h"

#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace sf {

using ArgValue = std::variant<bool, double, std::string>;
using Args = std::map<std::string, ArgValue>;

struct ParamSpec {
    std::string name;
    enum class Type { Bool, Number, String } type;
    std::string description;
    bool required = false;
};

struct CommandResult {
    bool ok = true;
    std::string message;
};

struct Command {
    std::string name;
    std::string description;
    std::vector<ParamSpec> params;
    std::function<CommandResult(Document&, const Args&)> run;
};

class CommandRegistry {
public:
    CommandRegistry(); // registers the built-in commands

    void add(Command cmd);
    void remove(const std::string& name); // every command with this name (none: nothing happens)
    const Command* find(const std::string& name) const;
    const std::vector<Command>& all() const { return commands_; }

    CommandResult execute(Document& doc, const std::string& name, const Args& args = {}) const;
    std::string toolSchemaJson() const;

private:
    std::vector<Command> commands_;
};

// Argument helpers with defaults.
double argNumber(const Args& args, const std::string& key, double fallback);
bool argBool(const Args& args, const std::string& key, bool fallback);
std::string argString(const Args& args, const std::string& key, const std::string& fallback = {});

} // namespace sf
