#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include "types.hpp"

// Parses the config file (startup); resolves a request to its location (per request).
class Config {
    public:
        Config();
        ~Config();

        Route route(const Request& request) const;

    private:
        std::vector<ServerConfig> servers;
};

#endif
