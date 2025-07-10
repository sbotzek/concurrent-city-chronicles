#ifndef CITY_H
#define CITY_H

#include <stdbool.h>

#define CITY_HEIGHT 50
#define CITY_WIDTH 200
#define TICKS_PER_HOUR 100
#define TICKS_PER_DAY  (24 * TICKS_PER_HOUR)

typedef struct Pop Pop;
typedef struct Tile Tile;
typedef struct City City;
typedef struct Coord Coord;
typedef struct Place Place;

typedef enum {
    NEED_FOOD,
    NEED_WORK,
    NEED_PLAY,
    NEED_SLEEP,
    NEED_COUNT,
    NEED_NONE
} NeedType;

typedef enum {
    PLACE_HOME,
    PLACE_JOB,
    PLACE_PARK,
    PLACE_DINER,
    PLACE_COUNT
} PlaceType;

struct Coord {
    int x, y;
};

typedef struct {
    int ticks_needs_unmet[NEED_COUNT]; // Remaining unmet at end of day

    int ticks_total; // Total ticks alive

    // These should sum to ticks_total:
    int ticks_needs[NEED_COUNT]; // Time spent satisfying needs
    int ticks_needs_blocked[NEED_COUNT]; // Blocked trying to satisfy needs
    int ticks_moved; // Time spent moving
    int ticks_move_blocked; // Blocked from moving
    int ticks_idle; // Nothing to do
} PopMetrics;


struct Pop {
    int id;
    int x, y;
    Place *in_place;
    Place *move_to_place;
    Coord *move_path;
    int move_path_idx;

    Place *job;
    Place *home;

    NeedType satisfying_need;
    NeedType move_for_need;

    // Ticks remaining to fulfill each need
    int ticks_needed[NEED_COUNT];

    PopMetrics metrics;

    Pop *next_in_city;
    Pop *next_in_place;
};

struct Place {
    PlaceType type;
    int x, y;

    int capacity;
    int reserved;
    int used;
    int idle; // pops can stay idle at a place if the next need isn't ready yet

    int needs_capacity[NEED_COUNT];
    int needs_reserved[NEED_COUNT];
    int needs_used[NEED_COUNT];

    Pop *pops;

    Place *next_in_city;
};

struct Tile {
    Place *place;
    int capacity;
    int used;
};

struct City {
    Tile tiles[CITY_WIDTH][CITY_HEIGHT];
    Place *places;
    Pop *pops;
    int num_moved;
    int num_wanted_move;
};

extern const char PLACE_CHARS[];

void city_init(City *city);
void city_update(City *city, int tick);
void city_draw(City *city);

#endif // CITY_H 
