// protocol.h — chat identity + canned-message send/resend orchestration.
#ifndef RC_PROTOCOL_H
#define RC_PROTOCOL_H
#include <stdint.h>

#define PROTO_NUM_CANNED 8
extern const char *const proto_canned[PROTO_NUM_CANNED];

void    proto_init(void);
uint8_t proto_self_id(void);
void    proto_send_canned(int idx);
void    proto_resend(void);

#endif
