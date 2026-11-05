#include "APIReddit.h"

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
    token.clear();
    tokenExpiry = std::chrono::system_clock::now() + std::chrono::minutes(1);

    for (bool appOnly : { true, false }) {
        cpr::Payload payload = appOnly
            ? cpr::Payload{ {"grant_type", "client_credentials"} }
            : cpr::Payload{ {"grant_type", "password"}, {"username", username}, {"password", password} };

        cpr::Response r = cpr::Post(
            cpr::Url{ "https://www.reddit.com/api/v1/access_token" },
            cpr::Authentication{ client_id, client_secret, cpr::AuthMode::BASIC },
            cpr::Header{ {"User-Agent", user_agent} },
            payload,
            cpr::Timeout{ 30000 }
        );

        try {
            nlohmann::json j = nlohmann::json::parse(r.text);
            if (j.contains("access_token") && j["access_token"].is_string()) {
                token = j["access_token"].get<std::string>();
                int expiresIn = j.contains("expires_in") && j["expires_in"].is_number() ? j["expires_in"].get<int>() : 3600;
                tokenExpiry = std::chrono::system_clock::now() + std::chrono::seconds(expiresIn - 60);
                std::cout << "Reddit login OK (" << (appOnly ? "as the app, read-only" : "as your account") << ")\n";
                return;
            }
        }
        catch (const std::exception&) {}

        std::cout << "Reddit login failed (" << (appOnly ? "as the app" : "as your account") << "): HTTP "
                  << r.status_code << " " << r.text.substr(0, 200) << "\n";
    }
}

bool APIReddit::fetch(std::string what)
{
    latestPayload = { {"data", nlohmann::json::array()} };

    if (std::chrono::system_clock::now() >= tokenExpiry) init();
    if (token.empty()) return false;
    lastBlocked = false;

    std::time_t now = time(NULL);
    std::time_t cutoff = now - cutofftime;
    bool hadHistory = !seen.empty();

    nlohmann::json dataArray = nlohmann::json::array();
    int cnt = 0;
    int requests = 0;
    bool caughtUp = false;  
    std::string after;      

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
            lastBlocked = api_res.text.find("blocked by network security") != std::string::npos;
            if (lastBlocked)
                std::cout << "Reddit request failed: HTTP " << api_res.status_code
                          << ", Reddit's \"blocked by network security\" page\n";
            else
                std::cout << "Reddit request failed: HTTP " << api_res.status_code << " "
                          << api_res.text.substr(0, 200) << "\n";
            if (api_res.status_code == 401) tokenExpiry = std::chrono::system_clock::now();
            if (cnt == 0) return false;
            break;  
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

    for (auto it = seen.begin(); it != seen.end(); )
        it = (it->second < cutoff) ? seen.erase(it) : std::next(it);

    latestPayload["data"] = dataArray;

    return true;
}

bool APIReddit::fetchComments()
{
    if (activeURL.empty()) activeURL = commentsURL;
    bool ok = fetch(activeURL);
    if (ok || !lastBlocked) return ok;

    auto now = std::chrono::steady_clock::now();
    if (lastCheck != std::chrono::steady_clock::time_point{} && now - lastCheck < std::chrono::minutes(30)) return false;
    lastCheck = now;

    std::string before = activeURL;
    findWorkingUrl();
    if (activeURL == before) return false;
    return fetch(activeURL);
}

std::string APIReddit::checkUrl(const std::string& url)
{
    cpr::Response r = cpr::Get(
        cpr::Url{ url },
        cpr::Header{ {"Authorization", "bearer " + token}, {"User-Agent", user_agent} },
        cpr::Parameters{ {"limit", "1"} },
        cpr::Timeout{ 30000 }
    );
    if (r.status_code == 200) {
        try {
            nlohmann::json j = nlohmann::json::parse(r.text);
            if (j.contains("data") && j["data"].contains("children")) return "";
        }
        catch (const std::exception&) {}
        return "HTTP 200 but not a listing";
    }
    if (r.text.find("blocked by network security") != std::string::npos)
        return "HTTP " + std::to_string(r.status_code) + " blocked";
    return "HTTP " + std::to_string(r.status_code) + " " + r.text.substr(0, 100);
}

void APIReddit::findWorkingUrl()
{
    const std::vector<std::string> candidates = {
        commentsURL,
        commentsURL + ".json",
        commentsURL + "/",
        "https://oauth.reddit.com/r/wallstreetbets%2Fcomments",
        postsURL,
    };

    std::cout << "Reddit is blocking the comments feed, checking other ways in:\n";
    std::string found;
    for (const std::string& url : candidates) {
        std::string problem = checkUrl(url);
        std::cout << "  " << url << ": " << (problem.empty() ? "works" : problem) << "\n";
        if (problem.empty() && found.empty()) found = url;
    }

    if (found.empty()) {
        std::cout << "Reddit check: nothing works, trying again in 30 min\n";
        return;
    }
    if (found == postsURL)
        std::cout << "Reddit check: only new posts work, reading post titles and text instead of comments\n";
    else
        std::cout << "Reddit check: reading comments from " << found << "\n";
    activeURL = found;
}

bool APIReddit::fetchPosts()
{
    return fetch(postsURL);
}

nlohmann::json APIReddit::getLatestPayload() const
{
    return latestPayload;
}