#pragma once

#include "APIReddit.h"
#include "APIGpt.h"
#include "State.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
//#include "MyEWrapper.h"

class MyEWrapper;

class Engine {
public: 
	Engine(APIGpt& g, APIReddit& r, std::atomic<bool>& t, std::atomic<bool>& o, std::atomic<bool>& w, StateStore& s);
	void run();
	void setFlag(bool f);
	void notifyTradeCycleComplete();
	void notifyRebalanceMap();
	void setClient(MyEWrapper& c);
	void clearClient();
	bool canTradeNow();

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
};