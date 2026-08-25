/* bench_rtc.c -- is there a wall clock on this device, is it running, and what epoch is it on?
 *
 * WHY THIS EXISTS. The question "does the calculator know night from day" has a real answer in the
 * SDK: vendor/Ndless/ndless-sdk/libsyscalls/stdlib.cpp:514 implements gettimeofday() as a straight
 * read of a 32-bit register at 0x90090000 into tv_sec, and nspire-io reads the same address for its
 * cursor blink. So the hardware has a seconds counter. What the SDK does NOT establish is
 *   (a) whether it is RUNNING on this unit,
 *   (b) what EPOCH its zero means, and
 *   (c) whether it ticks at exactly 1 Hz.
 * The SDK assumes Unix epoch by assigning straight to tv_sec. That is an assumption in someone
 * else's code, not a measurement on this device, and a day/night feature built on it would be a
 * guess wearing a number. Hence this probe.
 *
 * WHAT IT DOES NOT VERIFY: whether the value survives a battery pull, and whether the Nspire OS
 * clock setting writes this same register. Both need operator action -- see the RUN note below.
 *
 * RUN NOTE, for the operator:
 *   Write down the real wall-clock time to the minute BEFORE pressing a key, and again at the end.
 *   The probe cannot know the true time; it prints its raw counter and three candidate readings so
 *   you can see which epoch, if any, lands on the time you wrote down.
 */
#include "common.h"
#include <time.h>

#define RTC_ADDR 0x90090000u

/* Gregorian civil-from-days, Howard Hinnant's algorithm. newlib's gmtime is available, but this
 * prints all three candidate epochs from one place and cannot be surprised by a libc that decides
 * time_t is 64-bit or that localtime needs a timezone database that is not on the device. */
static void civil(long long z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned long long doe = (unsigned long long)(z - era * 146097);
    unsigned long long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    long long yy = (long long)yoe + era * 400;
    unsigned long long doy = doe - (365*yoe + yoe/4 - yoe/100);
    unsigned long long mp = (5*doy + 2)/153;
    *d = (unsigned)(doy - (153*mp+2)/5 + 1);
    *m = (unsigned)(mp < 10 ? mp+3 : mp-9);
    *y = (int)(yy + (*m <= 2));
}
static void stamp(char *out, int cap, unsigned long long secs, const char *label) {
    long long days = (long long)(secs / 86400ULL);
    unsigned rem  = (unsigned)(secs % 86400ULL);
    int y; unsigned m, d;
    civil(days, &y, &m, &d);
    snprintf(out, (size_t)cap, "%s %04d-%02u-%02u %02u:%02u:%02u UTC",
             label, y, m, d, rem/3600u, (rem%3600u)/60u, rem%60u);
}

int main(void) {
    bench_open("bench_rtc");

    uint32_t r0 = MMIO32(RTC_ADDR);
    bench_result("rtc_raw_first", "%lu (0x%08lX)", (unsigned long)r0, (unsigned long)r0);

    if (r0 == 0 || r0 == 0xFFFFFFFFu) {
        bench_result("rtc_running", "NO -- register reads %s, treat as absent",
                     r0 ? "all ones" : "zero");
        bench_close();
        return 0;
    }

    /* Is it ticking, and at what rate? Gate against the 32.768 kHz timer, which bench_platform
     * established as the crystal-derived cross-check -- NOT against msleep, whose accuracy is
     * itself unmeasured. Five seconds is long enough to distinguish 1 Hz from 2 Hz or 0.5 Hz. */
    bench_timer_t tm;
    timer_acquire(&tm, TIMER_32K_BASE);
    uint32_t t0 = timer_raw(TIMER_32K_BASE);
    uint32_t a = MMIO32(RTC_ADDR);
    while (timer_delta(t0, timer_raw(TIMER_32K_BASE)) < 5u * 32768u) { }
    uint32_t b = MMIO32(RTC_ADDR);
    uint32_t ticks = timer_delta(t0, timer_raw(TIMER_32K_BASE));
    timer_release(&tm);

    /* A stopped clock reads a constant, which is indistinguishable from "nothing happened yet"
     * unless the gate itself is checked. bench_platform hit exactly this and reported a rate from
     * a dead timer. */
    if (ticks == 0) {
        bench_result("rtc_rate_hz", "%s", "UNMEASURABLE -- the 32 kHz gate did not advance");
        bench_close();
        return 0;
    }

    unsigned long delta = (unsigned long)(b - a);
    bench_result("rtc_delta_over_5s", "%lu ticks in %lu 32k-counts (%.3f s)",
                 delta, (unsigned long)ticks, (double)ticks / 32768.0);
    bench_result("rtc_rate_hz", "%.4f", (double)delta / ((double)ticks / 32768.0));
    bench_result("rtc_running", "%s", delta ? "YES" : "NO -- value did not change in 5 s");

    /* Which epoch? Print all three and let the operator match against the clock they wrote down.
     * Guessing here is exactly the failure this file exists to avoid. */
    char line[128];
    stamp(line, sizeof line, (unsigned long long)b, "as-unix-1970:");
    bench_result("rtc_epoch_candidate_1", "%s", line);
    stamp(line, sizeof line, (unsigned long long)b + 946684800ULL, "as-2000-epoch:");
    bench_result("rtc_epoch_candidate_2", "%s", line);
    stamp(line, sizeof line, (unsigned long long)b - 2208988800ULL, "as-ntp-1900: ");
    bench_result("rtc_epoch_candidate_3", "%s", line);

    /* What the SDK's own gettimeofday() returns, so the two paths can be compared rather than
     * assumed equal. */
    bench_result("rtc_note", "%s",
                 "SDK gettimeofday() assigns this register straight to tv_sec (Unix). "
                 "If candidate_1 matches your watch, that assumption holds on this unit.");
    bench_result("rtc_todo", "%s",
                 "STILL UNVERIFIED: battery-pull persistence, and whether the OS date setting "
                 "writes this register. Set the clock in the OS, re-run, and compare.");

    bench_close();
    return 0;
}
