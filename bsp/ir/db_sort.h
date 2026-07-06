// bsp/ir/db_sort.h — directory-listing types + ordering (pure logic).
#ifndef DB_SORT_H
#define DB_SORT_H
#include <stdint.h>
#include <stdbool.h>

#define DB_MAX_ENTRIES 200u
#define DB_NAME_MAX    64u    // FatFs LFN names longer than this are dropped
                              // (truncating would break reopening the path)

typedef struct { char name[DB_NAME_MAX]; bool is_dir; } db_entry_t;
typedef struct {
    db_entry_t e[DB_MAX_ENTRIES];
    uint32_t count;
    uint32_t dropped;         // over-cap or over-length entries not listed
} db_listing_t;

int  db_ci_cmp(const char *a, const char *b);     // case-insensitive strcmp
int  db_entry_cmp(const void *a, const void *b);  // dirs first, then name (ci)
void db_listing_sort(db_listing_t *l);
bool db_name_is_ir(const char *name);             // "*.ir", case-insensitive
#endif
