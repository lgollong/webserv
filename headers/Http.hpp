#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>
#include <sys/types.h>
#include "types.hpp"

// Byte stream <-> structured message, both directions; renders status codes (incl. errors).
class Http {
	public:
		enum {
			MAX_HEADER_BYTES = 16384,
			MAX_HEADER_FIELDS = 100
		};

		Http();
		~Http();

		// > 0: bytes consumed, a complete request was parsed into `request`.
		//   0: `inbuf` doesn't contain a complete request yet -- wait for more data.
		//  -1: `inbuf` contains a malformed request -- a parse error, not a wait.
		// Never mutates `inbuf`; the caller (Worker) owns trimming consumed bytes.
		ssize_t      parse(const std::string &inbuf, Request &request);
		std::string  build(const Response &response);
};

#endif
