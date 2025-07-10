#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "city.h"

#define MAX_TICKS 10000
#define TICK_USEC 20000

int main() {
    City city;

    city_init(&city);

    long last_sleep = 0;
    struct timeval start, end;
    for (int tick = 0; tick < MAX_TICKS; ++tick) {
        printf("\033[H\033[J");

        int day = tick / TICKS_PER_DAY + 1;
        int day_tick = tick - (day - 1) * TICKS_PER_DAY;

        printf("Day %d %d Tick %d Last Sleep %ld, Last Moved %d/%d %d%%\n", day, day_tick, tick, last_sleep, city.num_moved, city.num_wanted_move, (city.num_wanted_move == 0 ? 100 : (city.num_moved * 100 / city.num_wanted_move)));
        city_draw(&city);

        gettimeofday(&start, NULL);
        city_update(&city, tick);
        gettimeofday(&end, NULL);

        long elapsed = (end.tv_sec - start.tv_sec) * 1000000L
                     + (end.tv_usec - start.tv_usec);
        long sleep_for = TICK_USEC - elapsed;
        last_sleep = sleep_for;
        if (sleep_for > 0)
            usleep(sleep_for);
    }
    return 0;
}
