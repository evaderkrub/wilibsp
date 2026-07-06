// bsp/ir/ir_resolve.h – resolve a .ir entry into timings + carrier (pure).
// Replaces Plan 2's ir_file_send: the caller supplies the default carrier
// (from settings) instead of a hardcoded 38 kHz.
#ifndef IR_RESOLVE_H
#define IR_RESOLVE_H
#include "ir_file.h"

// Fills t[] and *carrier_out. Parsed entries encode at default_carrier; raw
// entries use their file frequency, or default_carrier when absent (0).
// Returns the timing count; 0 when the entry doesn't encode, is empty, or
// exceeds max.
uint32_t ir_resolve(const ir_file_entry_t *e, uint32_t default_carrier,
                    uint32_t *t, uint32_t max, uint32_t *carrier_out);
#endif
