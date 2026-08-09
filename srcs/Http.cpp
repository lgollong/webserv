#include "Http.hpp"

Http::Http() {}

Http::~Http() {}

bool Http::parse(std::string& inbuf, Request& request) {
    (void)inbuf;
    (void)request;
    return false;
}

std::string Http::build(const Response& response) {
    (void)response;
    return std::string();
}
