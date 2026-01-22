#include <iostream>
#include "utils/httplib.h"
#include "routes/register_routes.h"
#include "services/engine_client.h"

int main() {
    httplib::Server server;
    EngineClient engine;

    register_routes(server, engine);

    std::cout << "CarCal API running on http://127.0.0.1:8080\n";
    server.listen("0.0.0.0", 8080);
}

