// city.c
#include "city.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <assert.h>
#include <limits.h>

#define NUM_POPS 5120
#define RAND_SEED 12345

const int TOTAL_PLACE_CAPACITY[] = {
    NUM_POPS,
    NUM_POPS,
    NUM_POPS / 4,
    NUM_POPS / 4,
};

const int CAPACITY_PER_PLACE[] = {
    4,
    NUM_POPS / 2 / 2 / 2,
    NUM_POPS / 4 / 4,
    NUM_POPS / 4 / 4,
};

// 4‐way offsets
static const int dx[4] = {  1, -1,  0,  0 };
static const int dy[4] = {  0,  0,  1, -1 };

#define TICK_HOUR(tick) ((tick / TICKS_PER_HOUR) % 24)

const char PLACE_CHARS[] = { 'H', 'J', 'P', 'D' };

static bool can_add_place(City *city, int cx, int cy);

static Place* find_reservation(City *city, NeedType need);
static void reserve(Place *place, NeedType need);
static void unreserve(Place *place, NeedType need);

static void enter(City *city, Pop *pop, Place *place, NeedType need);
static void leave(City *city, Pop *pop, NeedType need);

static void update_pop(City *city, Pop *pop, int tick);
static void move_pops(City *city);
static void handle_pop_need(City *city, Pop *pop, NeedType need);

// Returns array of Coords, end value has x & y of -1
static Coord* calculate_path(City *city, int x1, int y1, int x2, int y2);


void city_init(City *city) {
    srand(RAND_SEED);

    memset(city, 0, sizeof(City));

    for (int x = 0; x < CITY_WIDTH; ++x) {
        for (int y = 0; y < CITY_HEIGHT; ++y) {
            city->tiles[x][y].capacity = 1;
        }
    }

    // Create places
    printf("Creating places.\n");
    for (PlaceType place_type = 0; place_type < PLACE_COUNT; ++place_type) {
        int capacity_remaining = TOTAL_PLACE_CAPACITY[place_type];
        // int num = NUM_PLACES[place_type];

        while (capacity_remaining > 0) {
            int capacity = CAPACITY_PER_PLACE[place_type];
            if (capacity > capacity_remaining) capacity = capacity_remaining;

            int x, y;
            do {
                x = rand() % CITY_WIDTH;
                y = rand() % CITY_HEIGHT;
            } while (!can_add_place(city, x, y));

            Place *place = malloc(sizeof(Place));
            *place = (Place){
                .type = place_type,
                .x = x,
                .y = y,
                .capacity = capacity,
                .next_in_city = city->places,
            };
            city->places = place;
            city->tiles[x][y].place = place;
            city->tiles[x][y].capacity = INT_MAX;

            place->needs_capacity[NEED_FOOD] = place_type == PLACE_DINER ? capacity : 0;
            place->needs_capacity[NEED_WORK] = place_type == PLACE_JOB ? capacity : 0;
            place->needs_capacity[NEED_PLAY] = place_type == PLACE_PARK ? capacity : 0;
            place->needs_capacity[NEED_SLEEP] = place_type == PLACE_HOME ? capacity : 0;
            assert(NEED_COUNT == 4); // try to detect need enums changing

            capacity_remaining -= capacity;
        }
    }

    // Create pops
    printf("Creating pops.\n");
    for (int i = 0; i < NUM_POPS; ++i) {
        Pop *pop = malloc(sizeof(Pop));

        *pop = (Pop) {
            .id = i,
            .satisfying_need = NEED_NONE,
            .move_for_need = NEED_NONE,
            .next_in_city = city->pops,
        };

        city->pops = pop;
    }

    // Find jobs and homes for pops
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        // Find homes for pops
        pop->home = find_reservation(city, NEED_SLEEP);
        assert(pop->home != NULL);
        reserve(pop->home, NEED_SLEEP);

        // Find jobs for pops
        pop->job = find_reservation(city, NEED_WORK);
        assert(pop->job != NULL);
        reserve(pop->job, NEED_WORK);
    }

    // We reserved home/job, we need to unreserve them.
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        unreserve(pop->job, NEED_WORK);
        unreserve(pop->home, NEED_SLEEP);
    }

    // Place pops in the map
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        pop->x = pop->home->x;
        pop->y = pop->home->y;
        ++city->tiles[pop->x][pop->y].used;
    }
}

