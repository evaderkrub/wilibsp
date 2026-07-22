// modem/msgq.h — single-producer/single-consumer frame queue (core 1 -> core 0).
// Lock-free: producer writes slot then advances head; consumer reads slot then
// advances tail. Indices are free-running and masked (MSGQ_CAP power of two).
// head/tail are C11 atomics with acquire/release ordering so that the
// non-atomic frame_msg_t slot copy is correctly synchronized between the
// producer and consumer cores.
#ifndef RC_MSGQ_H
#define RC_MSGQ_H
#include "frame.h"
#include <stdatomic.h>

#define MSGQ_CAP 8u

typedef struct {
    frame_msg_t slot[MSGQ_CAP];
    atomic_uint head;   // producer-owned
    atomic_uint tail;   // consumer-owned
} msgq_t;

void msgq_init(msgq_t *q);
int  msgq_push(msgq_t *q, const frame_msg_t *m);
int  msgq_pop(msgq_t *q, frame_msg_t *m);

#endif
