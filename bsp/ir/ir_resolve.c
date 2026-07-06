// bsp/ir/ir_resolve.c
#include "ir_resolve.h"
#include "ir_encode.h"
#include <string.h>

uint32_t ir_resolve(const ir_file_entry_t *e, uint32_t default_carrier,
                    uint32_t *t, uint32_t max, uint32_t *carrier_out) {
    if (!e) return 0;
    if (e->is_raw) {
        if (!e->timing_count || e->timing_count > max) return 0;
        memcpy(t, e->timings, e->timing_count * sizeof t[0]);
        *carrier_out = e->frequency ? e->frequency : default_carrier;
        return e->timing_count;
    }
    *carrier_out = default_carrier;
    return ir_encode(&e->msg, t, max);
}