static bool can_add_place(City *city, int cx, int cy) {
    // Make sure it isn't already occupied
    if (city->tiles[cx][cy].place)
        return false;

    // Ensure neighbors aren't having their only entrance blocked
    for (int i = 0; i < 4; i++) {
        int px = cx + dx[i], py = cy + dy[i];
        if (px < 0 || px >= CITY_WIDTH || py < 0 || py >= CITY_HEIGHT)
            continue;
        Place *p = city->tiles[px][py].place;
        if (!p)
            continue;

        int free_nei = 0;
        for (int j = 0; j < 4; j++) {
            int nx = p->x + dx[j], ny = p->y + dy[j];
            if (nx < 0 || nx >= CITY_WIDTH || ny < 0 || ny >= CITY_HEIGHT)
                continue;
            if (nx == cx && ny == cy)
                continue;   // skip our candidate
            if (!city->tiles[nx][ny].place)
                free_nei++;
        }
        if (free_nei == 0)
            return false;
    }

    // Make sure all free tiles would still be reachable (flood fill)
    bool seen[CITY_WIDTH][CITY_HEIGHT];
    memset(seen, 0, sizeof(seen));

    int start_x = -1, start_y = -1, total_free = 0;
    for (int x = 0; x < CITY_WIDTH; x++) {
        for (int y = 0; y < CITY_HEIGHT; y++) {
            if (x == cx && y == cy)
                continue;
            if (!city->tiles[x][y].place) {
                total_free++;
                if (start_x < 0) {
                    start_x = x; start_y = y;
                }
            }
        }
    }
    if (start_x < 0)
        return true;  // no other free tiles

    Coord queue[CITY_WIDTH * CITY_HEIGHT];
    int qh = 0, qt = 0;
    queue[qt++] = (Coord){ start_x, start_y };
    seen[start_x][start_y] = true;
    int reached = 1;

    while (qh < qt) {
        Coord c = queue[qh++];
        for (int i = 0; i < 4; i++) {
            int nx = c.x + dx[i], ny = c.y + dy[i];
            if (nx < 0 || nx >= CITY_WIDTH || ny < 0 || ny >= CITY_HEIGHT)
                continue;
            if (seen[nx][ny])
                continue;
            if (nx == cx && ny == cy)
                continue;
            if (city->tiles[nx][ny].place)
                continue;
            seen[nx][ny] = true;
            reached++;
            queue[qt++] = (Coord){ nx, ny };
        }
    }

    return (reached == total_free);
}

static void reserve(Place *place, NeedType need) {
    assert(place->capacity > place->reserved + place->used);
    assert(place->needs_capacity[need] > place->needs_reserved[need] + place->needs_used[need]);
    ++place->reserved;
    ++place->needs_reserved[need];
}

static void unreserve(Place *place, NeedType need) {
    assert(place->reserved > 0);
    assert(place->needs_capacity[need] > 0);
    --place->reserved;
    --place->needs_reserved[need];
}

static void enter(City *city, Pop *pop, Place *place, NeedType need) {
    assert(pop->in_place == NULL);

    --city->tiles[pop->x][pop->y].used;
    pop->x = place->x;
    pop->y = place->y;
    pop->satisfying_need = need;

    --place->reserved;
    --place->needs_reserved[need];
    ++place->used;
    ++place->needs_used[need];

    pop->in_place = place;
    pop->next_in_place = place->pops;
    place->pops = pop;
}

static void leave(City *city, Pop *pop, NeedType need) {
    assert(pop->in_place != NULL);

    Place *place = pop->in_place;

    bool removed_pop = false;
    for (Pop** pp = &place->pops; *pp; pp = &(*pp)->next_in_place) {
        if (*pp == pop) {
            *pp = pop->next_in_place;
            pop->next_in_place = NULL;
            removed_pop = true;
            break;
        }
    }
    assert(removed_pop);

    --place->used;
    --place->needs_used[need];

    assert(place->used >= 0);
    assert(place->needs_used[need] >= 0);

    pop->satisfying_need = NEED_NONE;
    pop->in_place = NULL;
    pop->x = place->x;
    pop->y = place->y;
    ++city->tiles[pop->x][pop->y].used;
}

void city_draw(City *city) {
    for (int y = 0; y < CITY_HEIGHT; ++y) {
        for (int x = 0; x < CITY_WIDTH; ++x) {
            char c = ' ';
            if (city->tiles[x][y].place)
                c = PLACE_CHARS[city->tiles[x][y].place->type];
            else if (city->tiles[x][y].used > 0)
                c = '.';
            putchar(c);
        }
        putchar('\n');
    }
}

