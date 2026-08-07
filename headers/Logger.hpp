#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <ostream>
#include <string>
#include "types.hpp"

// Access log (one line per completed request) and error/diagnostic log.
class Logger {
    public:
        Logger(std::ostream& access_log, std::ostream& error_log, int level);
        ~Logger();

        void error(const std::string& msg, int level);
        void access(const Connection& conn);

    private:
        std::ostream&  access_log;
        std::ostream&  error_log;
        int            level;

        Logger(const Logger& other);
        Logger& operator=(const Logger& other);
};

#endif
