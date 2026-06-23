#include "loop_closure_config.hpp"

static LoopClosureConfig g_loop_closure_config;

LoopClosureConfig &loopClosureConfig()
{
    return g_loop_closure_config;
}
