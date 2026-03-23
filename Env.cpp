#include "Env.h"
#include <fstream>
#include <cstdlib>

void loadEnvFile(const std::string& path) {
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

#ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }
}