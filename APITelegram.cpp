#include "APITelegram.h"

void APITelegram::sendMessage(std::string s)
{
	std::string url =
		"https://api.telegram.org/bot" + token + "/sendMessage";

    auto response = cpr::Post(
        cpr::Url{ url },
        cpr::Payload{
            {"chat_id", chatid},
            {"text", s}
        },
        cpr::Timeout{ 10000 }
    );

    std::cout << "Status: " << response.status_code << std::endl;
    std::cout << "Response: " << response.text << std::endl;
}