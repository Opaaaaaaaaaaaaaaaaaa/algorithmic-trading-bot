#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

class Alerts;

class APIPrice {
public:
    APIPrice();

    std::optional<double> get(const std::string& ticker);

    void setAlerts(Alerts& a) { alerts = &a; }
    bool hasKey() const { return !key.empty(); }

private:
    struct Entry {
        std::optional<double> price;   
        std::chrono::steady_clock::time_point fetched;
    };

    std::optional<double> fetch(const std::string& ticker, bool& unknownSymbol);

    std::string key;
    std::string lastError;
    Alerts* alerts = nullptr;
    std::unordered_map<std::string, Entry> cache;
};
