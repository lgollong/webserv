#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>
#include <sys/types.h>
#include "types.hpp"

class Http {
	public:
		enum { MAX_HEADER_BYTES = 16384 };

		Http();
		~Http();

		ssize_t      parse(const std::string &inbuf, Request &request, size_t maxBodyBytes, int &errorStatus);
		Response     defaultErrorResponse(int status) const;
		bool         supportsStatus(int status) const;
		std::string  build(const Response &response);
		std::string  buildHead(const Response &response, size_t bodyLength);
};

#endif
