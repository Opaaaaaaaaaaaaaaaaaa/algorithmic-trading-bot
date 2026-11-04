#include "Engine.h"
#include "MyEWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>

const size_t kGptBatchSize = 100;
const size_t kSentimentDays = 5;

Engine::Engine(APIGpt& g, APIReddit& r, std::atomic<bool>& t, std::atomic<bool>& o, std::atomic<bool>& w, StateStore& s, Alerts& a)
    : gpt(g)
    , reddit(r)
    , flag(true)
    , trigger(t)
    , open(o)
    , wait(w)
    , client(nullptr)
    , state(s)
    , alerts(a)
{
    
    nlohmann::json days = state.get("sentimentDays");
    if (days.is_object()) {
        for (auto& [date, v] : days.items())
            if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number())
                sentimentDays[date] = { v[0].get<double>(), v[1].get<double>() };
    }

    nlohmann::json savedHype = state.get("overnightHype");
    if (savedHype.is_object()) {
        for (auto& [ticker, v] : savedHype.items())
            if (v.is_number()) hype[ticker] = v.get<double>();
    }

    mktOpen = false;
}

void Engine::run()
{
    std::cout << "Engine loop start\n";
    while (flag)
    {
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            if (client)
            {
                bool nowOpen = canTradeNow();

                if (nowOpen != mktOpen)
                {
                    mktOpen = nowOpen;

                    if (mktOpen) open.store(true);
                    else wait.store(true);
                }
            }
        }

        gpt.decay();

        lastLoop.store(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

        bool fetched = false;
        try {
            fetched = reddit.fetchComments();
        }
        catch (const std::exception& e) {
            std::cerr << "Reddit fetch crashed: " << e.what() << "\n";
        }
        if (fetched) alerts.ok("Reddit");
        else alerts.fail("Reddit", "can't fetch comments from Reddit", std::chrono::minutes(5));

        nlohmann::json commentsPayload = reddit.getLatestPayload();

        for (auto& comment : commentsPayload["data"]) {
            std::cout << comment["id"] << " :" << comment["text"] << "\n\n";
        }

        const nlohmann::json& all = commentsPayload["data"];
        for (size_t start = 0; start < all.size(); start += kGptBatchSize) {
            nlohmann::json batch = { {"data", nlohmann::json::array()} };
            size_t end = start + kGptBatchSize;
            if (end > all.size()) end = all.size();
            for (size_t i = start; i < end; ++i)
                batch["data"].push_back(all[i]);

            try {
                nlohmann::json gptResponse = gpt.evaluate(batch.dump());

                std::string content = gptResponse.at("choices").at(0).at("message").at("content");

                nlohmann::json evaluations = nlohmann::json::parse(content);
                alerts.ok("GPT");

                for (auto& ev : evaluations.at("evaluations")) {
                    std::cout << "Id :" << ev["id"] << ", Ticker: " << ev["ticker"] << ", Confidence: " << ev["confidence"] << "\n";
                    if (ev.contains("confidence") && ev["confidence"].is_number()) {
                        std::lock_guard<std::mutex> lock(statsMutex);
                        hourStats.add(ev["confidence"].get<double>());
                        dayStats.add(ev["confidence"].get<double>());
                        addToSentimentDays(ev["confidence"].get<double>());
                        double c = ev["confidence"].get<double>();
                        if (c > 0 && ev.contains("ticker") && ev["ticker"].is_string() && !ev["ticker"].get<std::string>().empty())
                            hype[ev["ticker"].get<std::string>()] += c * c;
                    }
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Bad GPT reply, skipping: " << e.what() << "\n";
                alerts.fail("GPT", "OpenAI calls are failing (out of credits, bad key or bad replies, see the bot window)",
                    std::chrono::minutes(5));
            }
        }

        reddit.saveState();

        {
            std::lock_guard<std::mutex> lock(statsMutex);
            nlohmann::json days = nlohmann::json::object();
            for (auto& [date, w] : sentimentDays) days[date] = { w.first, w.second };
            state.set("sentimentDays", days);
            state.set("overnightHype", hype);
        }

        if (std::chrono::steady_clock::now() - lastHourLog >= std::chrono::hours(1)) {
            lastHourLog = std::chrono::steady_clock::now();
            double ratio = wsbRatio();
            std::lock_guard<std::mutex> lock(statsMutex);
            std::cout << "Last hour GPT: " << hourStats.describe() << "\n";
            std::cout << "WSB 5-day ratio (bullish/bearish): x" << ratio << "\n";
            hourStats = Sentiment();
        }

        if (mktOpen && !rebalancedToday()) trigger.store(true);

        alerts.tick();

        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}

namespace {

using namespace std::chrono;

constexpr year_month_day kNyseHolidays[] = {
    2026y / January / 1,
    2026y / January / 19,
    2026y / February / 16,
    2026y / April / 3,
    2026y / May / 25,
    2026y / June / 19,
    2026y / July / 3,
    2026y / September / 7,
    2026y / November / 26,
    2026y / December / 25,
    2027y / January / 1,
    2027y / January / 18,
    2027y / February / 15,
    2027y / March / 26,
    2027y / May / 31,
    2027y / June / 18,
    2027y / July / 5,
    2027y / September / 6,
    2027y / November / 25,
    2027y / December / 24,
};

constexpr year_month_day kNyseEarlyCloses[] = {
    2026y / November / 27,
    2026y / December / 24,
    2027y / November / 26,
};

constexpr year kLastListedYear = 2027y;

constexpr minutes kTradeStart = 9h + 40min;
constexpr minutes kTradeEnd = 16h;
constexpr minutes kTradeEndEarlyClose = 13h;
constexpr minutes kRebalanceBeforeClose = 10min;

template <size_t N>
bool contains(const year_month_day (&days)[N], const year_month_day& d)
{
    for (const auto& x : days)
        if (x == d) return true;
    return false;
}

}

std::string Engine::todayInNewYork()
{
    zoned_time ny{ "America/New_York", system_clock::now() };
    year_month_day d{ floor<days>(ny.get_local_time()) };

    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u",
        static_cast<int>(d.year()), static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
    return buf;
}

struct NyClock {
    bool tradingDay = false;
    minutes timeOfDay{ 0 };
    minutes close{ 0 };
};

static NyClock nyClock()
{
    zoned_time ny{ "America/New_York", system_clock::now() };
    local_time<minutes> now = floor<minutes>(ny.get_local_time());
    local_days today = floor<days>(now);
    year_month_day date{ today };

    NyClock c;
    c.timeOfDay = now - today;
    c.close = contains(kNyseEarlyCloses, date) ? kTradeEndEarlyClose : kTradeEnd;

    weekday wd{ today };
    if (wd == Saturday || wd == Sunday) return c;

    if (date.year() > kLastListedYear) {
        static bool warned = false;
        if (!warned) {
            std::cout << "WARNING: NYSE holiday list in Engine.cpp ends in "
                      << static_cast<int>(kLastListedYear) << ". Please update it.\n";
            warned = true;
        }
    }

    c.tradingDay = !contains(kNyseHolidays, date);
    return c;
}

bool Engine::canTradeNow()
{
    NyClock c = nyClock();
    return c.tradingDay && c.timeOfDay >= kTradeStart && c.timeOfDay < c.close;
}

bool Engine::rebalancedToday()
{
    std::string today = todayInNewYork();
    for (const char* key : { "lastRebalance", "lastMarketOpen" }) {
        nlohmann::json last = state.get(key);
        if (last.is_string() && last.get<std::string>() == today) return true;
    }
    return false;
}

bool Engine::rebalanceDue()
{
    NyClock c = nyClock();
    return c.tradingDay && c.timeOfDay >= c.close - kRebalanceBeforeClose && c.timeOfDay < c.close && !rebalancedToday();
}

void Engine::markRebalanced()
{
    state.set("lastRebalance", todayInNewYork());
}

std::vector<std::string> Engine::takeTopHype(size_t n)
{
    std::lock_guard<std::mutex> lock(statsMutex);
    std::vector<std::pair<std::string, double>> all(hype.begin(), hype.end());
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::string> top;
    for (size_t i = 0; i < all.size() && i < n; ++i) top.push_back(all[i].first);
    hype.clear();
    state.set("overnightHype", hype);
    return top;
}

void Sentiment::add(double confidence)
{
    if (confidence > 0) { ++bullish; bullishWeight += confidence * confidence; }
    if (confidence < 0) { ++bearish; bearishWeight += confidence * confidence; }
}

std::string Sentiment::describe() const
{
    int total = bullish + bearish;
    if (total == 0) return "no calls";
    double weight = bullishWeight + bearishWeight;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%d bullish / %d bearish calls (%.0f%% bullish, %.0f%% by score)",
        bullish, bearish, 100.0 * bullish / total, weight > 0 ? 100.0 * bullishWeight / weight : 0.0);
    return buf;
}

void Engine::addToSentimentDays(double confidence)
{
    auto& day = sentimentDays[todayInNewYork()];
    if (confidence > 0) day.first += confidence * confidence;
    if (confidence < 0) day.second += confidence * confidence;
    while (sentimentDays.size() > kSentimentDays) sentimentDays.erase(sentimentDays.begin());
}

double Engine::wsbRatio()
{
    std::lock_guard<std::mutex> lock(statsMutex);
    double bullish = 0, bearish = 0;
    for (auto& [date, w] : sentimentDays) { bullish += w.first; bearish += w.second; }
    if (bearish <= 0) return bullish > 0 ? 99.0 : 1.0;
    return bullish / bearish;
}

Sentiment Engine::takeDayStats()
{
    std::lock_guard<std::mutex> lock(statsMutex);
    Sentiment s = dayStats;
    dayStats = Sentiment();
    return s;
}

long long Engine::minutesSinceLastLoop() const
{
    long long last = lastLoop.load();
    if (last == 0) return 0;
    long long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (now - last) / 60;
}

void Engine::setClient(MyEWrapper& c)
{
    std::lock_guard<std::mutex> lock(clientMutex);
    client = &c;
}

void Engine::clearClient()
{
    std::lock_guard<std::mutex> lock(clientMutex);
    client = nullptr;
}