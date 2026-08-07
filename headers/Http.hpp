#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>
#include "types.hpp"

// Byte stream <-> structured message, both directions; renders status codes (incl. errors).
class Http {
    public:
        Http();
        ~Http();

        bool         parse(std::string& inbuf, Request& request);
        std::string  build(const Response& response);
};

#endif
