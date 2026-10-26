#pragma once

#include <mutex>
#include <string>
#include <nlohmann/json.hpp>


class StateStore {
public:
    explicit StateStore(std::string path);

    
    nlohmann::json get(const std::string& key) const;

    
    void set(const std::string& key, nlohmann::json value);

private:
    void write();

    std::string path;
    nlohmann::json data;
    mutable std::mutex m;
};
