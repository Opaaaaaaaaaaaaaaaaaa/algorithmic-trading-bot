#include "APIPrice.h"

#include <cstdlib>
#include <iostream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using namespace std::chrono;

// Reuse a price for this long before asking again.
const auto kFresh = seconds(60);
// If Finnhub can't be reached, fall back to a cached price up to this old.
const auto kFallback = minutes(30);
// Don't re-ask about a symbol Finnhub doesn't know for this long.
const auto kUnknownRetry = hours(1);

APIPrice::APIPrice()
{
    const char* k = std::getenv("FINNHUB_KEY");
    if (k) key = k;
    else std::cerr << "WARNING: FINNHUB_KEY is not set, no prices means no trades\n";
}

std::optional<double> APIPrice::get(const std::string& ticker)
{
    auto now = steady_clock::now();
    auto it = cache.find(ticker);

    if (it != cache.end()) {
        auto age = now - it->second.fetched;
        if (it->second.price && age < kFresh) return it->second.price;
        if (!it->second.price && age < kUnknownRetry) return std::nullopt;
    }

    bool unknownSymbol = false;
    std::optional<double> price = fetch(ticker, unknownSymbol);

    if (price || unknownSymbol) {
        cache[ticker] = { price, now };
        if (unknownSymbol) std::cout << "No price for " << ticker << " (unknown symbol), skipping it\n";
        return price;
    }

    // Lookup failed (network, rate limit...). Use a recent cached price if there is one.
    if (it != cache.end() && it->second.price && now - it->second.fetched < kFallback)
        return it->second.price;

    return std::nullopt;
}

std::optional<double> APIPrice::fetch(const std::string& ticker, bool& unknownSymbol)
{
    if (key.empty()) return std::nullopt;

    cpr::Response r = cpr::Get(
        cpr::Url{ "https://finnhub.io/api/v1/quote" },
        cpr::Parameters{ {"symbol", ticker}, {"token", key} },
        cpr::Timeout{ 5000 }
    );

    if (r.status_code != 200) {
        std::cerr << "Price lookup for " << ticker << " failed: HTTP " << r.status_code << "\n";
        return std::nullopt;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(r.text);
        // "c" is the current price. Finnhub returns 0 for symbols it doesn't know.
        if (j.contains("c") && j["c"].is_number()) {
            double c = j["c"].get<double>();
            if (c > 0) return c;
            unknownSymbol = true;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Price lookup for " << ticker << " returned bad JSON: " << e.what() << "\n";
    }
    return std::nullopt;
}
