#include "Engine.h"
#include "MyEWrapper.h"

#include <chrono>
#include <cstdio>
#include <iostream>

const size_t kGptBatchSize = 100;

Engine::Engine(APIGpt& g, APIReddit& r, std::atomic<bool>& t, std::atomic<bool>& o, std::atomic<bool>& w, StateStore& s)
    : gpt(g)
    , reddit(r)
    , flag(true)
    , trigger(t)
    , open(o)
    , wait(w)
    , client(nullptr)
    , state(s)
{
    
    nlohmann::json last = state.get("lastMarketOpen");
    bool alreadyOpenedToday = last.is_string() && last.get<std::string>() == todayInNewYork();
    mktOpen = canTradeNow() && alreadyOpenedToday;
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

        reddit.fetchComments();

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

                for (auto& ev : evaluations.at("evaluations")) {
                    std::cout << "Id :" << ev["id"] << ", Ticker: " << ev["ticker"] << ", Confidence: " << ev["confidence"] << "\n";
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Bad GPT reply, skipping: " << e.what() << "\n";
            }
        }


        reddit.saveState();

        if(mktOpen) trigger.store(true);

        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}


namespace {

using namespace std::chrono;

// NYSE full-day closures. Source: nyse.com/markets/hours-calendars.
// Update this list once a year.
constexpr year_month_day kNyseHolidays[] = {
    // 2026
    2026y / January / 1,    // New Year's Day
    2026y / January / 19,   // Martin Luther King Jr. Day
    2026y / February / 16,  // Washington's Birthday
    2026y / April / 3,      // Good Friday
    2026y / May / 25,       // Memorial Day
    2026y / June / 19,      // Juneteenth
    2026y / July / 3,       // Independence Day (observed)
    2026y / September / 7,  // Labor Day
    2026y / November / 26,  // Thanksgiving Day
    2026y / December / 25,  // Christmas Day
    // 2027
    2027y / January / 1,    // New Year's Day
    2027y / January / 18,   // Martin Luther King Jr. Day
    2027y / February / 15,  // Washington's Birthday
    2027y / March / 26,     // Good Friday
    2027y / May / 31,       // Memorial Day
    2027y / June / 18,      // Juneteenth (observed)
    2027y / July / 5,       // Independence Day (observed)
    2027y / September / 6,  // Labor Day
    2027y / November / 25,  // Thanksgiving Day
    2027y / December / 24,  // Christmas Day (observed)
};

// NYSE early closes (13:00 New York time).
constexpr year_month_day kNyseEarlyCloses[] = {
    2026y / November / 27,  // Day after Thanksgiving
    2026y / December / 24,  // Christmas Eve
    2027y / November / 26,  // Day after Thanksgiving
};

constexpr year kLastListedYear = 2027y;

// Regular NYSE session, New York time: open 9:30, close 16:00 (13:00 on early-close days).
constexpr minutes kTradeStart = 9h + 30min;
constexpr minutes kTradeEnd = 16h;
constexpr minutes kTradeEndEarlyClose = 13h;

template <size_t N>
bool contains(const year_month_day (&days)[N], const year_month_day& d)
{
    for (const auto& x : days)
        if (x == d) return true;
    return false;
}

} // namespace

std::string Engine::todayInNewYork()
{
    zoned_time ny{ "America/New_York", system_clock::now() };
    year_month_day d{ floor<days>(ny.get_local_time()) };

    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u",
        static_cast<int>(d.year()), static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
    return buf;
}

bool Engine::canTradeNow()
{
    // Work in New York time so US daylight saving and the machine's own
    // time zone don't matter.
    zoned_time ny{ "America/New_York", system_clock::now() };
    local_time<minutes> now = floor<minutes>(ny.get_local_time());
    local_days today = floor<days>(now);
    year_month_day date{ today };

    weekday wd{ today };
    if (wd == Saturday || wd == Sunday) return false;

    if (date.year() > kLastListedYear) {
        static bool warned = false;
        if (!warned) {
            std::cout << "WARNING: NYSE holiday list in Engine.cpp ends in "
                      << static_cast<int>(kLastListedYear) << ". Please update it.\n";
            warned = true;
        }
    }

    if (contains(kNyseHolidays, date)) return false;

    minutes timeOfDay = now - today;
    minutes end = contains(kNyseEarlyCloses, date) ? kTradeEndEarlyClose : kTradeEnd;

    return timeOfDay >= kTradeStart && timeOfDay < end;
}

void Engine::notifyRebalanceMap()
{
    gpt.rebalanceTickerConfidenceMap();
    state.set("lastMarketOpen", todayInNewYork());
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