#pragma once

#include "APIReddit.h"
#include "APIGpt.h"

class MyEWrapper;

class Engine {
public: 
	Engine(APIGpt& g, APIReddit& r, std::atomic<bool>& t, std::atomic<bool>& o, std::atomic<bool>& w);
	void run();
	void notifyTradeCycleComplete();
	void notifyRebalanceMap();
	void setClient(MyEWrapper& c);
	void clearClient();
	bool canTradeNow();

private:
	std::mutex clientMutex;

private:
	APIGpt& gpt;
	APIReddit& reddit;
	MyEWrapper* client;
	bool mktOpen;
	std::atomic<bool> rebalance{ false };
	std::atomic<bool> tradeCycleComplete{ false };
	std::atomic<bool> flag;
	std::atomic<bool>& trigger;
	std::atomic<bool>& open;
	std::atomic<bool>& wait;
};