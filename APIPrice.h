#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

class Alerts;

// Looks up current stock prices from Finnhub (finnhub.io) over HTTP.
// Needs FINNHUB_KEY in .env.
class APIPrice {
public:
    APIPrice();

    // Current price, or nothing if none is available right now.
    // Unknown symbols (e.g. made-up tickers) also return nothing.
    std::optional<double> get(const std::string& ticker);

    void setAlerts(Alerts& a) { alerts = &a; }
    bool hasKey() const { return !key.empty(); }

private:
    struct Entry {
        std::optional<double> price;   // empty = Finnhub doesn't know this symbol
        std::chrono::steady_clock::time_point fetched;
    };

    std::optional<double> fetch(const std::string& ticker, bool& unknownSymbol);

    std::string key;
    std::string lastError;
    Alerts* alerts = nullptr;
    std::unordered_map<std::string, Entry> cache;
};