void city_update(City *city, int tick) {
    for (Pop *p = city->pops; p != NULL; p = p->next_in_city) {
        update_pop(city, p, tick);
    }

    move_pops(city);
}

static void update_pop(City *city, Pop *pop, int tick) {
    assert(NEED_COUNT == 4);
    ++pop->metrics.ticks_total;

    // Handle need generation
    if (tick % TICKS_PER_HOUR == 0) {
        switch (TICK_HOUR(tick)) {
            case 7:
                pop->ticks_needed[NEED_WORK] += 8 * TICKS_PER_HOUR;
                break;
            case 12:
                pop->ticks_needed[NEED_FOOD] += TICKS_PER_HOUR / 2;
                break;
            case 18:
                pop->ticks_needed[NEED_PLAY] += TICKS_PER_HOUR * 2;
                break;
            case 22:
                // bed time, so we reset our needs and track any excess
                for (NeedType need = 0; need < NEED_COUNT; ++need) {
                    pop->metrics.ticks_needs_unmet[need] += pop->ticks_needed[need];
                    pop->ticks_needed[need] = 0;
                }

                pop->ticks_needed[NEED_SLEEP] += 8 * TICKS_PER_HOUR;
                break;
        }
    }

    // Sleep!
    if (pop->ticks_needed[NEED_SLEEP] > 0) {
        handle_pop_need(city, pop, NEED_SLEEP);
        return;
    }

    // Eat!
    if (pop->ticks_needed[NEED_FOOD] > 0) {
        handle_pop_need(city, pop, NEED_FOOD);
        return;
    }

    // Work!
    if (pop->ticks_needed[NEED_WORK] > 0) {
        handle_pop_need(city, pop, NEED_WORK);
        return;
    }

    // Play!
    if (pop->ticks_needed[NEED_PLAY] > 0) {
        handle_pop_need(city, pop, NEED_PLAY);
        return;
    }

    // ...nothing to do, idle pop!
    ++pop->metrics.ticks_idle;
}

static void handle_pop_need(City *city, Pop *pop, NeedType need) {
    if (pop->satisfying_need == need) {
        --pop->ticks_needed[need];
        ++pop->metrics.ticks_needs[need];
        if (pop->ticks_needed[need] == 0) {
            leave(city, pop, need);
        }
    } else {
        if (pop->move_for_need != need) {
            if (pop->in_place) {
                leave(city, pop, pop->satisfying_need);
            }
            if (pop->move_to_place) {
                unreserve(pop->move_to_place, pop->move_for_need);
                pop->move_to_place = NULL;
                pop->move_for_need = NEED_NONE;

                if (pop->move_path) {
                    free(pop->move_path);
                    pop->move_path = NULL;
                    pop->move_path_idx = 0;
                }
            }

            // Kinda a hack here.
            Place *place = need == NEED_WORK ? pop->job
                : need == NEED_SLEEP ? pop->home
                : find_reservation(city, need);
            if (!place) {
                ++pop->metrics.ticks_needs_blocked[need];
                return;
            }

            reserve(place, need);
            pop->move_to_place = place;
            pop->move_for_need = need;
        }
    }
}

