#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <ostream>
#include <sstream>
#include <string>
#include "types.hpp"

class Logger {
	public:
		Logger(std::ostream &access_log, std::ostream &error_log, LogLevel level);
		~Logger();

		bool enabled(LogLevel messageLevel) const;
		void error(const std::string &msg);
		void access(const Connection &conn);

		// stream-style debug logging: logger.debug() << "foo" << x << "bar";
		// buffers via operator<< and flushes to error_log once the returned
		// LogStream is destroyed, i.e. at the end of the full expression
		// that created it. Copying transfers ownership of the buffer (like
		// std::auto_ptr) so only the surviving instance ever flushes.
		// A disabled stream owns no buffer and ignores everything streamed.
		class LogStream {
			public:
				LogStream(std::ostream &out, bool enabled);
				LogStream(const LogStream &other);
				~LogStream();

				template <typename T>
				LogStream& operator<<(const T &value) {
					if (buf)
						(*buf) << value;
					return *this;
				}

			private:
				std::ostream               &out;
				mutable std::ostringstream *buf;
		};

		LogStream debug();

	private:
		std::ostream  &access_log;
		std::ostream  &error_log;
		LogLevel       level;

		Logger(const Logger &other);
		Logger& operator=(const Logger &other);
};

#endif
