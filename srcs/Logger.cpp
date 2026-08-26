#include "Logger.hpp"

Logger::Logger(std::ostream &access_log, std::ostream &error_log, int level)
	: access_log(access_log), error_log(error_log), level(level) {}

Logger::~Logger() {}

void Logger::error(const std::string &msg) {
	if (this->level <= ERROR)
		error_log << "ERROR: " << msg;
}

void Logger::access(const Connection &conn) {
	(void)conn;
}

Logger::LogStream Logger::debug() {
	return LogStream(error_log, this->level <= DEBUG);
}

Logger::LogStream::LogStream(std::ostream &out, bool enabled)
	: out(out), buf(new std::ostringstream()), enabled(enabled) {}

Logger::LogStream::LogStream(const LogStream &other)
	: out(other.out), buf(other.buf), enabled(other.enabled) {
	other.buf = 0; // ownership transferred to *this
}

Logger::LogStream::~LogStream() {
	if (buf) {
		if (enabled)
			out << "DEBUG: " << buf->str() << std::endl;
		delete buf;
	}
}
