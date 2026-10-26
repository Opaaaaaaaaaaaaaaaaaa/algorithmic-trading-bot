#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <string>
#include <unordered_map>


class APIReddit {
public:
    APIReddit();
    bool fetchComments();
	bool fetchPosts();
    nlohmann::json getLatestPayload() const;

private:
    void init();
    bool fetch(std::string what);

private:
    nlohmann::json latestPayload;
    std::unordered_map<std::string, std::time_t> seen; // Reddit id -> created time
    int cutofftime;
    std::chrono::system_clock::time_point tokenExpiry;

    const std::string commentsURL;
    const std::string postsURL;
    const std::string client_id = std::getenv("REDDIT_ID");;
    const std::string client_secret = std::getenv("REDDIT_SECRET");;
    const std::string username = std::getenv("REDDIT_USERNAME");
    const std::string password = std::getenv("REDDIT_PASSWORD");
    const std::string user_agent;
    std::string token;
};