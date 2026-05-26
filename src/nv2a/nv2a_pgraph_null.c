#include "nv2a_pgraph_d3d11.h"
#include <string.h>

static PgraphD3D11Stats g_stats;

void pgraph_d3d11_init(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}

void pgraph_d3d11_shutdown(void)
{
}

int pgraph_d3d11_method(int subchannel, uint32_t method, uint32_t param)
{
    (void)subchannel;
    (void)method;
    (void)param;
    return 0;
}

void pgraph_d3d11_flush(void)
{
}

void pgraph_d3d11_set_chyron_scroll(uint32_t frame)
{
    (void)frame;
}

void pgraph_d3d11_get_stats(PgraphD3D11Stats *out)
{
    if (out) *out = g_stats;
}
