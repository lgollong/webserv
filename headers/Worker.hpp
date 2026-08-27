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
#include "Session.hpp"

class WorkerEventLoopTests;

// Runs the loop; owns all connections; dispatches on phase; orchestrates the other services.
// Every other class is a passive service Worker calls; services never call each other.
class Worker {
	public:
		Worker(Config &config, Http &http, Cgi &cgi, StaticFile &files, Logger &logger);
		~Worker();

		void run();

	private:
		friend class WorkerEventLoopTests;

		std::map<int, Connection>  connections;
		std::map<int, Connection*> fdToConnection;
		std::map<pid_t, time_t>    pendingCgiReaps;
		Poller                     poller;
		Config                     &config;
		Http                       &http;
		Cgi                        &cgi;
		StaticFile                 &files;
		Logger                     &logger;
		SessionStore               sessions;

		void acceptNew(int listen_fd, size_t serverIndex);
		void dispatchReadyEvents(const std::vector<pollfd> &readyFds, std::map<int, size_t> &listeners,
			const std::vector<ServerConfig> &servers);
		void onReadable(Connection &conn);
		void onWritable(Connection &conn);
		void onCgiReadable(Connection &conn);
		void onCgiWritable(Connection &conn);
		void processBufferedRequest(Connection &conn);
		void queueParserError(Connection &conn, int status);
		void queueSessionResponse(Connection &conn);
		void finishClientResponse(Connection &conn);
		void sweepExpiredConnections();
		void sweepCgiJobs(time_t now);
		void reapDetachedCgiJobs(time_t now);
		void finishCgiResponse(Connection &conn);
		void failCgiJob(Connection &conn);
		void releaseCgiJob(Connection &conn);
		void closeManagedFd(int &fd);
		void closeConnection(Connection &conn);

		Worker(const Worker &other);
};

#endif
