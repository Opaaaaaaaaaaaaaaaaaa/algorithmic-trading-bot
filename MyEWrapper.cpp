#include "StdAfx.h"

#include "MyEWrapper.h"
#include "Engine.h"
#include "Log.h"

#include "EClientSocket.h"
#include "EPosixClientSocketPlatform.h"

#include "Contract.h"
#include "Order.h"
#include "Execution.h"
#include "Decimal.h"
#include "TagValue.h"

#include <stdio.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <ctime>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <optional>
#include <algorithm>
#include <cstdlib>

const double kDollarsPerPointSell = 5000.0;
const double kDollarsPerPointBuy  = 8500.0;
const auto kSlowFill = std::chrono::minutes(5);
const auto kRetryAfterReject = std::chrono::minutes(15);

const int kAccountSummaryReqId = 2000000001;
const auto kPositionsLogEvery = std::chrono::hours(1);
const auto kSummaryTimeout = std::chrono::minutes(2);

static Order adaptiveMarketOrder(const std::string& action, int shares)
{
	Order o;
	o.action = action;
	o.orderType = "MKT";
	o.tif = "DAY";
	o.totalQuantity = DecimalFunctions::stringToDecimal(std::to_string(shares));
	o.algoStrategy = "Adaptive";
	o.algoParams.reset(new TagValueList());
	o.algoParams->push_back(TagValueSPtr(new TagValue("adaptivePriority", "Normal")));
	return o;
}

const size_t kOvernightPicks = 5;
const double kOvernightDollars = 10000.0;
const char* const kOvernightCsv = "logs/overnight_test.csv";
const auto kOvernightRetry = std::chrono::minutes(5);

const double kStartValue = 1000000.0;
const size_t kMaxPublicTrades = 60;

const double kCrumbValue = 100.0;
const double kMinPrice = 5.0;

const int PING_DEADLINE = 2;
const int SLEEP_BETWEEN_PINGS = 30; 

MyEWrapper::MyEWrapper()
	: m_osSignal(2000)
	, m_pClient(new EClientSocket( this, &m_osSignal) )
	, m_state(ST_CONNECT)
	, m_sleepDeadline(0)
	, m_orderId(0)
	, m_extraAuth(false) 
{}

MyEWrapper::~MyEWrapper()
{
	if (m_pReader)
		m_pReader.reset();

	delete m_pClient;
}

bool MyEWrapper::connect( const char* host, int port, int clientId)
{
	logPrintf("Connecting to %s:%d clientId:%d\n", !(host && *host) ? "127.0.0.1" : host, port, clientId);

	bool bRes = m_pClient->eConnect(host, port, clientId, m_extraAuth);

	if (bRes) {
		logPrintf("Connected to %s:%d clientId:%d serverVersion: %d\n", m_pClient->host().c_str(), m_pClient->port(), clientId, m_pClient->EClient::serverVersion());

		m_pReader = std::unique_ptr<EReader>(new EReader(m_pClient, &m_osSignal));
		m_pReader->start();
	}
	else
		logPrintf("Cannot connect to %s:%d clientId:%d\n", m_pClient->host().c_str(), m_pClient->port(), clientId);

	return bRes;
}

void MyEWrapper::disconnect() const
{
	m_pClient->eDisconnect();

	logPrintf("Disconnected\n");
}

bool MyEWrapper::isConnected() const
{
	return m_pClient->isConnected();
}

void MyEWrapper::setConnectOptions(const std::string& connectOptions)
{
	m_pClient->setConnectOptions(connectOptions);
}

