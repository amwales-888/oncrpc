/*
 * error.c - status strings and version.
 */
#include "internal.h"

const char *oncRPCVersion(void)
{
    return ONCRPC_VERSION_STRING;
}

const char *oncRPCStrError(oncRPCStat stat)
{
    switch (stat) {
    case ONCRPC_OK:                     return "success";
    case ONCRPC_ERR_CANTENCODE:         return "cannot encode arguments";
    case ONCRPC_ERR_CANTDECODE:         return "cannot decode reply";
    case ONCRPC_ERR_CANTSEND:           return "cannot send";
    case ONCRPC_ERR_CANTRECV:           return "cannot receive";
    case ONCRPC_ERR_TIMEDOUT:           return "timed out";
    case ONCRPC_ERR_VERSMISMATCH:       return "RPC protocol version mismatch";
    case ONCRPC_ERR_AUTHERROR:          return "authentication error";
    case ONCRPC_ERR_PROGUNAVAIL:        return "program unavailable";
    case ONCRPC_ERR_PROGVERSMISMATCH:   return "program version mismatch";
    case ONCRPC_ERR_PROCUNAVAIL:        return "procedure unavailable";
    case ONCRPC_ERR_CANTDECODEARGS:     return "server cannot decode arguments";
    case ONCRPC_ERR_SYSTEMERROR:        return "remote system error";
    case ONCRPC_ERR_UNKNOWNHOST:        return "unknown host";
    case ONCRPC_ERR_CANTCONNECT:        return "cannot connect";
    case ONCRPC_ERR_PMAPFAILURE:        return "port mapper failure";
    case ONCRPC_ERR_PROGNOTREGISTERED:  return "program not registered";
    case ONCRPC_ERR_NOMEM:              return "out of memory";
    case ONCRPC_ERR_INVALID:            return "invalid argument";
    case ONCRPC_ERR_CANCELLED:          return "cancelled";
    case ONCRPC_ERR_FAILED:             return "failed";
    }
    return "unknown error";
}
