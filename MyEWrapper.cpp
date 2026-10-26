#include "StdAfx.h"

#include "MyEWrapper.h"
#include "Engine.h"

#include "EClientSocket.h"
#include "EPosixClientSocketPlatform.h"

#include "Contract.h"
#include "Order.h"
#include "Execution.h"
#include "Decimal.h"

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


const double kDollarsPerPoint = 500.0;
// Alert if an order still isn't fully filled after this long.
const auto kSlowFill = std::chrono::minutes(5);
// After IBKR rejects an order for a ticker, wait this long before trying that ticker again.
const auto kRetryAfterReject = std::chrono::minutes(15);


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
	printf("Connecting to %s:%d clientId:%d\n", !(host && *host) ? "127.0.0.1" : host, port, clientId);

	bool bRes = m_pClient->eConnect(host, port, clientId, m_extraAuth);

	if (bRes) {
		printf("Connected to %s:%d clientId:%d serverVersion: %d\n", m_pClient->host().c_str(), m_pClient->port(), clientId, m_pClient->EClient::serverVersion());

		m_pReader = std::unique_ptr<EReader>(new EReader(m_pClient, &m_osSignal));
		m_pReader->start();
	}
	else
		printf("Cannot connect to %s:%d clientId:%d\n", m_pClient->host().c_str(), m_pClient->port(), clientId);

	return bRes;
}

void MyEWrapper::disconnect() const
{
	m_pClient->eDisconnect();

	printf("Disconnected\n");
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
		printf("Error. Id: %d, Time: %s, Code: %d, Msg: %s, AdvancedOrderRejectJson: %s\n", id, errorTimeStr, errorCode, errorString.c_str(), advancedOrderRejectJson.c_str());
	}
	else {
		printf("Error. Id: %d, Time: %s, Code: %d, Msg: %s\n", id, errorTimeStr, errorCode, errorString.c_str());
	}

	if (!alerts) return;

	// A message about one of our orders. Codes 399 and 2100-2999 are only warnings.
	if (book && book->orders.count(id)) {
		std::string what = errorString + " (code " + std::to_string(errorCode) + ")";
		bool warningOnly = errorCode == 399 || (errorCode >= 2100 && errorCode < 3000);
		// 200 unknown symbol, 201 rejected, 202 cancelled, 203 not allowed for this account.
		bool orderIsDead = errorCode >= 200 && errorCode <= 203;

		if (orderIsDead)
			endOrder(id, "IBKR rejected it: " + what, true);
		else if (!warningOnly)
			alerts->now("[ALERT] IBKR message about order " + book->orders[id].label() + ": " + what);
	}

	// TWS lost / regained its own connection to IBKR's servers.
	if (errorCode == 1100)
		alerts->fail("IBKR server connection", "TWS lost its connection to IBKR's servers", std::chrono::minutes(3));
	if (errorCode == 1101 || errorCode == 1102)
		alerts->ok("IBKR server connection");
}

void MyEWrapper::nextValidId(OrderId orderId)
{
	printf("MyEWrapper::nextValidId. Next Valid Id: %ld\n", orderId);
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
	case ST_MARKET_OPEN:
		loadRebalanceMap();
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
	if (it == book->orders.end()) return;  // not ours, or already finished

	it->second.filled = filled;

	if (status == "Filled" || remaining <= 0) {
		if (alerts) alerts->ok("Orders for " + it->second.symbol);
		book->orders.erase(it);
	}
	else if (status == "Cancelled" || status == "ApiCancelled" || status == "Inactive") {
		endOrder(orderId, "IBKR ended it (" + status + ")", false);
	}
}

// The order won't fill any further: give the unfilled part's score back and alert.
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
		// Undo the score taken off for the shares that never traded.
		double points = unfilled * t.pointsPerShare;
		gpt->useScore(t.symbol, t.action == "BUY" ? -points : points);
		msg += "Score for the unfilled shares was given back";
		msg += rejected ? ", trying again in 15 min." : ".";
	}

	if (rejected) book->retryAfter[t.symbol] = std::chrono::steady_clock::now() + kRetryAfterReject;

	printf("Order ended: %s\n", msg.c_str());
	// Keyed per ticker: repeated rejections of the same ticker alert only once,
	// and you get an [OK] once an order for it fills again.
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