void MyEWrapper::error(int id, time_t errorTime, int errorCode, const std::string& errorString, const std::string& advancedOrderRejectJson)
{
	char errorTimeStr[80];
	if (errorTime > 0) {
#if defined(IB_WIN32)
		ctime_s(errorTimeStr, sizeof(errorTimeStr), &(errorTime /= 1000));
#else
		ctime_r(&(errorTime /= 1000), errorTimeStr);
#endif
		errorTimeStr[strlen(errorTimeStr) - 1] = '\0';
	}
	else {
		errorTimeStr[0] = '\0';
	}

	if (!advancedOrderRejectJson.empty()) {
		logPrintf("Error. Id: %d, Time: %s, Code: %d, Msg: %s, AdvancedOrderRejectJson: %s\n", id, errorTimeStr, errorCode, errorString.c_str(), advancedOrderRejectJson.c_str());
	}
	else {
		logPrintf("Error. Id: %d, Time: %s, Code: %d, Msg: %s\n", id, errorTimeStr, errorCode, errorString.c_str());
	}

	if (!alerts) return;

	if (book && book->orders.count(id)) {
		std::string what = errorString + " (code " + std::to_string(errorCode) + ")";
		bool warningOnly = errorCode == 399 || (errorCode >= 2100 && errorCode < 3000);
		bool orderIsDead = errorCode >= 200 && errorCode <= 203;

		if (orderIsDead)
			endOrder(id, "IBKR rejected it: " + what, true);
		else if (!warningOnly)
			alerts->now("[ALERT] IBKR message about order " + book->orders[id].label() + ": " + what);
	}

	if (errorCode == 1100)
		alerts->fail("IBKR server connection", "TWS lost its connection to IBKR's servers", std::chrono::minutes(3));
	if (errorCode == 1101 || errorCode == 1102)
		alerts->ok("IBKR server connection");
}

void MyEWrapper::nextValidId(OrderId orderId)
{
	logPrintf("MyEWrapper::nextValidId. Next Valid Id: %ld\n", orderId);
	m_orderId = orderId;
	m_state = ST_CONNECT_ACK;
}

void MyEWrapper::processMessages()
{
	switch (m_state)
	{
	case ST_CONNECT:
		break;
	case ST_CONNECT_ACK:
		break;
	case ST_WAIT_MARKET_OPEN:
		break;
	case ST_LOAD_REBALANCE_MAP:
		break;
	case ST_REBALANCE:
		rebalance();
		break;
	case ST_MARKET_OPEN_ACK:
		break;
	case ST_TRADE:
		executeTrades();
		break;
	case ST_TRADE_ACK:
		break;
	default:
		break;
	}

	m_osSignal.waitForSignal();
	errno = 0;
	m_pReader->processMsgs();
}

void MyEWrapper::setEngine(Engine& e)
{
	engine = &e;
}

void MyEWrapper::setGpt(APIGpt& g)
{
	gpt = &g;
}

void MyEWrapper::setPrices(APIPrice& p)
{
	prices = &p;
}

void MyEWrapper::setAlerts(Alerts& a)
{
	alerts = &a;
}

void MyEWrapper::setOrderBook(OrderBook& b)
{
	book = &b;
}

void MyEWrapper::setState(StateStore& s)
{
	state = &s;
}

void MyEWrapper::placeAndRemember(const Contract& contr, const Order& o, int shares, double pointsPerShare)
{
	if (book) {
		TrackedOrder t;
		t.symbol = contr.symbol;
		t.action = o.action;
		t.shares = shares;
		t.pointsPerShare = pointsPerShare;
		t.placed = std::chrono::steady_clock::now();
		book->orders[m_orderId] = t;
	}
	m_pClient->placeOrder(m_orderId++, contr, o);
}

void MyEWrapper::orderStatus(OrderId orderId, const std::string& status, Decimal filled, Decimal remaining,
	double, int, int, double, int, const std::string&, double)
{
	onOrderStatus(orderId, status, std::stod(DecimalFunctions::decimalToString(filled)),
		std::stod(DecimalFunctions::decimalToString(remaining)));
}

void MyEWrapper::orderStatus(OrderId orderId, const std::string& status, Decimal filled, Decimal remaining,
	double, long long, int, double, int, const std::string&, double)
{
	onOrderStatus(orderId, status, std::stod(DecimalFunctions::decimalToString(filled)),
		std::stod(DecimalFunctions::decimalToString(remaining)));
}

void MyEWrapper::onOrderStatus(long orderId, const std::string& status, double filled, double remaining)
{
	if (!book) return;
	auto it = book->orders.find(orderId);
	if (it == book->orders.end()) return;

	it->second.filled = filled;

	if (status == "Filled" || remaining <= 0) {
		if (alerts) alerts->ok("Orders for " + it->second.symbol);
		book->orders.erase(it);
	}
	else if (status == "Cancelled" || status == "ApiCancelled" || status == "Inactive") {
		endOrder(orderId, "IBKR ended it (" + status + ")", false);
	}
}

