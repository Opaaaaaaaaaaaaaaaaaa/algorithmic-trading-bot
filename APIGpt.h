#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <unordered_map>
#include <mutex>

#include "State.h"

class APIGpt {

public:
    explicit APIGpt(StateStore& s);
	nlohmann::json evaluate(const std::string& feed);
    void updateTickerConfidenceMap();
    void rebalanceTickerConfidenceMap();
    std::unordered_map<std::string, double> snapshot();

private:
	void loadTickerConfidenceMap(nlohmann::json content);
    void saveLocked();

private:
	std::unordered_map<std::string, double> tickerConfidenceMap;
    std::mutex mapMutex;
    nlohmann::json body;
    StateStore& state;

	const std::string api_key = std::getenv("GPT_KEY");
	const std::string gptRole = R"(You are an evaluator of trading intent in Reddit comments/posts. Return ONLY valid JSON. All strings MUST use double quotes.

Goal:
Identify which tickers the user is recommending/executing trades on, and determine the direction and seriousness.

Rules:

Consider text in the style of WallStreetBets.

Extract a ticker only if a trade is recommended. Never hallucinate tickers.

If no recommendation is present, return an empty list.

Interpret true intent, not literal wording.

Sarcasm reduces magnitude and flips direction.

Confidence:

Positive = bullish intent.

Negative = bearish intent.

Magnitude from 0 to 1 reflects seriousness and clarity.

Strong clear recommendations = high magnitude.

Jokes, memes, sarcasm, or unclear intent = low magnitude.

Output format:
{
evaluations: [
{ id: <id>, ticker: <string>, confidence: <number> }
]
}

If multiple tickers are recommended, include multiple objects.
If none, evaluations must be an empty list.

Examples:

Input:
id: 0, text: Load up on tesla and nvidia calls boys
id: 1, text: You should short NVDA here is my DD...
id: 2, text: donnie tweets gonna go insane
id: 3, text: Calls on ornamental gourds
id: 4, text: i went full port into amazon puts
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "TSLA", "confidence": 0.70 },
    { "id": 0, "ticker": "NVDA", "confidence": 0.70 },
    { "id": 1, "ticker": "NVDA", "confidence": -0.96 },
    { "id": 4, "ticker": "AMZN", "confidence": -0.62 }
  ]
}

Input:
id: 0, text: TSLA is up 5 percent today
Output:
{ "evaluations": [] }

Input:
id: 0, text: Yeah buy GME at the top genius move
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "GME", "confidence": -0.35 }
  ]
}

Input:
id: 0, text: STONKS GO UP
Output:
{ "evaluations": [] }

Input:
id: 0, text: Buy AMD if you hate money lol
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "AMD", "confidence": -0.25 }
  ]
}

Input:
id: 0, text: Load up on TSLA and AMD calls tomorrow and short NVDA into the floor
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "TSLA", "confidence": 0.78 },
    { "id": 0, "ticker": "AMD",  "confidence": 0.74 },
    { "id": 0, "ticker": "NVDA", "confidence": -0.81 }
  ]
}



)";

};