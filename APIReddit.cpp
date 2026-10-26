#include "APIReddit.h"

// Reddit returns at most 100 items per request and keeps about 1000 in a listing.
const int kPageSize = 100;
const int kMaxPages = 10;

APIReddit::APIReddit(StateStore& s)
    : user_agent("wsbInverseTest/0.1 by u/InverseBotWsbDegens")
    , commentsURL("https://oauth.reddit.com/r/wallstreetbets/comments")
    , postsURL("https://oauth.reddit.com/r/wallstreetbets/new")
    , cutofftime(3600)
    , state(s)
{
    loadState();
    init();
}

void APIReddit::loadState()
{
    nlohmann::json saved = state.get("reddit");
    if (!saved.is_object()) return;

    if (saved.contains("seen") && saved["seen"].is_object())
        for (auto& [name, created] : saved["seen"].items())
            if (created.is_number()) seen[name] = created.get<std::time_t>();

    if (saved.contains("lastFetch") && saved["lastFetch"].is_number())
        lastFetchTime = saved["lastFetch"].get<std::time_t>();

    std::cout << "Remembered " << seen.size() << " already-processed comments\n";
}

void APIReddit::saveState()
{
    state.set("reddit", { {"seen", seen}, {"lastFetch", lastFetchTime} });
}

void APIReddit::init()
{
    cpr::Response token_res = cpr::Post(
        cpr::Url{ "https://www.reddit.com/api/v1/access_token" },
        cpr::Authentication{ client_id, client_secret, cpr::AuthMode::BASIC },
        cpr::Header{ {"User-Agent", user_agent} },
        cpr::Payload{
            {"grant_type", "password"},
            {"username", username},
            {"password", password}
        },
        cpr::Timeout{ 30000 }
    );

    nlohmann::json token_json = nlohmann::json::parse(token_res.text);

    token = token_json["access_token"];

    int expires_in = token_json["expires_in"];  
    tokenExpiry = std::chrono::system_clock::now() + std::chrono::seconds(expires_in - 60);
}

bool APIReddit::fetch(std::string what)
{
    latestPayload = { {"data", nlohmann::json::array()} };

    if (std::chrono::system_clock::now() >= tokenExpiry) init();

    std::time_t now = time(NULL);
    std::time_t cutoff = now - cutofftime;
    bool hadHistory = !seen.empty();

    nlohmann::json dataArray = nlohmann::json::array();
    int cnt = 0;
    int requests = 0;
    bool caughtUp = false;  // reached a comment we already have, or one older than the cutoff
    std::string after;      // Reddit's marker for the next (older) page

    // Newest comments come first. Keep paging back until we reach one we've
    // already processed, so nothing posted since the last fetch is missed.
    while (!caughtUp && requests < kMaxPages)
    {
        cpr::Parameters params = after.empty()
            ? cpr::Parameters{ {"limit", std::to_string(kPageSize)} }
            : cpr::Parameters{ {"limit", std::to_string(kPageSize)}, {"after", after} };

        cpr::Response api_res = cpr::Get(
            cpr::Url{ what },
            cpr::Header{
                {"Authorization", "bearer " + token},
                {"User-Agent", user_agent}
            },
            params,
            cpr::Timeout{ 30000 }
        );
        ++requests;

        if (api_res.status_code != 200) {
            std::cout << "API request failed: " << api_res.status_code << "\n";
            std::cout << api_res.text << "\n";
            if (cnt == 0) return false;
            break;  // keep the pages we already got
        }

        nlohmann::json feed;
        try {
            feed = nlohmann::json::parse(api_res.text);
        }
        catch (const std::exception& e) {
            std::cout << "Reddit sent bad JSON: " << e.what() << "\n";
            if (cnt == 0) return false;
            break;
        }

        const auto& children = feed["data"]["children"];

        for (const auto& child : children) {
            const auto& fdata = child["data"];

            std::time_t created = static_cast<std::time_t>(fdata["created_utc"].get<double>());
            const std::string name = fdata.value("name", "");

            if (created < cutoff || seen.count(name)) {
                caughtUp = true;
                break;
            }

            if (!name.empty()) seen.emplace(name, created);

            nlohmann::json dataObj;
            dataObj["id"] = cnt++;

            if (fdata.contains("body"))
            {
                dataObj["text"] = fdata.value("body", "");
            }
            else
            {
                dataObj["text"] = fdata.value("title", "") + " " + fdata.value("selftext", "");
            }

            dataArray.push_back(dataObj);
        }

        const auto& next = feed["data"]["after"];
        if (children.empty() || !next.is_string()) break;
        after = next.get<std::string>();
    }

    if (!caughtUp && hadHistory)
        std::cout << "WARNING: couldn't reach the last processed comment, some comments were skipped\n";

    if (lastFetchTime > 0 && now > lastFetchTime) {
        double mins = (now - lastFetchTime) / 60.0;
        std::cout << "WSB rate: ~" << (cnt / mins) << " comments/min (" << cnt
                  << " new, " << requests << " request" << (requests == 1 ? "" : "s") << ")\n";
    }
    else {
        std::cout << "Reddit: " << cnt << " new comments (" << requests
                  << " request" << (requests == 1 ? "" : "s") << ")\n";
    }
    lastFetchTime = now;

    // Forget comments older than the cutoff; they can't come back anyway.
    for (auto it = seen.begin(); it != seen.end(); )
        it = (it->second < cutoff) ? seen.erase(it) : std::next(it);

    latestPayload["data"] = dataArray;

    return true;
}

bool APIReddit::fetchComments()
{
    return fetch(commentsURL);
}

bool APIReddit::fetchPosts()
{
    return fetch(postsURL);
}

nlohmann::json APIReddit::getLatestPayload() const
{
    return latestPayload;
}