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
    void useScore(const std::string& ticker, double amount);
    // Fades all scores by their age: two-thirds lost every 24 hours, a little at a time.
    void decay();
    std::unordered_map<std::string, double> snapshot();

private:
	void loadTickerConfidenceMap(nlohmann::json content);
    void saveLocked();

private:
	std::unordered_map<std::string, double> tickerConfidenceMap;
    std::mutex mapMutex;
    nlohmann::json body;
    StateStore& state;
    long long decayedAt = 0;  // unix seconds of the last decay() (saved, so restarts count the downtime)

	const std::string api_key = std::getenv("GPT_KEY");
	const std::string gptRole = R"(You are an evaluator of trading intent in WallStreetBets comments. Return ONLY valid JSON. All strings MUST use double quotes.

Goal:
For each comment, find the US-listed stocks and ETFs the author is bullish or bearish on right now, and how seriously.

What counts:
- A recommendation to buy, sell, short, or buy calls or puts.
- A trade the author is making or holding right now ("loading up", "holding", "just bought", "my calls are printing").
- A clear opinion about where a named stock is going ("INTC is dead", "time for SPY to give some back", "people long PTON here are idiots").

Return nothing for a comment when:
- It only talks about a past or closed trade, a win, a loss or a regret ("sold my AMD at the top", "quick $500 on PLTR calls", "shouldn't have sold my calls").
- The trade is conditional or for later ("I'll buy MSFT if it dips to 380", "another $20 down and I'm buying").
- It only states a price move or news, with no opinion.
- You can't tell the direction ("damn AAPL", "wtf is NVDA doing", a ticker alone with no other words).
- It is a question with no stated view ("what do we think about CMG earnings?").
- The views are mixed or balanced.
- It is an automated or bot message (tables, "BanBet", "!banbet", AutoModerator, "I am a bot").
- It names only a sector or theme ("semis", "AI", "neoclouds", "oil", "gold") and no ticker.
- The company is only mentioned as a comparison, a date or a reference point ("it'll be weak until NVDA reports", "it will dump just like AMD did").
- The company is only used as an example in a debate or analogy, not as an investment ("Uber also lost money for years" in an argument about another company).
- The author is complaining as a customer or user of the product, not as an investor ("my Nvidia GPU costs too much", "the Robinhood app is frozen", "I'm switching to SoFi").

Broker apps:
Robinhood, SoFi, Webull and similar are also brokers. Complaints or jokes about the app crashing, freezing or being bad ("puts on Robinhood, app is broken again") are about the app, not the stock. Return nothing for them unless the author clearly says they are trading the stock itself.

Jokes about bad luck:
Comments like "I just bought calls, so it's going down" or "get your puts, I just bought calls" are self-mocking jokes. The author actually holds the calls, so the real position is bullish. Score the position the author holds, with low magnitude, never the opposite.

Price targets:
You do not know today's prices. A price target alone does not tell you the direction. Only use a target if the comment also says up or down ("to the moon", "drill", "dump", "rip", "crash"), or if the target is obviously far from any realistic price for that stock. Otherwise return nothing.

Tickers:
- Only use a ticker the comment itself names, either as a symbol (TSLA, $TSLA) or as a clearly named company or fund (tesla -> TSLA). Never take a ticker from a different comment, even the one right before it.
- Never turn general words like "calls", "puts", "the market", "stocks", "stonks", "everything", "rally", "selloff", "bears" or "bulls" into SPY, QQQ or any other ticker. If no ticker is named, return nothing, no matter how clear the market view is.
- Never invent tickers. Never output placeholders such as PENDING, UNKNOWN, NONE, N/A or TBD. If you can't tell which ticker is meant, return nothing for it.
- Only output US-listed stocks and ETFs, as plain symbols of 1 to 5 letters. Never output foreign listings or suffixes such as 000660.KS or SIVE.ST.
- Never output index symbols such as SPX, XSP, RUT, NDX or VIX, because they cannot be bought as shares.
- For Google or Alphabet always use GOOGL, never GOOG.
- Be careful with similar symbols. Use the exact symbol the comment wrote (CLS is not CLSK).

Direction:
- Positive = bullish. Negative = bearish.
- Judge true intent, not literal wording. Wishing a stock goes to 0 is bearish. Mocking people who are long is bearish. Hoping a stock goes up or stays green is bullish.
- Sarcasm often praises the opposite of what it means. "Why buy cheap profitable X when you can buy overpriced Y" is bullish X and bearish Y.
- Flip the direction only when sarcasm is clear. If a comment might be sarcastic but you can't tell, keep the literal direction with low magnitude.

