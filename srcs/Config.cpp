#include "Config.hpp"

Config::Config() {}

Config::~Config() {}

Route Config::route(const Request& request) const {
    (void)request;
    return Route();
}
