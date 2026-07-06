#include "test_util.h"
#include "db_sort.h"
#include <string.h>

int main(void) {
    db_listing_t l;
    l.count = 4;
    l.dropped = 0;
    strcpy(l.e[0].name, "zeta.ir");   l.e[0].is_dir = false;
    strcpy(l.e[1].name, "Alpha");     l.e[1].is_dir = true;
    strcpy(l.e[2].name, "beta.ir");   l.e[2].is_dir = false;
    strcpy(l.e[3].name, "gamma");     l.e[3].is_dir = true;

    db_listing_sort(&l);

    // Directories first, then files; case-insensitive alphabetical within each.
    ASSERT_TRUE(strcmp(l.e[0].name, "Alpha") == 0);
    ASSERT_TRUE(l.e[0].is_dir);
    ASSERT_TRUE(strcmp(l.e[1].name, "gamma") == 0);
    ASSERT_TRUE(strcmp(l.e[2].name, "beta.ir") == 0);
    ASSERT_TRUE(!l.e[2].is_dir);
    ASSERT_TRUE(strcmp(l.e[3].name, "zeta.ir") == 0);

    ASSERT_TRUE(db_name_is_ir("tv.ir"));
    ASSERT_TRUE(db_name_is_ir("TV.IR"));
    ASSERT_TRUE(!db_name_is_ir("tv.txt"));
    ASSERT_TRUE(!db_name_is_ir(".ir"));      // needs a stem
    ASSERT_TRUE(!db_name_is_ir("ir"));

    ASSERT_TRUE(db_ci_cmp("alpha", "ALPHA") == 0);
    ASSERT_TRUE(db_ci_cmp("alpha", "beta") < 0);
    ASSERT_TRUE(db_ci_cmp("Gamma", "beta") > 0);

    TEST_RETURN();
}
