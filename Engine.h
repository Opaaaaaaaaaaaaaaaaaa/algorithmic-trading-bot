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

class MyEWrapper;

// Counts GPT's bullish and bearish calls.
struct Sentiment {
	int bullish = 0, bearish = 0;
	double bullishWeight = 0, bearishWeight = 0;  // confidence squared, like the bot's scoring
	void add(double confidence);
	std::string describe() const;
};

class Engine {
public: 
	Engine(APIGpt& g, APIReddit& r, std::atomic<bool>& t, std::atomic<bool>& o, std::atomic<bool>& w, StateStore& s, Alerts& a);
	void run();
	void notifyRebalanceMap();
	void setClient(MyEWrapper& c);
	void clearClient();
	bool canTradeNow();
	// Minutes since the engine loop last started a round (to spot it getting stuck).
	long long minutesSinceLastLoop() const;
	// GPT's calls since the last time this was called (for the daily summary).
	Sentiment takeDayStats();
	// How much bigger buy orders are made: WSB's bullish weight divided by its bearish
	// weight over the last kSentimentDays days, limited to 0.5 - 3.
	double buyMultiplier();

private:
	std::mutex clientMutex;
	std::string todayInNewYork();

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
	std::atomic<long long> lastLoop{ 0 };  // steady_clock seconds
	std::mutex statsMutex;
	Sentiment hourStats, dayStats;
	std::map<std::string, std::pair<double, double>> sentimentDays;  // NY date -> bullish, bearish weight (saved)
	void addToSentimentDays(double confidence);
	std::chrono::steady_clock::time_point lastHourLog = std::chrono::steady_clock::now();
};