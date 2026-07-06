// bsp/ir/db_sort.c
#include "db_sort.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int db_ci_cmp(const char *x, const char *y) {
    while (*x && *y) {
        int cx = tolower((unsigned char)*x), cy = tolower((unsigned char)*y);
        if (cx != cy) return cx - cy;
        x++; y++;
    }
    return (unsigned char)*x - (unsigned char)*y;
}

int db_entry_cmp(const void *pa, const void *pb) {
    const db_entry_t *a = pa, *b = pb;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return db_ci_cmp(a->name, b->name);
}

void db_listing_sort(db_listing_t *l) {
    qsort(l->e, l->count, sizeof l->e[0], db_entry_cmp);
}

bool db_name_is_ir(const char *name) {
    size_t n = strlen(name);
    return n > 3 && name[n-3] == '.' &&
           tolower((unsigned char)name[n-2]) == 'i' &&
           tolower((unsigned char)name[n-1]) == 'r';
}
