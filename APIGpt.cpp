#include "APIGpt.h"

#include <chrono>
#include <cmath>

// A score keeps 1/3 of its strength after 24 hours (and 1/9 after 48), fading smoothly.
const double kHoursToThird = 24.0;
// Scores smaller than this are dropped.
const double kTinyScore = 0.001;

static long long nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}


APIGpt::APIGpt(StateStore& s)
    : state(s)
{
    body = {
    {"model", "gpt-6-luna"},
    {"reasoning_effort", "medium"},
    {"response_format", {{"type", "json_object"}}},
    {"messages", {
        {{"role", "system"}, {"content", gptRole}},
        {{"role", "user"},   {"content", ""}}
    }}
    };

    nlohmann::json saved = state.get("confidence");
    if (saved.is_object()) {
        for (auto& [ticker, value] : saved.items())
            if (value.is_number()) tickerConfidenceMap[ticker] = value.get<double>();
        std::cout << "Loaded scores for " << tickerConfidenceMap.size() << " tickers\n";
    }

    nlohmann::json at = state.get("confidenceDecayedAt");
    decayedAt = at.is_number() ? at.get<long long>() : nowSeconds();
}

void APIGpt::decay()
{
    std::lock_guard<std::mutex> lock(mapMutex);
    long long now = nowSeconds();
    if (now <= decayedAt) return;
    double hours = (now - decayedAt) / 3600.0;
    decayedAt = now;

    double factor = std::pow(3.0, -hours / kHoursToThird);
    for (auto it = tickerConfidenceMap.begin(); it != tickerConfidenceMap.end();) {
        it->second *= factor;
        if (std::abs(it->second) < kTinyScore) it = tickerConfidenceMap.erase(it);
        else ++it;
    }
    state.set("confidenceDecayedAt", decayedAt);
    saveLocked();
}


void APIGpt::saveLocked()
{
    state.set("confidence", tickerConfidenceMap);
}

nlohmann::json APIGpt::evaluate(const std::string& feed)
{
    body["messages"][1]["content"] = feed;

    auto r = cpr::Post(
        cpr::Url{ "https://api.openai.com/v1/chat/completions" },
        cpr::Header{
            {"Authorization", "Bearer " + api_key},
            {"Content-Type", "application/json"}
        },
        cpr::Body{ body.dump() },
        cpr::Timeout{ 120000 }
    );

    if (r.error) {
        std::cerr << "Error: " << r.error.message << "\n";  
        return nlohmann::json();
    }

    if (r.status_code != 200) {
        std::cerr << "HTTP " << r.status_code << ":\n" << r.text << "\n";
        return nlohmann::json();
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(r.text);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to parse OpenAI response JSON: " << e.what() << "\n";
        std::cerr << "Raw response:\n" << r.text << "\n";
        return nlohmann::json();
    }

    loadTickerConfidenceMap(parsed);
    return parsed;
}

void APIGpt::loadTickerConfidenceMap(nlohmann::json content)
{
    std::lock_guard<std::mutex> lock(mapMutex);

    if (!content.is_object() ||
        !content.contains("choices") ||
        !content["choices"].is_array() ||
        content["choices"].empty() ||
        !content["choices"][0].contains("message") ||
        !content["choices"][0]["message"].is_object() ||    
        !content["choices"][0]["message"].contains("content") ||
        !content["choices"][0]["message"]["content"].is_string())
    {
        std::cerr << "Invalid OpenAI response shape in loadTickerConfidenceMap:\n"
            << content.dump(2) << "\n";
        return;
    }

    std::string evaluationsStr =
        content["choices"][0]["message"]["content"].get<std::string>();

    nlohmann::json evaluationsJson;
    try {
        evaluationsJson = nlohmann::json::parse(evaluationsStr);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to parse model content as JSON: " << e.what() << "\n";
        std::cerr << "Model content was:\n" << evaluationsStr << "\n";
        return;
    }

    if (!evaluationsJson.is_object() ||
        !evaluationsJson.contains("evaluations") ||
        !evaluationsJson["evaluations"].is_array())
    {
        std::cerr << "Model JSON missing valid 'evaluations' array:\n"
            << evaluationsJson.dump(2) << "\n";
        return;
    }

    for(auto& ev : evaluationsJson["evaluations"]) {
        if (!ev.contains("ticker") || !ev["ticker"].is_string()) {
            continue;
        }

        std::string ticker = ev["ticker"];
        double confidence = ev["confidence"];

        confidence *= std::abs(confidence);

        tickerConfidenceMap[ticker] -= confidence;
	}

    saveLocked();
}


void APIGpt::useScore(const std::string& ticker, double amount)
{
    std::lock_guard<std::mutex> lock(mapMutex);
    tickerConfidenceMap[ticker] -= amount;
    saveLocked();
}
 
std::unordered_map<std::string, double> APIGpt::snapshot()
{
    std::lock_guard<std::mutex> lock(mapMutex);
    return tickerConfidenceMap;
}