void MyEWrapper::endOrder(long orderId, const std::string& why, bool rejected)
{
	auto it = book->orders.find(orderId);
	if (it == book->orders.end()) return;
	TrackedOrder t = it->second;
	book->orders.erase(it);

	double unfilled = t.shares - t.filled;
	std::string msg = t.label() + ": " + why + ". ";
	if (t.filled > 0) {
		char part[80];
		std::snprintf(part, sizeof(part), "%g of %d filled. ", t.filled, t.shares);
		msg += part;
	}

	if (unfilled > 0 && t.pointsPerShare > 0) {
		double points = unfilled * t.pointsPerShare;
		gpt->useScore(t.symbol, t.action == "BUY" ? -points : points);
		msg += "Score for the unfilled shares was given back";
		msg += rejected ? ", trying again in 15 min." : ".";
	}

	if (rejected) book->retryAfter[t.symbol] = std::chrono::steady_clock::now() + kRetryAfterReject;

	logPrintf("Order ended: %s\n", msg.c_str());
	if (alerts) alerts->fail("Orders for " + t.symbol, msg, std::chrono::seconds(0));
}

void MyEWrapper::checkOrders()
{
	if (!book || !alerts) return;
	auto now = std::chrono::steady_clock::now();
	for (auto& [id, t] : book->orders) {
		if (t.slowAlerted || now - t.placed < kSlowFill) continue;
		t.slowAlerted = true;
		char msg[200];
		std::snprintf(msg, sizeof(msg), "[ALERT] %s: only %g of %d filled after 5 min, the rest is still open",
			t.label().c_str(), t.filled, t.shares);
		alerts->now(msg);
	}
}

void MyEWrapper::execDetails(int reqId, const Contract& contract, const Execution& execution)
{
	double shares = std::stod(DecimalFunctions::decimalToString(execution.shares));
	logPrintf("Filled: %s %g %s @ $%.2f\n", execution.side.c_str(), shares, contract.symbol.c_str(), execution.price);
	if (alerts) alerts->addFill(execution.orderId, contract.symbol, execution.side, shares, execution.price);
}

void MyEWrapper::executeTrades()
{
	std::unordered_map<std::string, double> localCopy = gpt->snapshot();
	for (auto& [ticker, score] : localCopy)
	{
		if (std::abs(score) < 1.0) continue;

		if (book) {
			auto wait = book->retryAfter.find(ticker);
			if (wait != book->retryAfter.end()) {
				if (std::chrono::steady_clock::now() < wait->second) continue;
				book->retryAfter.erase(wait);
			}
		}

		std::optional<double> price = prices ? prices->get(ticker) : std::nullopt;
		if (!price) continue;
		if (*price < kMinPrice) continue;

		double dollarsPerPoint = score > 0 ? kDollarsPerPointBuy : kDollarsPerPointSell;
		double dollars = std::abs(score) * dollarsPerPoint;

		int shares = static_cast<int>(dollars / *price);
		if (shares == 0) continue; 

		Contract contr;
		contr.symbol = ticker;
		contr.secType = "STK";
		contr.exchange = "SMART";
		contr.currency = "USD";

		Order o = adaptiveMarketOrder(score > 0 ? "BUY" : "SELL", shares);

		placeAndRemember(contr, o, shares, *price / dollarsPerPoint);

		double used = shares * *price / dollarsPerPoint;
		gpt->useScore(ticker, score > 0 ? used : -used);

		char msg[200];
		std::snprintf(msg, sizeof(msg), "Order placed: %s %d %s @ ~$%.2f (~$%.0f)",
			o.action.c_str(), shares, ticker.c_str(), *price, shares * *price);
		logPrintf("%s\n", msg);
	}

	m_state = ST_TRADE_ACK;
}

bool MyEWrapper::triggerTrades()
{
	if (m_state != ST_CONNECT_ACK && m_state != ST_MARKET_OPEN_ACK && m_state != ST_TRADE_ACK) return false;

	m_state = ST_TRADE;

	logPrintf("In trigger trades\n");
	return true;
}

void MyEWrapper::waitMktOpen()
{
	m_state = ST_WAIT_MARKET_OPEN;

	if (book) {
		std::vector<long> open;
		for (auto& [id, t] : book->orders) open.push_back(id);
		for (long id : open) endOrder(id, "not filled by the market close", false);
	}

	requestAccountSummary(true);
}

