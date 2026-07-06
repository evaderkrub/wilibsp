// bsp/ir/ir_protocols.c
#include "ir_protocols.h"

const char *ir_protocol_name(ir_protocol_t p) {
    switch (p) {
    case IR_PROTO_NEC:       return "NEC";
    case IR_PROTO_NECEXT:    return "NECext";
    case IR_PROTO_SAMSUNG32: return "Samsung32";
    case IR_PROTO_RC5:       return "RC5";
    case IR_PROTO_RC6:       return "RC6";
    case IR_PROTO_SIRC:      return "SIRC";
    case IR_PROTO_SIRC15:    return "SIRC15";
    case IR_PROTO_SIRC20:    return "SIRC20";
    case IR_PROTO_KASEIKYO:  return "Kaseikyo";
    case IR_PROTO_RCA:       return "RCA";
    default:                 return "raw";
    }
}
