#pragma once

#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

// Keeps the bot's memory (seen comments, ticker scores, last market open)
// in a JSON file, so stopping and restarting the bot picks up where it left off.
class StateStore {
public:
    explicit StateStore(std::string path);

    // Returns the saved value for key, or null if there is none.
    nlohmann::json get(const std::string& key) const;

    // Saves value under key and writes the file straight away.
    void set(const std::string& key, nlohmann::json value);

private:
    void write();

    std::string path;
    nlohmann::json data;
    mutable std::mutex m;
};
