#pragma once

#include "DefaultEWrapper.h"
#include "EReaderOSSignal.h"
#include "EReader.h"
#include "OrderState.h"
#include "APIGpt.h"
#include "APITelegram.h"
#include "APIPrice.h"

#include <memory>
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

public:
	void myTest();
	void setEngine(Engine& e);
	void setGpt(APIGpt& g);
	void setPrices(APIPrice& p);
	void executeTrades();
	void waitMktOpen();
	void mktOpen();
	void loadRebalanceMap();
	void rebalance();
	bool triggerTrades();


private:
	Engine* engine;
	APITelegram telegram;
	APIGpt* gpt;
	APIPrice* prices = nullptr;
	std::unordered_map<std::string, double> m_rebalanceMap;

	EReaderOSSignal m_osSignal;
	EClientSocket* const m_pClient;
	State m_state;
	time_t m_sleepDeadline;

	OrderId m_orderId;
	std::unique_ptr<EReader> m_pReader;
	bool m_extraAuth;
	std::string m_bboExchange;
};