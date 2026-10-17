#include "../headers/Logger.hpp"

#include <iostream>
#include <sstream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &message) {
	if (!condition) {
		std::cerr << "failure: " << message << std::endl;
		++failures;
	}
}

static void expectLevel(LogLevel level, bool errorShown, bool debugShown, const std::string &name) {
	std::ostringstream access;
	std::ostringstream errors;
	Logger logger(access, errors, level);
	logger.error("error message");
	logger.debug() << "debug " << 42;

	const std::string output = errors.str();
	expect((output.find("ERROR: error message\n") != std::string::npos) == errorShown,
		name + (errorShown ? " writes" : " suppresses") + " error messages");
	expect((output.find("DEBUG: debug 42\n") != std::string::npos) == debugShown,
		name + (debugShown ? " writes" : " suppresses") + " debug messages");
	expect(logger.enabled(LOG_ERROR) == errorShown && logger.enabled(LOG_DEBUG) == debugShown,
		name + " reports enabled levels consistently");
	expect(!logger.enabled(LOG_NONE), name + " never enables LOG_NONE as a message level");
	expect(access.str().empty(), name + " writes nothing to the access log");
}

int main() {
	expectLevel(LOG_NONE, false, false, "LOG_NONE");
	expectLevel(LOG_ERROR, true, false, "LOG_ERROR");
	expectLevel(LOG_DEBUG, true, true, "LOG_DEBUG");

	std::ostringstream access;
	std::ostringstream errors;
	Logger logger(access, errors, LOG_DEBUG);
	logger.debug() << "first";
	logger.debug() << "second";
	expect(errors.str() == "DEBUG: first\nDEBUG: second\n",
		"each debug expression flushes exactly one line in order");

	if (failures == 0)
		std::cout << "logger tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
