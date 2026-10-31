/* Called only after native GetData has released all guest memory pointers.
 * Preserve its exact result; a bounded pending FLUSH wait gives GPU workers
 * and other guest threads time without repeatedly crossing the x86 ABI. */
static int dxvk_query_wait_config(void)
{
    static int configured = -1;
    if (configured < 0) {
        configured = 0;
        const char *value = getenv("UT3_DXVK_QUERY_WAIT_US");
        if (value && *value) {
            char *end;
            errno = 0;
            unsigned long parsed = strtoul(value, &end, 10);
            if (!errno && !*end && parsed <= 1000) configured = (int)parsed;
        }
        void_log("[dxvk] Pending FLUSH query cooperative wait: %d us after 8 polls", configured);
    }
    return configured;
}

static void dxvk_query_wait(uint32_t result, uint32_t flags)
{
    static _Thread_local unsigned pending;
    /* S_FALSE is 1; D3DGETDATA_FLUSH is bit 0. Flags and all results are
     * forwarded to DXVK unchanged. Nonblocking probes never sleep. */
    if (result != 1u || !(flags & 1u)) { pending = 0; return; }
    if (pending < 8) { ++pending; return; }
    int microseconds = dxvk_query_wait_config();
    /* Zero avoids a timed sleep; the FIFO hook hands over only if contended. */
    if (!microseconds) { if (recomp_yield_hook) recomp_yield_hook(); return; }
    struct timespec delay = { 0, (long)microseconds * 1000L };
    void_blocking_begin();
    while (nanosleep(&delay, &delay) && errno == EINTR) {}
    void_blocking_end();
}
