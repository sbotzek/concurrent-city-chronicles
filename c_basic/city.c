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
    50,
    10,
    10
};

static FILE* log_file;

#ifndef NDEBUG
    #define DEBUG_LOG(fmt, ...) \
        fprintf(log_file, fmt, ##__VA_ARGS__)
    #define DEBUG_FLUSH() \
        fflush(log_file)
    #define DEBUG_INDENT(depth) \
        for (int _i = 0; _i < depth; ++_i) \
            fprintf(log_file, " ")
#else
    #define DEBUG_LOG(fmt, ...) \
        do {} while (0)
    #define DEBUG_FLUSH() \
        do {} while (0)
    #define DEBUG_INDENT(depth) \
        do {} while (0)
#endif


// 4‐way offsets
static const int dx[4] = {  1, -1,  0,  0 };
static const int dy[4] = {  0,  0,  1, -1 };

const char PLACE_CHARS[] = { 'H', 'J', 'P', 'D' };

static bool can_add_place(City *city, int cx, int cy);

static bool valid_xy(int x, int y);

static Place* find_reservation(City *city, NeedType need, int by_x, int by_y);
static void reserve(Place *place);
static void unreserve(Place *place);

static void enter(City *city, Pop *pop, Place *place);
static void leave(City *city, Pop *pop);

static void update_pop(City *city, Pop *pop, int tick);
static void move_pops(City *city);
static void claim_recursively(int depth, City *city, Pop *pop, int next_used[CITY_WIDTH][CITY_HEIGHT], bool can_move[NUM_POPS], bool checked[NUM_POPS]);
static void handle_pop_need(City *city, Pop *pop, NeedType need, int tick);

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

    log_file = fopen("log.txt", "w");

    // Create places
    printf("Creating places.\n");
    for (PlaceType place_type = 0; place_type < PLACE_COUNT; ++place_type) {
        int capacity_remaining = TOTAL_PLACE_CAPACITY[place_type];

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

            switch (place_type) {
                case PLACE_JOB: place->satisfies = NEED_WORK; break;
                case PLACE_HOME: place->satisfies = NEED_SLEEP; break;
                case PLACE_DINER: place->satisfies = NEED_FOOD; break;
                case PLACE_PARK: place->satisfies = NEED_PLAY; break;
                case PLACE_COUNT: abort(); break;
            }

            city->places = place;
            city->tiles[x][y].place = place;
            city->tiles[x][y].capacity = INT_MAX;

            capacity_remaining -= capacity;
        }
    }

    // Create pops
    printf("Creating pops.\n");
    for (int i = 0; i < NUM_POPS; ++i) {
        Pop *pop = malloc(sizeof(Pop));

        *pop = (Pop) {
            .id = i,
            .next_in_city = city->pops,
        };

        city->pops = pop;
    }

    // Find jobs and homes for pops
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        // Find homes for pops
        pop->home = find_reservation(city, NEED_SLEEP, rand() % CITY_WIDTH, rand() % CITY_HEIGHT);
        assert(pop->home != NULL);
        reserve(pop->home);

        // Find jobs for pops
        pop->job = find_reservation(city, NEED_WORK, pop->home->x, pop->home->y);
        assert(pop->job != NULL);
        reserve(pop->job);
    }

    // We reserved home/job, we need to unreserve them.
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        unreserve(pop->job);
        unreserve(pop->home);
    }

    // Place pops in the map
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        pop->x = pop->home->x;
        pop->y = pop->home->y;
        pop->next_in_tile = city->tiles[pop->x][pop->y].pops;
        city->tiles[pop->x][pop->y].pops = pop;
        ++city->tiles[pop->x][pop->y].used;
        reserve(pop->home);
        enter(city, pop, pop->home);
    }
}

