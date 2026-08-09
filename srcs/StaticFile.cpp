#include "StaticFile.hpp"

StaticFile::StaticFile() {}

StaticFile::~StaticFile() {}

Content StaticFile::serve(const Route& route, const Request& request) {
    (void)route;
    (void)request;
    return Content();
}