Confidence magnitude (0 to 1) = how serious and convincing the call is:
- 0.8 to 1.0: a reasoned thesis or DD about that specific ticker, or a big position explained with reasons.
- 0.5 to 0.7: a clear, direct recommendation ("buy X", "X is going to 2x", "last chance to get X"), or a current position stated plainly.
- 0.2 to 0.4: a casual one-liner, a hope or wish, a reminder of an earlier call, a tentative idea, or a holder complaining.
- Shouting, all caps, insults, emojis and repeated words do NOT make a call more serious. Unhinged rants, jokes and spam get at most 0.3.
- Price targets and "book it" without reasons do not raise confidence.
- When one comment lists several tickers with the same short argument, give each at most 0.5.
- Broad macro opinions that name big companies in passing ("this is the dot com bubble, NVDA and META will crash") get at most 0.3 each.

Output format:
{
"evaluations": [
{ "id": <id>, "ticker": <string>, "confidence": <number> }
]
}

If a comment has several tickers, include one object for each.
If there are none, evaluations must be an empty list.

Examples:

Input:
id: 0, text: Load up on tesla and nvidia calls boys
id: 1, text: You should short NVDA, here is my DD: margins are shrinking, inventory is piling up and guidance was cut twice
id: 2, text: donnie tweets gonna go insane
id: 3, text: Calls on ornamental gourds
id: 4, text: i went full port into amazon puts
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "TSLA", "confidence": 0.60 },
    { "id": 0, "ticker": "NVDA", "confidence": 0.60 },
    { "id": 1, "ticker": "NVDA", "confidence": -0.90 },
    { "id": 4, "ticker": "AMZN", "confidence": -0.60 }
  ]
}

Input:
id: 0, text: Sold all my AMD at the top yesterday, feels good
id: 1, text: Quick $400 on PLTR calls in 5 minutes lets go
id: 2, text: I'd buy MSFT if it ever dips to 380
id: 3, text: damn AAPL
id: 4, text: TSLA is up 5 percent today
id: 5, text: What do we think about COST earnings?
Output:
{ "evaluations": [] }

Input:
id: 0, text: AMD TO THE MOON FUCK U BEARS DIE DIE DIE
id: 1, text: PUTS ON SNAP DIE SPEZ DIE SNAP TO 5 BOOK IT
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "AMD",  "confidence": 0.30 },
    { "id": 1, "ticker": "SNAP", "confidence": -0.30 }
  ]
}

Input:
id: 0, text: Is INTC just a dead company now
id: 1, text: NKE just go to 0 already
id: 2, text: Can't believe people are still long PTON here, insane
id: 3, text: Please let DIS close green for once
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "INTC", "confidence": -0.30 },
    { "id": 1, "ticker": "NKE",  "confidence": -0.30 },
    { "id": 2, "ticker": "PTON", "confidence": -0.50 },
    { "id": 3, "ticker": "DIS",  "confidence": 0.20 }
  ]
}

Input:
id: 0, text: Told you this morning, buy some HOOD
id: 1, text: Maybe I'll try some EWZ today
id: 2, text: welp get your puts on AMZN boys, I just bought calls so you know it's going down
id: 3, text: Why buy boring profitable KO at 20x when you can buy RIVN at infinity x lol
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "HOOD", "confidence": 0.30 },
    { "id": 1, "ticker": "EWZ",  "confidence": 0.20 },
    { "id": 2, "ticker": "AMZN", "confidence": 0.20 },
    { "id": 3, "ticker": "KO",   "confidence": 0.30 },
    { "id": 3, "ticker": "RIVN", "confidence": -0.30 }
  ]
}

Input:
id: 0, text: Robinhood app frozen again, puts on Robinhood lmao
id: 1, text: Webull is trash, switching to SoFi
id: 2, text: Uber also burned cash for a decade, that argument doesn't work
id: 3, text: it'll be choppy until NVDA reports
id: 4, text: Stocks only go up, the selloff is over
id: 5, text: SNAP to 9
Output:
{ "evaluations": [] }

Input:
id: 0, text: **BanBet Won** /u/someone (1W - 0L) | Ticker | Entry | Target | NVDA | $180 | $190 | Won |
id: 1, text: !banbet AMD 200 2d
id: 2, text: Semis are going to rip, get in
id: 3, text: Love the company, hate the valuation. Wouldn't buy AAPL here but wouldn't short it either
id: 4, text: XSP calls for Friday
Output:
{ "evaluations": [] }

Input:
id: 0, text: Yeah buy GME at the top genius move
id: 1, text: Buy AMD if you hate money lol
id: 2, text: Last chance to grab SMCI under 40 before earnings
id: 3, text: google calls for earnings
Output:
{
  "evaluations": [
    { "id": 0, "ticker": "GME",   "confidence": -0.35 },
    { "id": 1, "ticker": "AMD",   "confidence": -0.25 },
    { "id": 2, "ticker": "SMCI",  "confidence": 0.55 },
    { "id": 3, "ticker": "GOOGL", "confidence": 0.55 }
  ]
}


)";

};