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
		telegram.sendMessage(errorString.c_str());
		telegram.sendMessage(advancedOrderRejectJson.c_str());
	}
	else {
		printf("Error. Id: %d, Time: %s, Code: %d, Msg: %s\n", id, errorTimeStr, errorCode, errorString.c_str());
	}
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

void MyEWrapper::executeTrades()
{
	printf("In execute trades\n");

	std::unordered_map<std::string, double> localCopy = gpt->snapshot();

	for (auto& [ticker, instruction] : localCopy)
	{
		Contract contr;
		contr.symbol = ticker;
		contr.secType = "STK";
		contr.exchange = "SMART";
		contr.currency = "USD";

		Order o;
		if(instruction >= 1) o.action = "BUY";
		else if(instruction <= -1) o.action = "SELL";
		else continue;

		int qt = static_cast<int>(instruction);
		int amount = abs(qt);
		if (amount == 0) continue;

		o.orderType = "MKT";
		o.tif = "DAY";
		o.totalQuantity = DecimalFunctions::stringToDecimal(std::to_string(amount));

		printf("%d\n", qt);

		m_pClient->placeOrder(m_orderId++, contr, o);

		telegram.sendMessage("Order placed: Ticker " + ticker + " Amount " + std::to_string(qt));
	}

	engine->notifyTradeCycleComplete();

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

		m_pClient->placeOrder(m_orderId++, contr, o);

		telegram.sendMessage("Order placed: Ticker " + symbol + " Amount " + std::to_string(half));
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