static bool can_add_place(City *city, int cx, int cy) {
    // Make sure it isn't already occupied
    if (city->tiles[cx][cy].place)
        return false;

    // Ensure neighbors aren't having their only entrance blocked
    for (int i = 0; i < 4; i++) {
        int px = cx + dx[i], py = cy + dy[i];
        if (!valid_xy(px, py))
            continue;
        Place *p = city->tiles[px][py].place;
        if (!p)
            continue;

        int free_nei = 0;
        for (int j = 0; j < 4; j++) {
            int nx = p->x + dx[j], ny = p->y + dy[j];
            if (!valid_xy(nx, ny))
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
            if (!valid_xy(nx, ny))
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

static void reserve(Place *place) {
    assert(place->capacity > place->reserved + place->used);
    ++place->reserved;
}

static void unreserve(Place *place) {
    assert(place->reserved > 0);
    --place->reserved;
}

static void enter(City *city, Pop *pop, Place *place) {
    assert(pop->in_place == NULL);
    assert(pop->x == place->x);
    assert(pop->y == place->y);

    --city->tiles[pop->x][pop->y].used;

    --place->reserved;
    ++place->used;

    pop->in_place = place;
    pop->next_in_place = place->pops;
    place->pops = pop;
}

static void leave(City *city, Pop *pop) {
    assert(pop->in_place != NULL);

    Place *place = pop->in_place;
    assert(pop->x == place->x);
    assert(pop->y == place->y);

    bool removed_pop = false;
    for (Pop** pp = &place->pops; *pp; pp = &(*pp)->next_in_place) {
        if (*pp == pop) {
            *pp = pop->next_in_place;
            pop->next_in_place = NULL;
            removed_pop = true;
            break;
        }
    }
    (void)removed_pop;
    assert(removed_pop);

    --place->used;

    assert(place->used >= 0);

    pop->in_place = NULL;
    ++city->tiles[pop->x][pop->y].used;
}

void city_draw(City *city) {
    char output[CITY_WIDTH+1];

    for (int y = 0; y < CITY_HEIGHT; ++y) {
        for (int x = 0; x < CITY_WIDTH; ++x) {
            char c = ' ';
            if (city->tiles[x][y].place)
                c = PLACE_CHARS[city->tiles[x][y].place->type];
            else if (city->tiles[x][y].used > 0)
                c = '.';
            output[x] = c;
        }

        output[CITY_WIDTH] = '\0';
        puts(output);
    }
}

void city_update(City *city, int tick) {
    DEBUG_LOG("Tick %d: Updating pops\n", tick);
    for (Pop *p = city->pops; p != NULL; p = p->next_in_city) {
        update_pop(city, p, tick);
    }

    DEBUG_LOG("Tick %d: Moving pops\n", tick);
    move_pops(city);
}

static void update_pop(City *city, Pop *pop, int tick) {
    assert(NEED_COUNT == 4);
    ++pop->metrics.ticks_total;

    // Handle need generation
    if (tick == 0) {
        pop->ticks_needed[NEED_SLEEP] += 7 * TICKS_PER_HOUR;
    }
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
                    pop->next_tick_can_find_reservation[need] = 0;
                }

                pop->ticks_needed[NEED_SLEEP] += 8 * TICKS_PER_HOUR;
                break;
        }
    }

    // Sleep!
    if (pop->ticks_needed[NEED_SLEEP] > 0) {
        handle_pop_need(city, pop, NEED_SLEEP, tick);
        return;
    }

    // Eat!
    if (pop->ticks_needed[NEED_FOOD] > 0) {
        handle_pop_need(city, pop, NEED_FOOD, tick);
        return;
    }

    // Work!
    if (pop->ticks_needed[NEED_WORK] > 0) {
        handle_pop_need(city, pop, NEED_WORK, tick);
        return;
    }

    // Play!
    if (pop->ticks_needed[NEED_PLAY] > 0) {
        handle_pop_need(city, pop, NEED_PLAY, tick);
        return;
    }

    // ...nothing to do, idle pop!
    ++pop->metrics.ticks_idle;
}

