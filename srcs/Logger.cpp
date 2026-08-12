#include "Logger.hpp"

Logger::Logger(std::ostream& access_log, std::ostream& error_log, int level)
	: access_log(access_log), error_log(error_log), level(level) {}

Logger::~Logger() {}

void Logger::error(const std::string& msg, int level) {
	if (level <= this->level)
	error_log << msg;
}

void Logger::access(const Connection& conn) {
	(void)conn;
}
