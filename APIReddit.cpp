#include "APIReddit.h"

APIReddit::APIReddit()
    : user_agent("wsbInverseTest/0.1 by u/InverseBotWsbDegens")
    , commentsURL("https://oauth.reddit.com/r/wallstreetbets/comments")
    , postsURL("https://oauth.reddit.com/r/wallstreetbets/new")
    , cutofftime(3600)
{
    init();
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
        }
    );

    nlohmann::json token_json = nlohmann::json::parse(token_res.text);

    token = token_json["access_token"];

    int expires_in = token_json["expires_in"];  
    tokenExpiry = std::chrono::system_clock::now() + std::chrono::seconds(expires_in - 60);
}

bool APIReddit::fetch(std::string what)
{
    if (std::chrono::system_clock::now() >= tokenExpiry) init();

    long now = time(NULL);
    long cutoff = now - cutofftime; 

    cpr::Response api_res = cpr::Get(
        cpr::Url{ what },
        cpr::Header{
            {"Authorization", "bearer " + token},
            {"User-Agent", user_agent}
        },
        cpr::Parameters{ {"limit", "100"} }
    );

    if (api_res.status_code != 200) {
        std::cout << "API request failed: " << api_res.status_code << "\n";
        std::cout << api_res.text << "\n";
        return false;
    }

    nlohmann::json feed = nlohmann::json::parse(api_res.text);

    nlohmann::json dataArray = nlohmann::json::array();

    auto children = feed["data"]["children"];

    if (children.size() > 1) {
        double newest = children.front()["data"]["created_utc"].get<double>();
        double oldest = children.back()["data"]["created_utc"].get<double>();
        double mins = (newest - oldest) / 60.0;
        if (mins > 0)
            std::cout << "WSB rate: ~" << (children.size() / mins) << " comments/min\n";
    }

	int cnt = 0;

    for (const auto& child : children) {
        const auto& fdata = child["data"];

        std::time_t created = static_cast<std::time_t>(fdata["created_utc"].get<double>());

        // Skip comments already sent on a previous fetch.
        const std::string name = fdata.value("name", "");
        bool isNew = name.empty() || seen.emplace(name, created).second;

        if (created >= cutoff && isNew) {
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
    }

    // Forget comments older than the cutoff; they can't come back anyway.
    for (auto it = seen.begin(); it != seen.end(); )
        it = (it->second < cutoff) ? seen.erase(it) : std::next(it);

    nlohmann::json finalPayload;
    finalPayload["data"] = dataArray;

	latestPayload = finalPayload;

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