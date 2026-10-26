#include "State.h"

#include <filesystem>
#include <fstream>
#include <iostream>

StateStore::StateStore(std::string p)
    : path(std::move(p))
    , data(nlohmann::json::object())
{
    std::ifstream in(path);
    if (!in) {
        std::cout << "No saved state (" << path << "), starting fresh\n";
        return;
    }

    try {
        in >> data;
        if (!data.is_object()) throw std::runtime_error("not a JSON object");
        std::cout << "Loaded saved state from " << path << "\n";
    }
    catch (const std::exception& e) {
        in.close();
        std::cerr << "Saved state " << path << " is unreadable (" << e.what()
                  << "), starting fresh. Old file kept as " << path << ".bad\n";
        std::error_code ec;
        std::filesystem::rename(path, path + ".bad", ec);
        data = nlohmann::json::object();
    }
}

nlohmann::json StateStore::get(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m);
    auto it = data.find(key);
    return it != data.end() ? *it : nlohmann::json();
}

void StateStore::set(const std::string& key, nlohmann::json value)
{
    std::lock_guard<std::mutex> lock(m);
    data[key] = std::move(value);
    write();
}

void StateStore::write()
{
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << data.dump();
        if (!out) {
            std::cerr << "Could not write " << tmp << "\n";
            return;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::cerr << "Could not save state to " << path << ": " << ec.message() << "\n";
}