static void handle_pop_need(City *city, Pop *pop, NeedType need, int tick) {
    if (pop->in_place && pop->in_place->satisfies == need) {
        --pop->ticks_needed[need];
        ++pop->metrics.ticks_needs[need];
        if (pop->ticks_needed[need] == 0) {
            leave(city, pop);
        }
    } else {
        if (!pop->move_to_place || pop->move_to_place->satisfies != need) {
            Place *place;

            switch (need) {
                // Kinda a hack here - NEED_WORK and NEED_SLEEP special cases.
                case NEED_WORK: place = pop->job; break;
                case NEED_SLEEP: place = pop->home; break;
                default:
                    if (tick >= pop->next_tick_can_find_reservation[need]) {
                        place = find_reservation(city, need, pop->x, pop->y);
                        if (!place) {
                            // Don't want to introduce randomness but don't want to use the same value every time, so use (pop->id + tick) to vary
                            pop->next_tick_can_find_reservation[need] = tick + TICKS_PER_HOUR / 3 + (pop->id + tick) % (TICKS_PER_HOUR / 3);
                        }
                    } else {
                        place = NULL;
                    }
            }
            if (!place) {
                if (pop->in_place && pop->ticks_needed[pop->in_place->satisfies] > 0) {
                    handle_pop_need(city, pop, pop->in_place->satisfies, tick);
                } else if (pop->move_to_place && pop->ticks_needed[pop->move_to_place->satisfies] > 0) {
                    handle_pop_need(city, pop, pop->move_to_place->satisfies, tick);
                } else {
                    ++pop->metrics.ticks_needs_blocked[need];
                }
                return;
            }


            if (pop->in_place) {
                leave(city, pop);
            }
            if (pop->move_to_place) {
                unreserve(pop->move_to_place);
                pop->move_to_place = NULL;

                if (pop->move_path) {
                    free(pop->move_path);
                    pop->move_path = NULL;
                    pop->move_path_idx = 0;
                }
            }

            reserve(place);
            // A pop could be idle at the same location as the place
            if (pop->x == place->x && pop->y == place->y) {
                enter(city, pop, place);
                handle_pop_need(city, pop, need, tick);
            } else {
                pop->move_to_place = place;
                pop->move_path = calculate_path(city, pop->x, pop->y, pop->move_to_place->x, pop->move_to_place->y);
                pop->move_path_idx = 0;
                assert(pop->move_path);
            }
        }
    }
}

static void claim_recursively(int depth, City *city, Pop *pop, int next_used[CITY_WIDTH][CITY_HEIGHT], bool can_move[NUM_POPS], bool checked[NUM_POPS]) {
    assert(!checked[pop->id]);
    assert(pop->move_to_place);

    checked[pop->id] = true;

    Coord next = pop->move_path[pop->move_path_idx];
    Tile *next_tile = &city->tiles[next.x][next.y];
    // we don't need to recurse across tiles with places since they have infinite capacity
    if (!next_tile->place) {
        for (Pop* pop2 = next_tile->pops; pop2; pop2 = pop2->next_in_tile) {
            if (!checked[pop2->id]) {
                claim_recursively(depth+1, city, pop2, next_used, can_move, checked);
            }
        }
    }

    if (next_used[next.x][next.y] >= city->tiles[next.x][next.y].capacity) {
        DEBUG_INDENT(depth);
        DEBUG_LOG("claim_recursively: cannot move for pop %d at %d,%d, into %d,%d reserving next_used for %d,%d which is at %d\n", pop->id, pop->x, pop->y, next.x, next.y, pop->x, pop->y, next_used[pop->x][pop->y]);
        DEBUG_FLUSH();
        ++next_used[pop->x][pop->y];
        assert(next_used[pop->x][pop->y] <= city->tiles[pop->x][pop->y].capacity);
        return;
    }

    DEBUG_INDENT(depth);
    DEBUG_LOG("claim_recursively: can move for pop %d at %d,%d, into %d,%d reserving next_used for %d,%d which is at %d\n", pop->id, pop->x, pop->y, next.x, next.y, next.x, next.y, next_used[next.x][next.y]);
    ++next_used[next.x][next.y];
    can_move[pop->id] = true;
}

