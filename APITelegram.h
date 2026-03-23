#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <chrono>

class APITelegram {
public:
	void sendMessage(std::string s);

private:
	const std::string token = std::getenv("TELEGRAM_TOKEN");
	const std::string chatid = std::getenv("TELEGRAM_ID");
};