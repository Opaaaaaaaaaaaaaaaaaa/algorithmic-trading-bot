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
	ST_LOAD_REBALANCE_MAP,
	ST_REBALANCE,
	ST_MARKET_OPEN_ACK,
	ST_TRADE,
	ST_TRADE_ACK
};

struct TrackedOrder {
	std::string symbol, action;
	int shares = 0;
	double pointsPerShare = 0;
	double filled = 0;
	std::chrono::steady_clock::time_point placed;
	bool slowAlerted = false;
	std::string label() const { return action + " " + std::to_string(shares) + " " + symbol; }
};

struct OrderBook {
	std::unordered_map<long, TrackedOrder> orders;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> retryAfter;
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
	void checkOrders();
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

	void placeAndRemember(const Contract& contr, const Order& o, int shares, double pointsPerShare);
	void onOrderStatus(long orderId, const std::string& status, double filled, double remaining);
	void endOrder(long orderId, const std::string& why, bool rejected);
	void requestAccountSummary(bool forDailySummary);
	void sendDailySummary(const std::string& positions);
	void startOvernightTest();
	void finishOvernightTest();
	void publishPerformance(bool closed, double account, double longValue, double shortValue, const std::string& sign);
	std::unordered_map<std::string, double> m_rebalanceMap;
	std::unordered_map<std::string, double> m_avgCost;

	bool m_summaryPending = false;
	bool m_dailyPending = false;
	std::chrono::steady_clock::time_point m_summaryAsked, m_lastPositionsLog;
	std::string m_summaryCurrency;
	std::unordered_map<std::string, double> m_summaryValues;
	std::string m_account;

	std::chrono::steady_clock::time_point m_overnightTried;

	EReaderOSSignal m_osSignal;
	EClientSocket* const m_pClient;
	State m_state;
	time_t m_sleepDeadline;

	OrderId m_orderId;
	std::unique_ptr<EReader> m_pReader;
	bool m_extraAuth;
	std::string m_bboExchange;
};