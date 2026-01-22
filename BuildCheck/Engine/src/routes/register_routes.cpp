#include "utils/httplib.h"

void register_analyze_route(httplib::Server& server);

void register_engine_routes(httplib::Server& server) {
    register_analyze_route(server);
}
