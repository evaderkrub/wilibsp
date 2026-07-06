#include "test_util.h"
#include "ir_resolve.h"
#include "ir_decode.h"
#include <string.h>

static ir_file_entry_t e;   // ~2.3 KB — keep off the stack even on host

int main(void) {
    uint32_t t[IR_MAX_TIMINGS], carrier = 0;

    // Parsed entry encodes at the caller's default carrier.
    memset(&e, 0, sizeof e);
    strcpy(e.name, "Power");
    e.msg = (ir_message_t){IR_PROTO_NEC, 0x04, 0x08, false};
    uint32_t n = ir_resolve(&e, 40000, t, IR_MAX_TIMINGS, &carrier);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(carrier == 40000);
    ir_message_t got;
    ASSERT_TRUE(ir_decode(t, n, &got));
    ASSERT_TRUE(got.protocol == IR_PROTO_NEC);
    ASSERT_TRUE(got.address == 0x04 && got.command == 0x08);

    // Raw entry keeps its own file frequency...
    memset(&e, 0, sizeof e);
    e.is_raw = true;
    e.frequency = 56000;
    e.timing_count = 3;
    e.timings[0] = 500; e.timings[1] = 500; e.timings[2] = 500;
    n = ir_resolve(&e, 38000, t, IR_MAX_TIMINGS, &carrier);
    ASSERT_TRUE(n == 3 && carrier == 56000 && t[2] == 500);

    // ...and falls back to the default when the file omitted it (0).
    e.frequency = 0;
    n = ir_resolve(&e, 38000, t, IR_MAX_TIMINGS, &carrier);
    ASSERT_TRUE(n == 3 && carrier == 38000);

    // Failure paths: empty raw, raw bigger than the output, unknown protocol.
    e.timing_count = 0;
    ASSERT_TRUE(ir_resolve(&e, 38000, t, IR_MAX_TIMINGS, &carrier) == 0);
    e.timing_count = 3;
    ASSERT_TRUE(ir_resolve(&e, 38000, t, 2, &carrier) == 0);
    memset(&e, 0, sizeof e);
    e.msg.protocol = IR_PROTO_UNKNOWN;
    ASSERT_TRUE(ir_resolve(&e, 38000, t, IR_MAX_TIMINGS, &carrier) == 0);

    TEST_RETURN();
}
