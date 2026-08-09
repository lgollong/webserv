#include "Cgi.hpp"

Cgi::Cgi() {}

Cgi::~Cgi() {}

CgiJob Cgi::start(const Request& request, const Route& route) {
    (void)request;
    (void)route;
    return CgiJob();
}

bool Cgi::collect(CgiJob& cgi) {
    (void)cgi;
    return false;
}