// IBKR reports each (partial) fill here. Collected for the hourly Telegram summary.
void MyEWrapper::execDetails(int reqId, const Contract& contract, const Execution& execution)
{
	double shares = std::stod(DecimalFunctions::decimalToString(execution.shares));
	printf("Filled: %s %g %s @ $%.2f\n", execution.side.c_str(), shares, contract.symbol.c_str(), execution.price);
	if (alerts) alerts->addFill(execution.orderId, contract.symbol, execution.side, shares, execution.price);
}

void MyEWrapper::executeTrades()
{
	std::unordered_map<std::string, double> localCopy = gpt->snapshot();

	for (auto& [ticker, score] : localCopy)
	{
		if (std::abs(score) < 1.0) continue;

		// Recently rejected by IBKR: wait before trying this ticker again.
		if (book) {
			auto wait = book->retryAfter.find(ticker);
			if (wait != book->retryAfter.end()) {
				if (std::chrono::steady_clock::now() < wait->second) continue;
				book->retryAfter.erase(wait);
			}
		}

		std::optional<double> price = prices ? prices->get(ticker) : std::nullopt;
		if (!price) continue;  

		
		double dollars = std::abs(score) * kDollarsPerPoint;

		int shares = static_cast<int>(dollars / *price);
		if (shares == 0) continue; 

		Contract contr;
		contr.symbol = ticker;
		contr.secType = "STK";
		contr.exchange = "SMART";
		contr.currency = "USD";

		Order o;
		o.action = score > 0 ? "BUY" : "SELL";
		o.orderType = "MKT";
		o.tif = "DAY";
		o.totalQuantity = DecimalFunctions::stringToDecimal(std::to_string(shares));

		placeAndRemember(contr, o, shares, *price / kDollarsPerPoint);

		
		double used = shares * *price / kDollarsPerPoint;
		gpt->useScore(ticker, score > 0 ? used : -used);

		char msg[200];
		std::snprintf(msg, sizeof(msg), "Order placed: %s %d %s @ ~$%.2f (~$%.0f)",
			o.action.c_str(), shares, ticker.c_str(), *price, shares * *price);
		printf("%s\n", msg);
	}

	m_state = ST_TRADE_ACK;
}

bool MyEWrapper::triggerTrades()
{
	if (m_state != ST_CONNECT_ACK && m_state != ST_MARKET_OPEN_ACK && m_state != ST_TRADE_ACK) return false;

	m_state = ST_TRADE;

	printf("In trigger trades\n");
	return true;
}

void MyEWrapper::waitMktOpen()
{
	m_state = ST_WAIT_MARKET_OPEN;

	// Market closed: DAY orders that didn't fully fill are over.
	if (book) {
		std::vector<long> open;
		for (auto& [id, t] : book->orders) open.push_back(id);
		for (long id : open) endOrder(id, "not filled by the market close", false);
	}
}

void MyEWrapper::mktOpen()
{
	m_state = ST_MARKET_OPEN;
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
		if (half <= 0) continue;

		Contract contr;
		contr.symbol = symbol;
		contr.secType = "STK";
		contr.exchange = "SMART";
		contr.currency = "USD";

		Order o;
		o.orderType = "MKT";
		o.tif = "DAY";
		o.totalQuantity = DecimalFunctions::stringToDecimal(std::to_string(half));

		o.action = (pos > 0) ? "SELL" : "BUY";

		placeAndRemember(contr, o, half, 0.0);

		printf("Rebalance order placed: %s %d %s\n", o.action.c_str(), half, symbol.c_str());
	}

	engine->notifyRebalanceMap();

	m_rebalanceMap.clear();
	m_state = ST_MARKET_OPEN_ACK;
}


void MyEWrapper::position(const std::string& account, const Contract& contract, Decimal position, double avgCost)
{
	m_rebalanceMap[contract.symbol] = std::stod(DecimalFunctions::decimalToString(position));;
}

void MyEWrapper::positionEnd()
{
	m_state = ST_REBALANCE;
}
