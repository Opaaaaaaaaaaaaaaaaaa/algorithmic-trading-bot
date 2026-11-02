#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>

class APITelegram {
public:
	// Private alerts, to TELEGRAM_ID.
	void sendMessage(std::string s);
	// Public channel, to TELEGRAM_PUBLIC_ID (e.g. @mychannel). Does nothing if that isn't set.
	void sendPublic(std::string s);

private:
	void sendTo(const std::string& chat, const std::string& s);
	static std::string env(const char* name);

	const std::string token = env("TELEGRAM_TOKEN");
	const std::string chatid = env("TELEGRAM_ID");
	const std::string publicChat = env("TELEGRAM_PUBLIC_ID");
};