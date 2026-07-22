#include "msgq.h"
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "msgq requires lock-free atomic_uint");

void msgq_init(msgq_t *q) {
    memset(q->slot, 0, sizeof q->slot);
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
}

int msgq_push(msgq_t *q, const frame_msg_t *m) {
    unsigned h = atomic_load_explicit(&q->head, memory_order_relaxed);
    unsigned t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h - t >= MSGQ_CAP) return 0;
    q->slot[h % MSGQ_CAP] = *m;
    atomic_store_explicit(&q->head, h + 1u, memory_order_release);  // publish after the copy
    return 1;
}

int msgq_pop(msgq_t *q, frame_msg_t *m) {
    unsigned t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    unsigned h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (h == t) return 0;
    *m = q->slot[t % MSGQ_CAP];
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
    return 1;
}
