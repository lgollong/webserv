#include "../headers/types.hpp"

#include <iostream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &name) {
	if (condition)
		return;
	std::cerr << "FAIL: " << name << std::endl;
	++failures;
}

int main() {
	Connection connection;
	connection.phase = READING;
	connection.last_activity = static_cast<time_t>(100);

	expect(!connection.hasClientTimedOut(static_cast<time_t>(129), static_cast<time_t>(30)),
		"client remains active before deadline");
	expect(connection.hasClientTimedOut(static_cast<time_t>(130), static_cast<time_t>(30)),
		"client expires at deadline");
	expect(!connection.hasClientTimedOut(static_cast<time_t>(99), static_cast<time_t>(30)),
		"clock moving backwards does not expire client");

	connection.phase = RUNNING_CGI;
	expect(!connection.hasClientTimedOut(static_cast<time_t>(200), static_cast<time_t>(30)),
		"active CGI is excluded from client timeout");

	connection.phase = WRITING;
	connection.last_activity = 0;
	expect(!connection.hasClientTimedOut(static_cast<time_t>(200), static_cast<time_t>(30)),
		"uninitialized activity does not expire client");

	if (failures == 0)
		std::cout << "connection timeout tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
