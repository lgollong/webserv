#include "Logger.hpp"

Logger::Logger(std::ostream &access_log, std::ostream &error_log, LogLevel level)
	: access_log(access_log), error_log(error_log), level(level) {}

Logger::~Logger() {}

bool Logger::enabled(LogLevel messageLevel) const {
	return messageLevel != LOG_NONE && messageLevel <= level;
}

void Logger::error(const std::string &msg) {
	if (enabled(LOG_ERROR))
		error_log << "ERROR: " << msg << std::endl;
}

void Logger::access(const Connection &conn) {
	(void)conn;
}

Logger::LogStream Logger::debug() {
	return LogStream(error_log, enabled(LOG_DEBUG));
}

Logger::LogStream::LogStream(std::ostream &out, bool enabled)
	: out(out), buf(enabled ? new std::ostringstream() : 0) {}

Logger::LogStream::LogStream(const LogStream &other)
	: out(other.out), buf(other.buf) {
	other.buf = 0; // ownership transferred to *this
}

Logger::LogStream::~LogStream() {
	if (buf) {
		out << "DEBUG: " << buf->str() << std::endl;
		delete buf;
	}
}
