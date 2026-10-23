#ifndef WORKER_HPP
#define WORKER_HPP

#include <map>
#include "types.hpp"
#include "Poller.hpp"
#include "Config.hpp"
#include "Http.hpp"
#include "Cgi.hpp"
#include "StaticFile.hpp"
#include "Session.hpp"

class Worker {
	public:
		Worker(Config &config, Http &http, Cgi &cgi, StaticFile &files);
		~Worker();

		void run();

	private:
		std::map<int, size_t>      listeners;
		std::map<int, Connection>  connections;
		std::map<int, Connection*> fdToConnection;
		Poller                     poller;
		Config                     &config;
		Http                       &http;
		Cgi                        &cgi;
		StaticFile                 &files;
		SessionStore               sessions;
		bool                       debug;

		void dispatchEvent(const pollfd &event);
		void acceptNew(int listen_fd, size_t serverIndex);
		void onReadable(Connection &conn);
		void onWritable(Connection &conn);
		void onCgiReadable(Connection &conn);
		void onCgiWritable(Connection &conn);
		void processBufferedRequest(Connection &conn);
		void handleRequest(Connection &conn);
		void startCgi(Connection &conn);
		void respond(Connection &conn, int status);
		void respondWith(Connection &conn, const Content &content);
		void queueResponse(Connection &conn);
		void queueSessionResponse(Connection &conn);
		void finishCgiResponse(Connection &conn);
		void finishClientResponse(Connection &conn);
		void sweepExpiredConnections();
		void closeManagedFd(int &fd);
		void closeConnection(Connection &conn);

		Worker(const Worker &other);
};

#endif
