#include "APITelegram.h"

void APITelegram::sendMessage(std::string s)
{
	sendTo(chatid, s);
}

void APITelegram::sendPublic(std::string s)
{
	if (!publicChat.empty()) sendTo(publicChat, s);
}

void APITelegram::sendTo(const std::string& chat, const std::string& s)
{
	std::string url =
		"https://api.telegram.org/bot" + token + "/sendMessage";

    auto response = cpr::Post(
        cpr::Url{ url },
        cpr::Payload{
            {"chat_id", chat},
            {"text", s}
        },
        cpr::Timeout{ 10000 }
    );

    std::cout << "Status: " << response.status_code << std::endl;
    std::cout << "Response: " << response.text << std::endl;
}