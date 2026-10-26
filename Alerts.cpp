#define _CRT_SECURE_NO_WARNINGS

#include "Alerts.h"

#include <cstdio>
#include <iostream>

using namespace std::chrono;

// How often the filled-orders summary is sent (only if something filled).
const auto kSummaryEvery = hours(1);
// Keep each summary well under Telegram's 4096-character limit.
const size_t kMaxSummaryLines = 40;

void Alerts::ok(const std::string& what)
{
    bool wasAlerted = false;
    {
        std::lock_guard<std::mutex> lock(m);
        auto it = problems.find(what);
        if (it == problems.end()) return;
        wasAlerted = it->second.alerted;
        problems.erase(it);
    }
    if (wasAlerted) send("[OK] " + what + " is working again");
}

void Alerts::fail(const std::string& what, const std::string& detail, seconds after)
{
    {
        std::lock_guard<std::mutex> lock(m);
        auto [it, isNew] = problems.try_emplace(what);
        if (isNew) {
            it->second.since = steady_clock::now();
            it->second.after = after;
        }
        it->second.detail = detail;
    }
    tick();
}

void Alerts::now(const std::string& message)
{
    send(message);
}

void Alerts::addFill(long orderId, const std::string& symbol, const std::string& side, double shares, double price)
{
    std::lock_guard<std::mutex> lock(m);
    auto [it, isNew] = fills.try_emplace(orderId);
    if (isNew) {
        fillOrder.push_back(orderId);
        it->second.symbol = symbol;
        it->second.side = side == "BOT" ? "BUY" : side == "SLD" ? "SELL" : side;
    }
    it->second.shares += shares;
    it->second.cost += shares * price;
}

void Alerts::tick()
{
    std::vector<std::string> out;
    {
        std::lock_guard<std::mutex> lock(m);
        auto t = steady_clock::now();

        for (auto& [what, p] : problems) {
            if (p.alerted || t - p.since < p.after) continue;
            p.alerted = true;
            std::string msg = "[ALERT] " + what + ": " + p.detail;
            auto mins = duration_cast<minutes>(t - p.since).count();
            if (mins >= 1) msg += " (for " + std::to_string(mins) + " min)";
            out.push_back(msg);
        }

        if (t - lastSummary >= kSummaryEvery) {
            lastSummary = t;
            if (!fills.empty()) {
                std::string msg = "Orders filled in the last hour:";
                size_t n = 0;
                for (long id : fillOrder) {
                    if (n++ == kMaxSummaryLines) {
                        msg += "\n...and " + std::to_string(fillOrder.size() - kMaxSummaryLines) + " more";
                        break;
                    }
                    const Fill& f = fills[id];
                    char line[160];
                    std::snprintf(line, sizeof(line), "\n%s %g %s @ $%.2f (~$%.0f)",
                        f.side.c_str(), f.shares, f.symbol.c_str(), f.cost / f.shares, f.cost);
                    msg += line;
                }
                out.push_back(msg);
                fills.clear();
                fillOrder.clear();
            }
        }
    }
    for (const auto& msg : out) send(msg);
}

void Alerts::send(const std::string& message)
{
    std::cout << "Telegram: " << message << "\n";
    telegram.sendMessage(message);
}
