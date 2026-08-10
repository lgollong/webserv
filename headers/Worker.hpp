#ifndef WORKER_HPP
#define WORKER_HPP

#include <map>
#include "types.hpp"
#include "Poller.hpp"
#include "Config.hpp"
#include "Http.hpp"
#include "Cgi.hpp"
#include "StaticFile.hpp"
#include "Logger.hpp"

// Runs the loop; owns all connections; dispatches on phase; orchestrates the other services.
// Every other class is a passive service Worker calls; services never call each other.
class Worker {
    public:
        Worker(Config& config, Http& http, Cgi& cgi, StaticFile& files, Logger& logger);
        ~Worker();

        void start();

    private:
        std::map<int, Connection>  connections;
        Poller                     poller;
        Config&                    config;
        Http&                      http;
        Cgi&                       cgi;
        StaticFile&                files;
        Logger&                    logger;

        void acceptNew(int listen_fd);
        void onReadable(int client_fd);
        void onWritable(int client_fd);

        Worker(const Worker& other);
        Worker& operator=(const Worker& other);
};

#endif