void MyEWrapper::requestAccountSummary(bool forDailySummary)
{
	if (forDailySummary) m_dailyPending = true;
	if (m_summaryPending) return;
	m_summaryPending = true;
	m_summaryAsked = std::chrono::steady_clock::now();
	m_summaryValues.clear();
	m_summaryCurrency.clear();
	m_pClient->reqAccountSummary(kAccountSummaryReqId, "All", "NetLiquidation,TotalCashValue,GrossPositionValue");
}

void MyEWrapper::accountSummary(int reqId, const std::string& account, const std::string& tag,
	const std::string& value, const std::string& currency)
{
	if (reqId != kAccountSummaryReqId || !m_summaryPending) return;
	if (m_account.empty() && !account.empty() && account != "All") m_account = account;
	if (account != m_account) return;
	try {
		m_summaryValues[tag] = std::stod(value);
		if (!currency.empty()) m_summaryCurrency = currency;
	}
	catch (const std::exception&) {}
}

void MyEWrapper::accountSummaryEnd(int reqId)
{
	if (reqId != kAccountSummaryReqId || !m_summaryPending) return;
	m_pClient->cancelAccountSummary(kAccountSummaryReqId);
	m_summaryPending = false;

	double net = m_summaryValues["NetLiquidation"] - m_summaryValues["TotalCashValue"];
	double gross = m_summaryValues["GrossPositionValue"];
	std::string sign = m_summaryCurrency == "EUR" ? "\xE2\x82\xAC"
		: m_summaryCurrency == "GBP" ? "\xC2\xA3"
		: m_summaryCurrency == "USD" || m_summaryCurrency.empty() ? "$"
		: m_summaryCurrency + " ";
	std::string line = "Positions: long " + dollars((gross + net) / 2, sign) + ", short " +
		dollars((gross - net) / 2, sign) + ", net " + dollars(net, sign);
	logPrintf("%s\n", line.c_str());

	double account = m_summaryValues["NetLiquidation"];
	if (m_dailyPending) {
		sendDailySummary(line);
		publishPerformance(true, account, (gross + net) / 2, (gross - net) / 2, sign);
	}
	else if (engine && engine->canTradeNow()) {
		publishPerformance(false, account, (gross + net) / 2, (gross - net) / 2, sign);
	}
}

namespace {

std::string compact(double amount)
{
	double a = std::abs(amount);
	char buf[32];
	if (a >= 1e6) std::snprintf(buf, sizeof(buf), "%.2fM", a / 1e6);
	else if (a >= 1e3) std::snprintf(buf, sizeof(buf), "%.0fk", a / 1e3);
	else std::snprintf(buf, sizeof(buf), "%.0f", a);
	return buf;
}

std::string withSign(double amount, const std::string& sign, bool compactForm)
{
	std::string body = compactForm ? sign + compact(amount) : dollars(std::abs(amount), sign);
	if (amount > 0.5) return "+" + body;
	if (amount < -0.5) return "-" + body;
	return body;
}

}

void MyEWrapper::publishPerformance(bool closed, double account, double longValue, double shortValue,
	const std::string& sign)
{
	if (!alerts || account <= 0) return;

	std::string msg = "\xF0\x9F\x93\x8A ";
	msg += closed ? "Market closed - bot performance" : "Bot performance";
	if (m_account.rfind("DU", 0) == 0) msg += " (paper account)";

	char pct[32];
	std::snprintf(pct, sizeof(pct), "%+.2f%%", (account - kStartValue) / kStartValue * 100);
	msg += "\nAccount: " + dollars(account, sign) + " (" + pct + " since start)";

	nlohmann::json last = state ? state->get("closeValue") : nlohmann::json();
	if (last.is_number() && last.get<double>() > 0) {
		double prev = last.get<double>();
		std::snprintf(pct, sizeof(pct), "%+.2f%%", (account - prev) / prev * 100);
		msg += "\nToday: " + withSign(account - prev, sign, false) + " (" + pct + ")";
	}

	msg += "\nLong " + sign + compact(longValue) + " / Short " + sign + compact(shortValue) +
		" (net " + withSign(longValue - shortValue, sign, true) + ")";

	std::string trades = alerts->takePublicFills(kMaxPublicTrades);
	msg += trades.empty() ? "\n\nNo trades since the last update."
		: "\n\nTrades since the last update:\n" + trades;

	alerts->publish(msg);
	if (closed && state) state->set("closeValue", account);
}

