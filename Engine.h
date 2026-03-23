#pragma once

#include "APIReddit.h"
#include "APIGpt.h"
//#include "MyEWrapper.h"

class MyEWrapper;

class Engine {
public: 
	Engine(APIGpt& g, APIReddit& r, std::atomic<bool>& t, std::atomic<bool>& o, std::atomic<bool>& w);
	void run();
	void setFlag(bool f);
	void notifyTradeCycleComplete();
	void notifyRebalanceMap();
	void setClient(MyEWrapper& c);
	void clearClient();
	bool canTradeNowLocal();

private:
	std::mutex clientMutex;
	bool isWeekday(const std::tm& tm);
	bool isUsMarketHours(const std::tm& tm);

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