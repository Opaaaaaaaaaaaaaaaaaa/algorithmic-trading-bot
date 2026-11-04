#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "APITelegram.h"

std::string dollars(double amount, const std::string& sign = "$");

class Alerts {
public:
    void ok(const std::string& what);

    void fail(const std::string& what, const std::string& detail, std::chrono::seconds after);

    void now(const std::string& message);

    void publish(const std::string& message);

    std::string takePublicFills(size_t maxLines);

    void addFill(long orderId, const std::string& symbol, const std::string& side, double shares, double price);

    void tick();

    std::string takeDayFills();

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
    std::vector<long> fillOrder;
    std::unordered_map<long, Fill> fills;
    std::chrono::steady_clock::time_point lastSummary = std::chrono::steady_clock::now();
    std::vector<long> publicOrder;
    std::unordered_map<long, Fill> publicFills;
    std::unordered_set<long> dayOrders;
    double dayBought = 0, daySold = 0;
    APITelegram telegram;
};
