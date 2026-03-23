#include "Engine.h"
#include "MyEWrapper.h"

Engine::Engine(APIGpt& g, APIReddit& r, std::atomic<bool>& t, std::atomic<bool>& o, std::atomic<bool>& w)
    : gpt(g)
    , reddit(r)
    , flag(true)
    , trigger(t)
    , open(o)
    , wait(w)
    , client(nullptr)
{
    mktOpen = canTradeNowLocal();
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
                bool nowOpen = canTradeNowLocal();

                if (nowOpen != mktOpen)
                {
                    mktOpen = nowOpen;

                    if (mktOpen) open.store(true);
                    else wait.store(true);
                }
            }
        }

        if(rebalance.exchange(false, std::memory_order_acq_rel)) {
            gpt.rebalanceTickerConfidenceMap();
        }

        if(tradeCycleComplete.exchange(false, std::memory_order_acq_rel)) {
            gpt.updateTickerConfidenceMap();
        }

        reddit.fetchComments();

        nlohmann::json commentsPayload = reddit.getLatestPayload();

        for (auto& comment : commentsPayload["data"]) {
            std::cout << comment["id"] << " :" << comment["text"] << "\n\n";
        }

        std::string comments = commentsPayload.dump();

        nlohmann::json gptResponse = gpt.evaluate(comments);

        std::string content = gptResponse["choices"][0]["message"]["content"];

        nlohmann::json evaluations = nlohmann::json::parse(content);

        for (auto& ev : evaluations["evaluations"]) {
            std::cout << "Id :" << ev["id"] << ", Ticker: " << ev["ticker"] << ", Confidence: " << ev["confidence"] << "\n";
        }

        if(mktOpen) trigger.store(true);

        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}

void Engine::setFlag(bool f)
{
    flag.store(f);
}

bool Engine::isWeekday(const std::tm& tm)
{
    return tm.tm_wday >= 1 && tm.tm_wday <= 5;
}

bool Engine::isUsMarketHours(const std::tm& tm)
{
    int hour = tm.tm_hour;
    int min = tm.tm_min;

    if (hour < 15) return false;
    if (hour == 15 && min < 28) return false;

    if (hour > 22) return false;
    if (hour == 21 && min > 58) return false;

    return true;
}

bool Engine::canTradeNowLocal()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};

#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    if (!isWeekday(local)) return false;
    if (!isUsMarketHours(local)) return false;

    return true;
}

void Engine::notifyTradeCycleComplete()
{
    tradeCycleComplete.store(true, std::memory_order_release);
}


void Engine::notifyRebalanceMap()
{
    rebalance.store(true, std::memory_order_release);   
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