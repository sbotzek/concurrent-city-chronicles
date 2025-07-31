#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "city.h"

#define MAX_TICKS (TICKS_PER_DAY * 2)
#define TICK_USEC 6000

// Prints performance related metrics to the screen.
void display_performance_metrics(long tick_times[MAX_TICKS]);
// Prints city metrics
void display_city_metrics(City *city);
// Formats data in pop_data with avg, p50, p95, etc
const char* format_pop_metrics(long pop_data[NUM_POPS]);
// Used to compare two longs
int cmp_long(const void *a, const void *b);
// Figures out the sim mode from the arguments
SimMode sim_mode(int argc, char **argv);

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

    display_city_metrics(&city);
    display_performance_metrics(tick_times);

    return 0;
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

void display_city_metrics(City *city) {
    printf("Needs Stats\n");
    {
        long ticks_needs[NEED_COUNT][NUM_POPS];

        for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
            for (NeedType need = 0; need < NEED_COUNT; ++need) {
                ticks_needs[need][pop->id] = pop->metrics.ticks_needs[need];
            }
        }

        printf("  Satisfying\n");
        printf("    Sleep: %s\n", format_pop_metrics(ticks_needs[NEED_SLEEP]));
        printf("    Work:  %s\n", format_pop_metrics(ticks_needs[NEED_WORK]));
        printf("    Food:  %s\n", format_pop_metrics(ticks_needs[NEED_FOOD]));
        printf("    Play:  %s\n", format_pop_metrics(ticks_needs[NEED_PLAY]));
    }
    {
        long ticks_needs_blocked[NEED_COUNT][NUM_POPS];

        for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
            for (NeedType need = 0; need < NEED_COUNT; ++need) {
                ticks_needs_blocked[need][pop->id] = pop->metrics.ticks_needs_blocked[need];
            }
        }

        printf("  Blocked\n");
        printf("    Sleep: %s\n", format_pop_metrics(ticks_needs_blocked[NEED_SLEEP]));
        printf("    Work:  %s\n", format_pop_metrics(ticks_needs_blocked[NEED_WORK]));
        printf("    Food:  %s\n", format_pop_metrics(ticks_needs_blocked[NEED_FOOD]));
        printf("    Play:  %s\n", format_pop_metrics(ticks_needs_blocked[NEED_PLAY]));
    }
    {
        long ticks_needs_unmet[NEED_COUNT][NUM_POPS];

        for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
            for (NeedType need = 0; need < NEED_COUNT; ++need) {
                ticks_needs_unmet[need][pop->id] = pop->metrics.ticks_needs_unmet[need];
            }
        }

        printf("  Unmet\n");
        printf("    Sleep: %s\n", format_pop_metrics(ticks_needs_unmet[NEED_SLEEP]));
        printf("    Work:  %s\n", format_pop_metrics(ticks_needs_unmet[NEED_WORK]));
        printf("    Food:  %s\n", format_pop_metrics(ticks_needs_unmet[NEED_FOOD]));
        printf("    Play:  %s\n", format_pop_metrics(ticks_needs_unmet[NEED_PLAY]));
    }
    {
        long ticks_moved[NUM_POPS];
        long ticks_move_blocked[NUM_POPS];

        for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
            ticks_moved[pop->id] = pop->metrics.ticks_moved;
            ticks_move_blocked[pop->id] = pop->metrics.ticks_move_blocked;
        }

        printf("Movement Stats\n");
        printf("  Moved:        %s\n", format_pop_metrics(ticks_moved));
        printf("  Move Blocked: %s\n", format_pop_metrics(ticks_move_blocked));
    }
    {
        long ticks_idle[NUM_POPS];

        for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
            ticks_idle[pop->id] = pop->metrics.ticks_idle;
        }

        printf("Idle: %s\n", format_pop_metrics(ticks_idle));
    }
}

const char* format_pop_metrics(long pop_data[NUM_POPS]) {
    static char buf[1024];

    qsort(pop_data, NUM_POPS, sizeof(long), cmp_long);
    long min = pop_data[0];
    long p50 = pop_data[NUM_POPS * 50 / 100];
    long p95 = pop_data[NUM_POPS * 95 / 100];
    long p99 = pop_data[NUM_POPS * 99 / 100];
    long max = pop_data[NUM_POPS - 1];

    double sum = 0;
    for (int i = 0; i < NUM_POPS; ++i) {
        sum += (double)pop_data[i];
    }
    long avg = sum / NUM_POPS;

    sprintf(buf, "Avg: %ld | P50: %ld | P95: %ld | P99: %ld | Max: %ld, Min: %ld, Total: %f",
        avg, p50, p95, p99, max, min, sum);

    return buf;
}

int cmp_long(const void *a, const void *b) {
    long va = *(const long *)a;
    long vb = *(const long *)b;
    return (va > vb) - (va < vb); // avoids overflow
}
