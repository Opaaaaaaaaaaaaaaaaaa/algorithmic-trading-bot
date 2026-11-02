#pragma once

#include "DefaultEWrapper.h"
#include "EReaderOSSignal.h"
#include "EReader.h"
#include "OrderState.h"
#include "APIGpt.h"
#include "APITelegram.h"
#include "APIPrice.h"
#include "Alerts.h"

#include <memory>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <mutex>

class Engine;

class EClientSocket;

enum State {
	ST_CONNECT,
	ST_CONNECT_ACK,
	ST_WAIT_MARKET_OPEN,
	ST_MARKET_OPEN,
	ST_LOAD_REBALANCE_MAP,
	ST_REBALANCE,
	ST_MARKET_OPEN_ACK,
	ST_TRADE,
	ST_TRADE_ACK
};

// One order the bot placed, followed until it is fully filled or ends.
struct TrackedOrder {
	std::string symbol, action;      // action is "BUY" or "SELL"
	int shares = 0;
	double pointsPerShare = 0;       // score taken off per share when placed (0 for rebalance orders)
	double filled = 0;
	std::chrono::steady_clock::time_point placed;
	bool slowAlerted = false;
	bool hedge = false;              // a hedge order: its fills change the saved hedge share count
	std::string label() const { return action + " " + std::to_string(shares) + " " + symbol; }
};

// Lives in main() so it survives reconnects to TWS (not restarts of the bot).
struct OrderBook {
	std::unordered_map<long, TrackedOrder> orders;  // order id -> order
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> retryAfter;  // symbol -> wait until
};

class MyEWrapper : public DefaultEWrapper
{
public:
	MyEWrapper();
	~MyEWrapper();

	bool connect(const char* host, int port, int clientId = 0);
	void disconnect() const;
	bool isConnected() const;
	void setConnectOptions(const std::string&);
	void processMessages();
	void nextValidId(OrderId orderId) override;
	void error(int id, time_t errorTime, int errorCode, const std::string& errorString, const std::string& advancedOrderRejectJson);
	void position(const std::string& account, const Contract& contract, Decimal position, double avgCost);
	void positionEnd();
	void execDetails(int reqId, const Contract& contract, const Execution& execution) override;
	void accountSummary(int reqId, const std::string& account, const std::string& tag,
		const std::string& value, const std::string& currency);
	void accountSummaryEnd(int reqId);
	void updatePortfolio(const Contract& contract, Decimal position, double marketPrice, double marketValue,
		double averageCost, double unrealizedPNL, double realizedPNL, const std::string& accountName);
	void accountDownloadEnd(const std::string& accountName);
	// IBKR's order status update. permId is int in some TWS API versions and long long in
	// others; both are declared so whichever one your version uses gets called.
	void orderStatus(OrderId orderId, const std::string& status, Decimal filled, Decimal remaining,
		double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId,
		const std::string& whyHeld, double mktCapPrice);
	void orderStatus(OrderId orderId, const std::string& status, Decimal filled, Decimal remaining,
		double avgFillPrice, long long permId, int parentId, double lastFillPrice, int clientId,
		const std::string& whyHeld, double mktCapPrice);

public:
	void myTest();
	void setEngine(Engine& e);
	void setGpt(APIGpt& g);
	void setPrices(APIPrice& p);
	void setAlerts(Alerts& a);
	void setOrderBook(OrderBook& b);
	void setState(StateStore& s);
	// Call regularly: alerts about orders that are taking too long to fill.
	void checkOrders();
	// Call regularly: logs the positions once an hour and finishes the daily summary.
	void reportTick();
	void executeTrades();
	void waitMktOpen();
	void mktOpen();
	void loadRebalanceMap();
	void rebalance();
	bool triggerTrades();


private:
	Engine* engine;
	APIGpt* gpt;
	APIPrice* prices = nullptr;
	Alerts* alerts = nullptr;
	OrderBook* book = nullptr;
	StateStore* state = nullptr;

	void placeAndRemember(const Contract& contr, const Order& o, int shares, double pointsPerShare, bool hedge = false);
	double hedgeShares() const;
	void setHedgeShares(double shares);
	void addHedgeFill(const TrackedOrder& t, double filled);
	void onOrderStatus(long orderId, const std::string& status, double filled, double remaining);
	void endOrder(long orderId, const std::string& why, bool rejected);
	void requestAccountSummary(bool forDailySummary);
	void sendDailySummary(const std::string& positions);
	void requestHedgeCheck();
	void publishPerformance(bool closed, double account, double longValue, double shortValue, const std::string& sign);
	void adjustHedge();
	std::unordered_map<std::string, double> m_rebalanceMap;
	std::unordered_map<std::string, double> m_avgCost;  // symbol -> average price paid per share

	bool m_summaryPending = false;   // waiting for IBKR's account summary
	bool m_dailyPending = false;     // send the daily summary once it arrives
	std::chrono::steady_clock::time_point m_summaryAsked, m_lastPositionsLog;
	std::string m_summaryCurrency;
	std::unordered_map<std::string, double> m_summaryValues;  // tag -> value
	std::string m_account;

	bool m_hedgePending = false;     // waiting for IBKR's portfolio to size the hedge
	std::chrono::steady_clock::time_point m_hedgeAsked, m_lastHedge;
	double m_hedgeShortValue = 0;    // dollars the bot is short (SPY not included)
	double m_hedgePrice = 0;

	EReaderOSSignal m_osSignal;
	EClientSocket* const m_pClient;
	State m_state;
	time_t m_sleepDeadline;

	OrderId m_orderId;
	std::unique_ptr<EReader> m_pReader;
	bool m_extraAuth;
	std::string m_bboExchange;
};