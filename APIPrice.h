#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>


class APIPrice {
public:
    APIPrice();

    std::optional<double> get(const std::string& ticker);

private:
    struct Entry {
        std::optional<double> price;   
        std::chrono::steady_clock::time_point fetched;
    };

    std::optional<double> fetch(const std::string& ticker, bool& unknownSymbol);

    std::string key;
    std::unordered_map<std::string, Entry> cache;
};