void MyEWrapper::reportTick()
{
	if (m_state == ST_CONNECT) return;
	auto now = std::chrono::steady_clock::now();

	if (m_summaryPending && now - m_summaryAsked >= kSummaryTimeout) {
		m_pClient->cancelAccountSummary(kAccountSummaryReqId);
		m_summaryPending = false;
		if (m_dailyPending) sendDailySummary("Positions: TWS didn't send them");
	}

	if (m_lastPositionsLog == std::chrono::steady_clock::time_point{} || now - m_lastPositionsLog >= kPositionsLogEvery) {
		m_lastPositionsLog = now;
		requestAccountSummary(false);
	}

	if (engine && engine->rebalanceDue() &&
		(m_state == ST_CONNECT_ACK || m_state == ST_MARKET_OPEN_ACK || m_state == ST_TRADE_ACK)) {
		logPrintf("Halving positions before the close\n");
		startOvernightTest();
		loadRebalanceMap();
	}

	if (engine && engine->canTradeNow()) finishOvernightTest();
}

void MyEWrapper::sendDailySummary(const std::string& positions)
{
	m_dailyPending = false;
	if (!alerts) return;

	std::string msg = "Market closed. Today's summary:\n" + alerts->takeDayFills() + "\n" + positions;
	if (engine) {
		msg += "\nGPT: " + engine->takeDayStats().describe();
		char mult[64];
		std::snprintf(mult, sizeof(mult), "\nWSB 5-day ratio (bullish/bearish): x%.2f", engine->wsbRatio());
		msg += mult;
	}

	std::vector<std::pair<std::string, double>> scores;
	for (auto& [ticker, score] : gpt->snapshot())
		if (std::abs(score) >= 0.01) scores.push_back({ ticker, score });
	std::sort(scores.begin(), scores.end(),
		[](const auto& a, const auto& b) { return std::abs(a.second) > std::abs(b.second); });
	if (!scores.empty()) {
		msg += "\nBiggest scores (+ = bot leans buy, - = sell):";
		for (size_t i = 0; i < scores.size() && i < 5; ++i) {
			char line[80];
			std::snprintf(line, sizeof(line), "\n%s %+.2f", scores[i].first.c_str(), scores[i].second);
			msg += line;
		}
	}
	alerts->now(msg);
}

void MyEWrapper::mktOpen()
{
	if (m_state == ST_WAIT_MARKET_OPEN) m_state = ST_MARKET_OPEN_ACK;
}

void MyEWrapper::loadRebalanceMap()
{
	m_pClient->reqPositions();
	m_state = ST_LOAD_REBALANCE_MAP;
}

void MyEWrapper::rebalance()
{
	for (auto& [symbol, pos] : m_rebalanceMap)
	{
		int qty = static_cast<int>(std::abs(pos));
		int half = qty / 2;
		int left = qty - half;
		if (left <= 1 || left * m_avgCost[symbol] < kCrumbValue) half = qty;
		if (half <= 0) continue;

		Contract contr;
		contr.symbol = symbol;
		contr.secType = "STK";
		contr.exchange = "SMART";
		contr.currency = "USD";

		Order o = adaptiveMarketOrder(pos > 0 ? "SELL" : "BUY", half);

		placeAndRemember(contr, o, half, 0.0);

		logPrintf("Rebalance order placed: %s %d %s%s\n", o.action.c_str(), half, symbol.c_str(),
			half == qty ? " (closing the whole position)" : "");
	}

	engine->markRebalanced();

	m_rebalanceMap.clear();
	m_avgCost.clear();
	m_state = ST_MARKET_OPEN_ACK;
}

void MyEWrapper::position(const std::string& account, const Contract& contract, Decimal position, double avgCost)
{
	m_rebalanceMap[contract.symbol] = std::stod(DecimalFunctions::decimalToString(position));
	m_avgCost[contract.symbol] = avgCost;
}

void MyEWrapper::positionEnd()
{
	m_state = ST_REBALANCE;
}

