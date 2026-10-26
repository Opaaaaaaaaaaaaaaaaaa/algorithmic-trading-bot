
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
	printf("Start of C++ Socket Client Test %u\n", attempt);

	std::atomic<bool> trigger = false;
	std::atomic<bool> open = false;
	std::atomic<bool> wait = false;
	StateStore state("bot_state.json");
	APIGpt chatGpt(state);
	APIReddit reddit(state);
	Engine engine(chatGpt, reddit, trigger, open, wait, state);
	std::thread engineThread(&Engine::run, &engine);

	for (;;) {
		++attempt;
		printf("Attempt %u of %u\n", attempt, MAX_ATTEMPTS);

		MyEWrapper client;
		client.setEngine(engine);
		client.setGpt(chatGpt);
		if (connectOptions) {
			client.setConnectOptions(connectOptions);
		}

		engine.setClient(client);

		client.connect(host, port, clientId);

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
			}

			if (trigger)
			{
				if (client.triggerTrades())
				{
					trigger.store(false);
				}
			}
		}

		engine.clearClient();

		if (attempt >= MAX_ATTEMPTS) {
			std::this_thread::sleep_for(std::chrono::minutes(SLEEP_TIME));
			attempt = 0;
		}

		printf("Sleeping %u seconds before next attempt\n", SLEEP_TIME);
		std::this_thread::sleep_for(std::chrono::seconds(SLEEP_TIME));
	}

	printf("End of C++ Socket Client Test\n");
}