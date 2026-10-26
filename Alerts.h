#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "APITelegram.h"

// Telegram notifications:
//  - problems: one message when something keeps failing, one when it recovers
//  - an hourly summary of filled orders
// Safe to use from both the engine thread and the IBKR thread.
class Alerts {
public:
    // Something is working (again). Sends "working again" if we had alerted about it.
    void ok(const std::string& what);

    // Something failed. Sends one alert if it is still failing after `after`.
    void fail(const std::string& what, const std::string& detail, std::chrono::seconds after);

    // Send a message straight away.
    void now(const std::string& message);

    // Remember a fill for the hourly summary. Partial fills of one order are added up.
    void addFill(long orderId, const std::string& symbol, const std::string& side, double shares, double price);

    // Call often (every few seconds to a minute). Sends due alerts and the hourly summary.
    void tick();

private:
    struct Problem {
        std::chrono::steady_clock::time_point since;
        std::chrono::seconds after{ 0 };
        std::string detail;
        bool alerted = false;
    };
    struct Fill {
        std::string symbol, side;
        double shares = 0, cost = 0;
    };

    void send(const std::string& message);

    std::mutex m;
    std::unordered_map<std::string, Problem> problems;
    std::vector<long> fillOrder;            // order ids in the order they first filled
    std::unordered_map<long, Fill> fills;   // order id -> totals
    std::chrono::steady_clock::time_point lastSummary = std::chrono::steady_clock::now();
    APITelegram telegram;
};
