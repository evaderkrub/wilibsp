#include "msgq.h"
#include <string.h>

void msgq_init(msgq_t *q) { memset(q, 0, sizeof *q); }

int msgq_push(msgq_t *q, const frame_msg_t *m) {
    if (q->head - q->tail >= MSGQ_CAP) return 0;
    q->slot[q->head % MSGQ_CAP] = *m;
    q->head++;               // publish after the copy
    return 1;
}

int msgq_pop(msgq_t *q, frame_msg_t *m) {
    if (q->head == q->tail) return 0;
    *m = q->slot[q->tail % MSGQ_CAP];
    q->tail++;
    return 1;
}
