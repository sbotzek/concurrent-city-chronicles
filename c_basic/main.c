#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "city.h"

#define MAX_TICKS (TICKS_PER_DAY * 2)
#define TICK_USEC 6000

void display_performance_metrics(long tick_times[MAX_TICKS]);

int cmp_long(const void *a, const void *b) {
    long va = *(const long *)a;
    long vb = *(const long *)b;
    return (va > vb) - (va < vb); // avoids overflow
}

SimMode sim_mode(int argc, char **argv) {
    if (argc == 1) {
        return SIM_MODE_VISUAL;
    }

    if (strcmp(argv[1], "-bm") == 0) {
        return SIM_MODE_BENCHMARK;
    } else if (strcmp(argv[1], "-vm") == 0) {
        return SIM_MODE_VISUAL;
    } else {
        fprintf(stderr, "Invalid arg %s. Must be: -bm or -vm\n", argv[1]);
        exit(1);
    }
}

int main(int argc, char **argv) {
    SimMode mode = sim_mode(argc, argv);
    long tick_times[MAX_TICKS];
    City city;

    city_init(&city);

    struct timeval start, end;
    for (int tick = 0; tick < MAX_TICKS; ++tick) {
        int day = tick / TICKS_PER_DAY + 1;
        int hour = TICK_HOUR(tick);
        int minute = TICK_HOUR_MINUTE(tick);
        long tick_time = tick == 0 ? 0 : tick_times[tick-1];

        if (mode == SIM_MODE_VISUAL) {
            printf("\033[H\033[J");
            printf("Day %d %2d:%02d, Tick %d, Tick Time %ld%s, Last Moved %d/%d %d%%\n", day, hour, minute, tick,
                tick_time >= 1000 ? tick_time / 1000 : tick_time, tick_time >= 1000 ? "ms" : "us",
                city.num_moved, city.num_wanted_move, (city.num_wanted_move == 0 ? 100 : (city.num_moved * 100 / city.num_wanted_move)));
            city_draw(&city);
        } else if (mode == SIM_MODE_BENCHMARK) {
            if (tick % 1000 == 0) {
                printf("Tick %d\n", tick);
            }
        }

        gettimeofday(&start, NULL);
        city_update(&city, tick);
        gettimeofday(&end, NULL);

        long elapsed = (end.tv_sec - start.tv_sec) * 1000000L
                     + (end.tv_usec - start.tv_usec);
        tick_times[tick] = elapsed;
        if (mode == SIM_MODE_VISUAL) {
            long sleep_for = TICK_USEC - elapsed;
            if (sleep_for > 0)
                usleep(sleep_for);
        }
    }

    display_performance_metrics(tick_times);

    return 0;
}

void display_performance_metrics(long tick_times[MAX_TICKS]) {
    qsort(tick_times, MAX_TICKS, sizeof(long), cmp_long);
    long p50 = tick_times[MAX_TICKS * 50 / 100];
    long p95 = tick_times[MAX_TICKS * 95 / 100];
    long p99 = tick_times[MAX_TICKS * 99 / 100];
    long max = tick_times[MAX_TICKS - 1];

    double sum = 0;
    for (int i = 0; i < MAX_TICKS; ++i) {
        sum += (double)tick_times[i];
    }
    long avg = sum / MAX_TICKS;

    printf("Avg: %ld us | P50: %ld | P95: %ld | P99: %ld | Max: %ld, total %fs\n",
        avg, p50, p95, p99, max, sum / 1000 / 60);
}
