#pragma once

#include "APIReddit.h"
#include "APIGpt.h"
#include "State.h"
#include "Alerts.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class MyEWrapper;

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
};