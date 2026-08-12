#include <iostream>
#include "../headers/StaticFile.hpp"
#include "../headers/Logger.hpp"
#include "../headers/Cgi.hpp"
#include "../headers/Http.hpp"
#include "../headers/Config.hpp"
#include "../headers/Worker.hpp"

// add try catch stuff for worker
int main(int argc, char *argv[]){
	if (argc != 2) {
		std::cout << "Wrong number of arguments" << std::endl;
		return (1);
	}

	Logger logger(std::cout, std::cerr, 1);

	try {
		Config config(argv[1]);
		Http http;
		Cgi cgi;
		StaticFile files;
		Worker worker(config, http, cgi, files, logger);

		worker.start();
	}
	catch (const std::exception &e) {
		logger.error(e.what(), 0);
		return (1);
	}
	return (0);
}