void MyEWrapper::startOvernightTest()
{
	if (!state || !engine || !prices) return;
	nlohmann::json test = state->get("overnightTest");
	if (!test.is_object()) test = nlohmann::json::object();
	if (test.contains("picks") && !test["picks"].empty())
		logPrintf("Overnight test: dropping the picks from %s, they were never sold\n",
			test.value("date", std::string("?")).c_str());

	nlohmann::json picks = nlohmann::json::object();
	std::string list;
	for (const std::string& ticker : engine->takeTopHype(kOvernightPicks * 3)) {
		if (picks.size() >= kOvernightPicks) break;
		std::optional<double> price = prices->get(ticker);
		if (!price) continue;
		picks[ticker] = *price;
		char part[64];
		std::snprintf(part, sizeof(part), " %s @ $%.2f", ticker.c_str(), *price);
		list += part;
	}
	test["date"] = engine->todayInNewYork();
	test["picks"] = picks;
	state->set("overnightTest", test);
	logPrintf("Overnight test: bought%s\n", list.empty() ? " nothing" : list.c_str());
}

void MyEWrapper::finishOvernightTest()
{
	if (!state || !engine || !prices) return;
	nlohmann::json test = state->get("overnightTest");
	if (!test.is_object() || !test.contains("picks") || !test["picks"].is_object() || test["picks"].empty()) return;
	std::string bought = test.value("date", std::string());
	std::string today = engine->todayInNewYork();
	if (bought == today) return;

	auto now = std::chrono::steady_clock::now();
	if (m_overnightTried != std::chrono::steady_clock::time_point{} && now - m_overnightTried < kOvernightRetry) return;
	m_overnightTried = now;

	struct Result { std::string ticker; double buy, open, later; };
	std::vector<Result> results;
	for (auto& [ticker, buy] : test["picks"].items()) {
		if (!buy.is_number() || buy.get<double>() <= 0) continue;
		std::optional<APIPrice::Quote> q = prices->quote(ticker);
		if (!q) continue;
		results.push_back({ ticker, buy.get<double>(), q->open, q->now });
	}
	if (results.empty()) {
		logPrintf("Overnight test: no opening prices yet, trying again in 5 min\n");
		return;
	}

	bool newFile = !std::ifstream(kOvernightCsv).good();
	std::ofstream csv(kOvernightCsv, std::ios::app);
	if (newFile) csv << "bought,sold,ticker,price_1550,open,price_940,return_open_pct,return_940_pct\n";

	double pnlOpen = 0, pnlLater = 0, invested = 0;
	std::string lines;
	for (const Result& r : results) {
		double rOpen = r.open / r.buy - 1, rLater = r.later / r.buy - 1;
		pnlOpen += kOvernightDollars * rOpen;
		pnlLater += kOvernightDollars * rLater;
		invested += kOvernightDollars;
		char line[200];
		std::snprintf(line, sizeof(line), "\n%s $%.2f -> open $%.2f (%+.2f%%), 9:40 $%.2f (%+.2f%%)",
			r.ticker.c_str(), r.buy, r.open, rOpen * 100, r.later, rLater * 100);
		lines += line;
		csv << bought << ',' << today << ',' << r.ticker << ',' << r.buy << ',' << r.open << ',' << r.later << ','
			<< rOpen * 100 << ',' << rLater * 100 << '\n';
	}

	int nights = test.value("nights", 0) + 1;
	double totalInvested = test.value("invested", 0.0) + invested;
	double totalOpen = test.value("pnlOpen", 0.0) + pnlOpen;
	double totalLater = test.value("pnl940", 0.0) + pnlLater;
	test["nights"] = nights;
	test["invested"] = totalInvested;
	test["pnlOpen"] = totalOpen;
	test["pnl940"] = totalLater;
	test["picks"] = nlohmann::json::object();
	state->set("overnightTest", test);

	char head[400];
	std::snprintf(head, sizeof(head),
		"Overnight test: top %zu WSB-hyped stocks, $%.0f each, bought %s at 15:50\n"
		"Sold at the open: %s (%+.2f%%)\nSold at 9:40: %s (%+.2f%%)",
		results.size(), kOvernightDollars, bought.c_str(),
		withSign(pnlOpen, "$", false).c_str(), pnlOpen / invested * 100,
		withSign(pnlLater, "$", false).c_str(), pnlLater / invested * 100);
	char total[300];
	std::snprintf(total, sizeof(total),
		"\nAll %d nights: open %s (%+.2f%% per night), 9:40 %s (%+.2f%% per night)\nSpreads and fees not included.",
		nights, withSign(totalOpen, "$", false).c_str(), totalOpen / totalInvested * 100,
		withSign(totalLater, "$", false).c_str(), totalLater / totalInvested * 100);

	std::string msg = std::string(head) + lines + total;
	logPrintf("%s\n", msg.c_str());
	if (alerts) alerts->now(msg);
}