static void move_pops(City *city) {
    bool checked[NUM_POPS];
    memset(checked, 0, sizeof(checked));
    bool can_move[NUM_POPS];
    memset(can_move, 0, sizeof(can_move));
    int next_used[CITY_WIDTH][CITY_HEIGHT];
    memset(next_used, 0, sizeof(next_used));

    city->num_moved = 0;
    city->num_wanted_move = 0;

    // Make sure each moving pop has a move path.
    //
    // We need to do this now for later steps because we prioritize
    // certain move paths.
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (!pop->move_to_place) continue;

        ++city->num_wanted_move;
    }

    // Let's sanity check all moves we're going to try to make
    #ifndef NDEBUG
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (!pop->move_to_place) continue;

        Coord next = pop->move_path[pop->move_path_idx];

        assert(valid_xy(next.x, next.y));
        assert(next.x != pop->x || next.y != pop->y);
    }
    #endif

    // Claim tiles for unmoving pops
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (pop->move_to_place) continue;

        ++next_used[pop->x][pop->y];
        checked[pop->id] = true;
    }

    // Move pops wanting to swap tiles with each other.
    //
    // This guarantees conflicting congo lines will eventually clear out.
    // Before, without this step, movement could deadlock.
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (!pop->move_to_place) continue;
        if (checked[pop->id]) continue;

        Coord next = pop->move_path[pop->move_path_idx];
        if (city->tiles[next.x][next.y].place) continue; // tiles with places have gigantic capacity

        for (Pop *pop2 = city->tiles[next.x][next.y].pops; pop2; pop2 = pop2->next_in_tile) {
            if (!pop2->move_to_place) continue;
            if (checked[pop2->id]) continue;

            Coord next2 = pop2->move_path[pop2->move_path_idx];
            if (next2.x == pop->x && next2.y == pop->y
                && next.x == pop2->x && next.y == pop2->y) {
                // let the two pops move
                ++next_used[next.x][next.y];
                can_move[pop->id] = true;
                checked[pop->id] = true;
                assert(next_used[next.x][next.y] <= city->tiles[next.x][next.y].capacity);
                DEBUG_LOG("move_pops swap: move for pop %d at %d,%d, into %d,%d reserving next_used for %d,%d which is at %d\n", pop->id, pop->x, pop->y, next.x, next.y, next.x, next.y, next_used[next.x][next.y]);

                ++next_used[next2.x][next2.y];
                can_move[pop2->id] = true;
                checked[pop2->id] = true;
                assert(next_used[next2.x][next2.y] <= city->tiles[next2.x][next2.y].capacity);
                DEBUG_LOG("move_pops swap: move for pop %d at %d,%d, into %d,%d reserving next_used for %d,%d which is at %d\n", pop2->id, pop2->x, pop2->y, next2.x, next2.y, next2.x, next2.y, next_used[next2.x][next2.y]);

                break;
            }
        }
    }

    // Claim tiles for other pops recursively
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (!pop->move_to_place) continue;
        if (checked[pop->id]) continue;

        claim_recursively(0, city, pop, next_used, can_move, checked);
    }


    // Sanity check our moves
    for (int x = 0; x < CITY_WIDTH; ++x) {
        for (int y = 0; y < CITY_HEIGHT; ++y) {
            assert(next_used[x][y] <= city->tiles[x][y].capacity);
        }
    }

    // Handle moves
    for (Pop *pop = city->pops; pop; pop = pop->next_in_city) {
        if (!pop->move_to_place) continue;

        if (can_move[pop->id]) {
            ++city->num_moved;

            Coord next = pop->move_path[pop->move_path_idx];
            bool removed_pop = false;
            for (Pop **pp = &city->tiles[pop->x][pop->y].pops; *pp; pp = &(*pp)->next_in_tile) {
                if (*pp == pop) {
                    *pp = pop->next_in_tile;
                    pop->next_in_tile = NULL;
                    removed_pop = true;
                    break;
                }
            }
            (void)removed_pop;
            assert(removed_pop);

            pop->x = next.x;
            pop->y = next.y;
            pop->next_in_tile = city->tiles[pop->x][pop->y].pops;
            city->tiles[pop->x][pop->y].pops = pop;

            ++pop->metrics.ticks_moved;
            ++pop->move_path_idx;

            if (pop->x == pop->move_to_place->x && pop->y == pop->move_to_place->y) {
                assert(pop->move_path[pop->move_path_idx].x == -1);
                assert(pop->move_path[pop->move_path_idx].y == -1);

                enter(city, pop, pop->move_to_place);
                pop->move_to_place = NULL;

                free(pop->move_path);
                pop->move_path = NULL;
                pop->move_path_idx = 0;
            } else {
                assert(pop->move_path[pop->move_path_idx].x != -1);
                assert(pop->move_path[pop->move_path_idx].y != -1);
            }

        } else {
            ++pop->metrics.ticks_move_blocked;
        }
    }

    // Assign next_used
    for (int x = 0; x < CITY_WIDTH; ++x) {
        for (int y = 0; y < CITY_HEIGHT; ++y) {
            city->tiles[x][y].used = next_used[x][y];
        }
    }
}


static Place* find_reservation(City *city, NeedType need, int by_x, int by_y) {
    int best_distance = INT_MAX;
    Place *best_place = NULL;

    for (Place *place = city->places; place; place = place->next_in_city) {
        if (place->satisfies == need && place->capacity > place->reserved + place->used) {
            int distance = abs(by_x - place->x) + abs(by_y - place->y);
            if (distance < best_distance) {
                best_place = place;
                best_distance = distance;
            }
        }
    }

    return best_place;
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


static bool valid_xy(int x, int y) {
    return x >= 0 && x < CITY_WIDTH
        && y >= 0 && y < CITY_HEIGHT;
}
