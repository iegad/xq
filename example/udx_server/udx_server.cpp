#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif

#include "xq/net/rux_server.hpp"


int 
main(int argc, char** argv) {
    ASSERT(!rux_env_init());
    xq::net::RuxServer server;
    server.run("0.0.0.0", "6688");
    server.wait();
    ASSERT(!rux_env_release());
    return 0;
}
