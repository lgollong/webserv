#include <iostream>
#include "../headers/types.hpp"
#include "../headers/StaticFile.hpp"
#include "../headers/Logger.hpp"
#include "../headers/Cgi.hpp"
#include "../headers/Http.hpp"
#include "../headers/Config.hpp"
#include "../headers/Worker.hpp"

int main(int argc, char *argv[]){
	if (argc != 2) {
		std::cout << "Wrong number of arguments" << std::endl;
		return (1);
	}

	Logger logger(std::cout, std::cerr, DEBUG);

	try {
		Config config(argv[1]);
		Http http;
		Cgi cgi;
		StaticFile files;
		Worker worker(config, http, cgi, files, logger);

		worker.run();
	}
	catch (const std::exception &e) {
		logger.error(e.what());
		return (1);
	}
	return (0);
}