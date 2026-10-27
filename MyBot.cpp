
#include "StdAfx.h"

#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <thread>

#include "Env.h"
#include "MyEWrapper.h"
#include "APIReddit.h"
#include "APIGpt.h"
#include "APITelegram.h"
#include "Engine.h"
#include "State.h"
#include "APIPrice.h"
#include "Alerts.h"
#include "Log.h"

#include <iostream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

const unsigned MAX_ATTEMPTS = 5;
const unsigned SLEEP_TIME = 10;

int main(int argc, char** argv)
{
	loadEnvFile(".env");

	const char* host = argc > 1 ? argv[1] : "";
	int port = argc > 2 ? atoi(argv[2]) : 0;
	if (port <= 0)
		port = 7497;
	const char* connectOptions = argc > 3 ? argv[3] : "+PACEAPI +DELAYED";
	int clientId = 0;

	unsigned attempt = 0;
	startLogging("logs");
	logPrintf("Start\n");

	std::atomic<bool> trigger = false;
	std::atomic<bool> open = false;
	std::atomic<bool> wait = false;
	Alerts alerts;
	alerts.now("MyBot started");

	StateStore state("bot_state.json");
	APIGpt chatGpt(state);
	APIReddit reddit(state);
	APIPrice prices;
	prices.setAlerts(alerts);
	if (!prices.hasKey())
		alerts.now("[ALERT] FINNHUB_KEY is missing from .env, so the bot can't place any orders");
	Engine engine(chatGpt, reddit, trigger, open, wait, state, alerts);
	OrderBook orderBook;
	std::thread engineThread(&Engine::run, &engine);

	for (;;) {
		++attempt;
		logPrintf("Attempt %u of %u\n", attempt, MAX_ATTEMPTS);

		MyEWrapper client;
		client.setEngine(engine);
		client.setGpt(chatGpt);
		client.setPrices(prices);
		client.setAlerts(alerts);
		client.setOrderBook(orderBook);
		if (connectOptions) {
			client.setConnectOptions(connectOptions);
		}

		engine.setClient(client);

		client.connect(host, port, clientId);
		if (client.isConnected()) alerts.ok("IBKR connection");

		while (client.isConnected()) {

			client.processMessages();
			if (open)
			{
				client.mktOpen();
				open.store(false);
			}

			if (wait)
			{
				client.waitMktOpen();
				wait.store(false);
				trigger.store(false);  // market closed: drop any pending trade round
				alerts.ok("Trading");
			}

			if (trigger)
			{
				if (client.triggerTrades())
				{
					trigger.store(false);
					alerts.ok("Trading");
				}
				else
				{
					alerts.fail("Trading", "the market is open but orders can't be sent (stuck waiting on IBKR)",
						std::chrono::minutes(5));
				}
			}

			long long stuck = engine.minutesSinceLastLoop();
			if (stuck >= 10)
				alerts.fail("Engine", "the main loop hasn't run for " + std::to_string(stuck) +
					" min (stuck on Reddit or GPT?)", std::chrono::seconds(0));
			else
				alerts.ok("Engine");

			client.checkOrders();
			alerts.tick();
		}

		engine.clearClient();
		alerts.fail("IBKR connection", "can't connect to TWS / IB Gateway (is it running and logged in?)",
			std::chrono::minutes(2));

		if (attempt >= MAX_ATTEMPTS) {
			std::this_thread::sleep_for(std::chrono::minutes(SLEEP_TIME));
			attempt = 0;
		}

		logPrintf("Sleeping %u seconds before next attempt\n", SLEEP_TIME);
		std::this_thread::sleep_for(std::chrono::seconds(SLEEP_TIME));
	}

	logPrintf("End\n");
}