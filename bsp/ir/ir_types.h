// bsp/ir/ir_types.h — core IR value types (pure logic, host-testable).
// Timing arrays: alternating mark/space durations in us; index 0 is a mark.
#ifndef IR_TYPES_H
#define IR_TYPES_H
#include <stdint.h>
#include <stdbool.h>

#define IR_MAX_TIMINGS 512u

typedef enum {
    IR_PROTO_UNKNOWN = 0,
    IR_PROTO_NEC,        // 8-bit addr + 8-bit cmd, both complement-checked
    IR_PROTO_NECEXT,     // 16-bit addr + 16-bit cmd, no complement check
    IR_PROTO_SAMSUNG32,
    IR_PROTO_RC5,        // includes RC5X (inverted S2 = command bit 6)
    IR_PROTO_RC6,
    IR_PROTO_SIRC,       // 12-bit: cmd7 + addr5
    IR_PROTO_SIRC15,     // cmd7 + addr8
    IR_PROTO_SIRC20,     // cmd7 + addr13
    IR_PROTO_KASEIKYO,   // 48-bit: address = bits[0..23], command = bits[24..47]
    IR_PROTO_RCA,        // addr4 + cmd8, complement-checked
    IR_PROTO_COUNT
} ir_protocol_t;

typedef struct {
    ir_protocol_t protocol;
    uint32_t address;
    uint32_t command;
    bool repeat;         // true for a NEC repeat frame
} ir_message_t;

// Flipper .ir file protocol name ("NEC", "NECext", "Samsung32", ...).
const char *ir_protocol_name(ir_protocol_t p);
#endif
