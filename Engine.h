#pragma once

#include "APIReddit.h"
#include "APIGpt.h"
#include "State.h"
#include "Alerts.h"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MyEWrapper;

struct Sentiment {
	int bullish = 0, bearish = 0;
	double bullishWeight = 0, bearishWeight = 0;
	void add(double confidence);
	std::string describe() const;
};

class Engine {
public: 
	Engine(APIGpt& g, APIReddit& r, std::atomic<bool>& t, std::atomic<bool>& o, std::atomic<bool>& w, StateStore& s, Alerts& a);
	void run();
	void markRebalanced();
	bool rebalancedToday();
	bool rebalanceDue();
	std::vector<std::string> takeTopHype(size_t n);
	std::string todayInNewYork();
	void setClient(MyEWrapper& c);
	void clearClient();
	bool canTradeNow();
	long long minutesSinceLastLoop() const;
	Sentiment takeDayStats();
	double wsbRatio();

private:
	std::mutex clientMutex;

private:
	APIGpt& gpt;
	APIReddit& reddit;
	MyEWrapper* client;
	bool mktOpen;
	std::atomic<bool> flag;
	std::atomic<bool>& trigger;
	std::atomic<bool>& open;
	std::atomic<bool>& wait;
	StateStore& state;
	Alerts& alerts;
	std::atomic<long long> lastLoop{ 0 };
	std::mutex statsMutex;
	Sentiment hourStats, dayStats;
	std::map<std::string, std::pair<double, double>> sentimentDays;
	void addToSentimentDays(double confidence);
	std::map<std::string, double> hype;
	std::chrono::steady_clock::time_point lastHourLog = std::chrono::steady_clock::now();
};