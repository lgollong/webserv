#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>
#include <sys/types.h>
#include "types.hpp"

class Http {
	public:
		// Transfer framing has its own limits because the decoded body limit
		// does not count chunk extensions or trailers.
		enum {
			MAX_HEADER_BYTES = 16384,
			MAX_CHUNK_LINE_BYTES = 1024,
			MAX_CHUNK_TRAILER_BYTES = 16384,
			MAX_CHUNK_METADATA_BYTES = 1024 * 1024
		};

		Http();
		~Http();

		ssize_t      parseHead(const std::string &inbuf, Request &request, BodyReader &reader, int &errorStatus);
		int          parseBody(std::string &inbuf, Request &request, BodyReader &reader, size_t maxBodyBytes, int &errorStatus);
		Response     defaultErrorResponse(int status) const;
		bool         supportsStatus(int status) const;
		std::string  buildHead(const Response &response, size_t bodyLength);
};

#endif
