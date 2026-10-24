#include "Cgi.hpp"
#include "utils.hpp"
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

Cgi::Cgi() {}

Cgi::~Cgi() {}

static std::string toString(size_t value) {
	std::ostringstream out;
	out << value;
	return out.str();
}

static std::vector<std::string> buildEnv(const Request &request, const Route &route,
	const ServerConfig &server, const std::string &remoteAddr, const std::string &scriptFile) {
	std::vector<std::string> env;
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_PROTOCOL=" + request.version);
	env.push_back("SERVER_NAME=" + server.server_name);
	env.push_back("SERVER_PORT=" + toString(server.port));
	env.push_back("REQUEST_METHOD=" + request.method);
	std::string uri = request.path;
	if (!request.query.empty())
		uri += "?" + request.query;
	env.push_back("REQUEST_URI=" + uri);
	env.push_back("SCRIPT_NAME=" + route.cgi_script_name);
	env.push_back("SCRIPT_FILENAME=" + scriptFile);
	std::string pathInfo = request.path.substr(route.cgi_script_name.size());
	if (pathInfo.empty())
		pathInfo = request.path; // cgi_tester refuses an empty PATH_INFO
	env.push_back("PATH_INFO=" + pathInfo);
	env.push_back("QUERY_STRING=" + request.query);
	env.push_back("REMOTE_ADDR=" + remoteAddr);
	env.push_back("REDIRECT_STATUS=200");
	env.push_back("CONTENT_LENGTH=" + toString(request.body.size()));

	for (std::map<std::string, std::string>::const_iterator it = request.headers.begin(); it != request.headers.end(); ++it) {
		std::string key = "HTTP_" + it->first;
		if (it->first == "content-type")
			key = "CONTENT_TYPE";
		for (std::string::size_type i = 0; i < key.size(); ++i) {
			if (key[i] == '-')
				key[i] = '_';
			else
				key[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[i])));
		}
		env.push_back(key + "=" + it->second);
	}
	return env;
}

static void execScript(const Request &request, const Route &route, const ServerConfig &server,
	const std::string &remoteAddr, const std::vector<int> &serverFds, int inPipe[2], int outPipe[2]) {
	if (dup2(inPipe[0], STDIN_FILENO) < 0 || dup2(outPipe[1], STDOUT_FILENO) < 0)
		std::exit(1);
	close(inPipe[0]);
	close(inPipe[1]);
	close(outPipe[0]);
	close(outPipe[1]);
	for (std::vector<int>::const_iterator fd = serverFds.begin(); fd != serverFds.end(); ++fd)
		close(*fd);

	const std::string &script = route.cgi_script_path;
	std::string::size_type slash = script.find_last_of('/');
	if (chdir(script.substr(0, slash).c_str()) != 0)
		std::exit(1);
	std::string scriptFile = script.substr(slash + 1);

	std::vector<std::string> envStrings = buildEnv(request, route, server, remoteAddr, scriptFile);
	std::vector<char*> envp;
	for (size_t i = 0; i < envStrings.size(); ++i)
		envp.push_back(const_cast<char*>(envStrings[i].c_str()));
	envp.push_back(NULL);
	char *argv[] = { const_cast<char*>(route.cgi_handler.c_str()), const_cast<char*>(scriptFile.c_str()), NULL };

	execve(argv[0], argv, &envp[0]);
	std::exit(1);
}

CgiJob Cgi::start(const Request &request, const Route &route, const ServerConfig &server,
	const std::string &remoteAddr, const std::vector<int> &serverFds) {
	CgiJob job;
	job.failed = true;

	struct stat info;
	if (stat(route.cgi_script_path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
		return job;
	int inPipe[2];
	int outPipe[2];
	if (pipe(inPipe) < 0)
		return job;
	if (pipe(outPipe) < 0) {
		close(inPipe[0]);
		close(inPipe[1]);
		return job;
	}

	pid_t pid = fork();
	if (pid == 0)
		execScript(request, route, server, remoteAddr, serverFds, inPipe, outPipe);
	close(inPipe[0]);
	close(outPipe[1]);
	if (pid < 0 || fcntl(inPipe[1], F_SETFL, O_NONBLOCK) < 0 || fcntl(outPipe[0], F_SETFL, O_NONBLOCK) < 0) {
		close(inPipe[1]);
		close(outPipe[0]);
		job.pid = pid;
		stop(job);
		return job;
	}
	job.failed = false;
	job.pid = pid;
	job.started_at = time(NULL);
	job.last_io = job.started_at;
	job.in_fd = inPipe[1];
	job.out_fd = outPipe[0];
	return job;
}

bool Cgi::collect(CgiJob &job) {
	char buf[65536];
	ssize_t n = read(job.out_fd, buf, sizeof(buf));
	if (n > 0 && job.output.size() + static_cast<size_t>(n) <= MAX_OUTPUT_BYTES) {
		job.output.append(buf, static_cast<size_t>(n));
		job.last_io = time(NULL);
		return false;
	}
	if (n != 0)
		job.failed = true;
	return true;
}

bool Cgi::sendBody(CgiJob &job, const std::string &body) {
	if (job.sent >= body.size())
		return true;
	ssize_t n = write(job.in_fd, body.data() + job.sent, body.size() - job.sent);
	if (n <= 0)
		return true;
	job.sent += static_cast<size_t>(n);
	job.last_io = time(NULL);
	return job.sent == body.size();
}

void Cgi::stop(CgiJob &job) const {
	if (job.pid <= 0)
		return;
	if (waitpid(job.pid, NULL, WNOHANG) == 0) {
		kill(job.pid, SIGKILL);
		waitpid(job.pid, NULL, 0);
	}
	job.pid = -1;
}

// Parses the CGI header block into response; the body is moved out of job.output.
// A failed or malformed run leaves response as a bodiless 502.
void Cgi::buildResponse(CgiJob &job, Response &response) const {
	response = Response();
	response.status = 502;
	if (job.failed)
		return;

	Response parsed;
	parsed.status = 200;
	std::string::size_type pos = 0;
	while (true) {
		std::string::size_type end = job.output.find('\n', pos);
		if (end == std::string::npos)
			return;
		std::string line = job.output.substr(pos, end - pos);
		pos = end + 1;
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty())
			break;

		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos || colon == 0)
			return;
		const std::string name = lowerCase(line.substr(0, colon));
		const std::string value = trim(line.substr(colon + 1));
		if (name == "status")
			parsed.status = std::atoi(value.c_str());
		else if (name == "content-type")
			parsed.headers["Content-Type"] = value;
		else if (name == "location")
			parsed.headers["Location"] = value;
		else if (name != "content-length" && name != "transfer-encoding" && name != "connection")
			parsed.headers[line.substr(0, colon)] = value;
	}
	if (parsed.headers.count("Location") && parsed.status == 200)
		parsed.status = 302;
	if (!parsed.headers.count("Content-Type") && !parsed.headers.count("Location"))
		return;
	job.output.erase(0, pos);
	response.status = parsed.status;
	response.headers.swap(parsed.headers);
	response.body.swap(job.output);
}
