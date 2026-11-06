/* The VBlank edge fires on schedule while the CPU is inside an exception
 * handler, as on hardware: I_STAT bit 0 latches and missed edges coalesce;
 * delivery still waits for the handler (psx_check_interrupts). Deferring the
 * edge for the whole handler froze presentation, pacing and audio, then
 * fired the missed edges back to back, and pinned the scheduler's VBlank
 * deadline to 0 so devices were stepped one cycle at a time. */
#include "../src/interrupts.c"

/* No guest code runs here; the scheduler and IRQ delivery are not entered. */
void psx_dispatch(CPUState *cpu, uint32_t pc) {
    (void)cpu; (void)pc;
    fprintf(stderr, "FAIL: unexpected psx_dispatch\n");
    exit(1);
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void reset(void) {
    interrupts_init();
    i_stat = 0;
    g_vblank_raise_count = 0;
    cycles_since_vblank = 0;
}

static void edge_fires_inside_handler(void) {
    reset();
    in_exception = 1;
    cycles_since_vblank = g_psx_vblank_cycles + 100u;
    CHECK(interrupts_cycles_to_vblank() == 0);
    interrupts_service_scheduled_events();
    CHECK(g_vblank_raise_count == 1);
    CHECK(i_stat & (1u << IRQ_VBLANK));
    /* Overshoot carries forward; the next deadline is a full period out. */
    CHECK(cycles_since_vblank == 100u);
    CHECK(interrupts_cycles_to_vblank() == g_psx_vblank_cycles - 100u);
    in_exception = 0;
}

static void edges_during_long_handler_coalesce(void) {
    /* A ~4-frame handler (libcd CdSync from a CD callback): each edge fires
     * at its own time; I_STAT holds one latched bit until acknowledged. */
    reset();
    in_exception = 1;
    for (int frame = 1; frame <= 4; frame++) {
        cycles_since_vblank += g_psx_vblank_cycles;
        interrupts_service_scheduled_events();
        CHECK(g_vblank_raise_count == (uint64_t)frame);
        CHECK(interrupts_cycles_to_vblank() > 0);
    }
    CHECK(i_stat == (1u << IRQ_VBLANK));
    in_exception = 0;
}

static void not_due_does_nothing(void) {
    reset();
    in_exception = 1;
    cycles_since_vblank = g_psx_vblank_cycles - 1u;
    interrupts_service_scheduled_events();
    CHECK(g_vblank_raise_count == 0 && !(i_stat & (1u << IRQ_VBLANK)));
    in_exception = 0;
}

int main(void) {
    edge_fires_inside_handler();
    edges_during_long_handler_coalesce();
    not_due_does_nothing();
    puts("PASS: VBlank edge inside exception handlers fires on schedule and latches");
    return 0;
}
