// Host test: SPSC ring — order, full, empty, wraparound.
#include "msgq.h"
#include "test_util.h"

int main(void) {
    msgq_t q;
    frame_msg_t in = {0}, out;
    msgq_init(&q);
    ASSERT_EQ(msgq_pop(&q, &out), 0);            // empty
    for (int i = 0; i < MSGQ_CAP; i++) {
        in.sender = (uint8_t)i; in.len = 1; in.payload[0] = (uint8_t)i;
        ASSERT_EQ(msgq_push(&q, &in), 1);
    }
    ASSERT_EQ(msgq_push(&q, &in), 0);            // full
    for (int i = 0; i < MSGQ_CAP; i++) {
        ASSERT_EQ(msgq_pop(&q, &out), 1);
        ASSERT_EQ(out.sender, i);                // FIFO order
    }
    ASSERT_EQ(msgq_pop(&q, &out), 0);
    for (int i = 0; i < 3 * MSGQ_CAP; i++) {     // wraparound
        in.sender = (uint8_t)i;
        ASSERT_EQ(msgq_push(&q, &in), 1);
        ASSERT_EQ(msgq_pop(&q, &out), 1);
        ASSERT_EQ(out.sender, (uint8_t)i);
    }
    TEST_RETURN();
}
