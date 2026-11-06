/* Pause completion latency follows whether the drive is streaming sectors,
 * not the READ status bit. Seek completion sets READ (PSX-CD-003) on an idle
 * drive; a Pause there must ack quickly, as DuckStation's GetTicksForPause
 * does when not reading. PsyQ CdRead starts with Pause, so a loader that
 * reads from its SeekP-complete callback otherwise waits ~3.7 frames inside
 * the CD IRQ handler (Armored Core's menu music stretched). */
#define main fifo_baseline_main
#include "test_cdrom_sector_fifo.c"
#undef main

#define PAUSE_IDLE_CYCLES 5000

static int pause_latency(void) {
    response_clear(); ack();
    exec_command(0x09);
    CHECK(irq_flag == CDIRQ_ACK);
    CHECK(pending.pending && pending.cmd == 0x09);
    int lat = pending_rem_cycles();
    response_clear(); ack();
    psx_cycle_count += 100000000;
    process_pending(0);
    CHECK(!pending.pending && irq_flag == CDIRQ_COMPLETE);
    response_clear(); ack();
    return lat;
}

static void seek_to(int distant) {
    param_fifo[0] = distant ? 0x22 : 0x00;
    param_fifo[1] = distant ? 0x40 : 0x03;
    param_fifo[2] = distant ? 0x17 : 0x27;
    param_count = 3;
    exec_command(0x02);
    response_clear(); ack();
    exec_command(0x16);
    CHECK(irq_flag == CDIRQ_ACK && (stat_reg & CDSTAT_SEEK));
    response_clear(); ack();
    psx_cycle_count += 100000000;
    process_pending(0);
    CHECK(!pending.pending && irq_flag == CDIRQ_COMPLETE);
    response_clear(); ack();
}

static void pause_after_seek_is_fast(int distant) {
    setup();
    reading = 0;
    seek_to(distant);
    /* PSX-CD-003 still holds: READ is reported after the seek. */
    CHECK((stat_reg & CDSTAT_READ) && !(stat_reg & CDSTAT_SEEK));
    CHECK(!reading);
    CHECK(pause_latency() == PAUSE_IDLE_CYCLES);
    CHECK(!(stat_reg & CDSTAT_READ));
}

static void pause_while_reading_settles(void) {
    setup();
    stat_reg |= CDSTAT_READ;
    sector();
    sector();
    CHECK(reading);
    /* Beetle Command_Pause: >= 1124584 cycles at double speed while reading. */
    CHECK(pause_latency() >= 1124584);
    CHECK(!reading);
}

static void pause_when_paused_is_fast(void) {
    setup();
    sector();
    (void)pause_latency();
    CHECK(pause_latency() == PAUSE_IDLE_CYCLES);
}

static void read_after_seek_then_pause_settles(void) {
    /* The fast path must not leak into a real read started after the seek. */
    setup();
    reading = 0;
    seek_to(1);
    exec_command(0x06);
    response_clear(); ack();
    CHECK(reading);
    sector();
    CHECK(pause_latency() >= 1124584);
}

int main(void) {
    pause_after_seek_is_fast(0);
    pause_after_seek_is_fast(1);
    pause_while_reading_settles();
    pause_when_paused_is_fast();
    read_after_seek_then_pause_settles();
    puts("PASS: Pause latency after seek, while reading, and when paused");
    return 0;
}