static void move_pops(City *city) {
    int next_used[CITY_WIDTH][CITY_HEIGHT];
    memset(next_used, 0, sizeof(next_used));
    bool can_move[NUM_POPS];
    memset(can_move, 0, sizeof(can_move));

    // Claim tiles for people not moving
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (pop->move_to_place) continue;

        ++next_used[pop->x][pop->y];
    }

    // Claim tiles for people wanting to move.
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (!pop->move_to_place) continue;

        if (!pop->move_path) {
            pop->move_path = calculate_path(city, pop->x, pop->y, pop->move_to_place->x, pop->move_to_place->y);
            pop->move_path_idx = 0;
            assert(pop->move_path);
        }

        Coord next = pop->move_path[pop->move_path_idx];
        if (next_used[next.x][next.y] < city->tiles[next.x][next.y].capacity) {
            can_move[pop->id] = true;
            ++next_used[next.x][next.y];
        }
    }

    // Move pops (or don't if they can't)
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (!pop->move_to_place) continue;

        if (!can_move[pop->id]) {
            ++pop->metrics.ticks_move_blocked;
            continue;
        }

        --city->tiles[pop->x][pop->y].used;

        Coord next = pop->move_path[pop->move_path_idx];
        pop->x = next.x;
        pop->y = next.y;
        ++city->tiles[pop->x][pop->y].used;
        ++pop->metrics.ticks_moved;
        ++pop->move_path_idx;

        if (pop->x == pop->move_to_place->x && pop->y == pop->move_to_place->y) {
            assert(pop->move_path[pop->move_path_idx].x == -1);
            assert(pop->move_path[pop->move_path_idx].y == -1);

            enter(city, pop, pop->move_to_place, pop->move_for_need);
            pop->move_to_place = NULL;
            pop->move_for_need = NEED_NONE;

            free(pop->move_path);
            pop->move_path = NULL;
            pop->move_path_idx = 0;
        } else {
            assert(pop->move_path[pop->move_path_idx].x != -1);
            assert(pop->move_path[pop->move_path_idx].y != -1);
        }
    }

    // Sanity check our moves
    for (int x = 0; x < CITY_WIDTH; ++x) {
        for (int y = 0; y < CITY_HEIGHT; ++y) {
            assert(city->tiles[x][y].used <= city->tiles[x][y].capacity);
        }
    }
}


static Place* find_reservation(City *city, NeedType need) {
    for (Place *place = city->places; place; place = place->next_in_city) {
        if (place->needs_capacity[need] > place->needs_reserved[need] + place->needs_used[need]
            && place->capacity > place->reserved + place->used) {
            return place;
        }
    }

    return NULL;
}

//static Coord[] calculate_path(City *city, int x1, int y1, int x2, int y2) {
//}

static inline int in_bounds(int x, int y) {
    return x >= 0 && x < CITY_WIDTH && y >= 0 && y < CITY_HEIGHT;
}

#define MAX_QUEUE (CITY_WIDTH * CITY_HEIGHT)

static Coord* calculate_path(City *city, int x1, int y1, int x2, int y2) {
    if (x1 == x2 && y1 == y2) {
        Coord *path = malloc(sizeof(Coord) * 2);
        path[0] = (Coord){ x2, y2 };
        path[1] = (Coord){ -1, -1 };
        return path;
    }

    int visited[CITY_WIDTH][CITY_HEIGHT] = {0}; // [x][y]
    Coord came_from[CITY_WIDTH][CITY_HEIGHT];   // [x][y]

    Coord queue[MAX_QUEUE];
    int head = 0, tail = 0;

    queue[tail++] = (Coord){ x1, y1 };
    visited[x1][y1] = 1;

    const int dx[4] = { 1, -1, 0, 0 };
    const int dy[4] = { 0, 0, 1, -1 };

    int found = 0;

    while (head < tail) {
        Coord cur = queue[head++];
        for (int d = 0; d < 4; ++d) {
            int nx = cur.x + dx[d];
            int ny = cur.y + dy[d];

            if (nx < 0 || ny < 0 || nx >= CITY_WIDTH || ny >= CITY_HEIGHT)
                continue;
            if (visited[nx][ny])
                continue;

            // Allow destination tile to have a place
            if (city->tiles[nx][ny].place != NULL && !(nx == x2 && ny == y2))
                continue;

            visited[nx][ny] = 1;
            came_from[nx][ny] = (Coord){ cur.x, cur.y };

            if (nx == x2 && ny == y2) {
                found = 1;
                break;
            }

            queue[tail++] = (Coord){ nx, ny };
        }
        if (found) break;
    }

    if (!found) {
        Coord *path = malloc(sizeof(Coord));
        path[0] = (Coord){ -1, -1 };
        return path;
    }

    // Reconstruct path in reverse (excluding start, including destination)
    Coord reverse_path[MAX_QUEUE];
    int length = 0;
    int cx = x2, cy = y2;
    while (!(cx == x1 && cy == y1)) {
        reverse_path[length++] = (Coord){ cx, cy };
        Coord prev = came_from[cx][cy];
        cx = prev.x;
        cy = prev.y;
    }

    // Reverse into final array
    Coord *path = malloc(sizeof(Coord) * (length + 1)); // +1 for sentinel
    for (int i = 0; i < length; ++i)
        path[i] = reverse_path[length - i - 1];
    path[length] = (Coord){ -1, -1 };

    return path;
}
