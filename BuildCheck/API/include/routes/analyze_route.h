#pragma once
#include "utils/httplib.h"
#include "services/engine_client.h"

void register_analyze_route(httplib::Server& server, const EngineClient& engine);
