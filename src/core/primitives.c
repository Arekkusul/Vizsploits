#include "primitives.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t start_time_ns = 0;

const char *primitive_type_to_string(primitive_type_t type) {
    switch (type) {
        case PRIM_ALLOC:           return "ALLOCATE";
        case PRIM_FREE:            return "FREE";
        case PRIM_REALLOC:         return "REALLOCATE";
        case PRIM_READ:            return "READ";
        case PRIM_WRITE:           return "WRITE";
        case PRIM_EXECUTE:         return "EXECUTE";
        case PRIM_CALL:            return "CALL";
        case PRIM_RETURN:          return "RETURN";
        case PRIM_JUMP:            return "JUMP";
        case PRIM_BRANCH:          return "BRANCH";
        case PRIM_OVERFLOW:        return "OVERFLOW";
        case PRIM_UAF:             return "USE-AFTER-FREE";
        case PRIM_DOUBLE_FREE:     return "DOUBLE-FREE";
        case PRIM_RACE:            return "RACE";
        case PRIM_INTEGER_OVERFLOW: return "INTEGER-OVERFLOW";
        case PRIM_FORMAT_STRING:   return "FORMAT-STRING";
        case PRIM_SYSCALL:         return "SYSCALL";
        case PRIM_MMAP:            return "MMAP";
        case PRIM_MPROTECT:        return "MPROTECT";
        case PRIM_PRIV_ESCALATION: return "PRIVILEGE-ESCALATION";
        case PRIM_CHECKPOINT:      return "CHECKPOINT";
        case PRIM_ANNOTATION:      return "ANNOTATION";
        case PRIM_CUSTOM:          return "CUSTOM";
        case PRIM_NONE:            return "NONE";
        default:                   return "UNKNOWN";
    }
}

const char *primitive_type_short(primitive_type_t type) {
    switch (type) {
        case PRIM_ALLOC:           return "ALLOC";
        case PRIM_FREE:            return "FREE";
        case PRIM_REALLOC:         return "REALLC";
        case PRIM_READ:            return "READ";
        case PRIM_WRITE:           return "WRITE";
        case PRIM_EXECUTE:         return "EXEC";
        case PRIM_CALL:            return "CALL";
        case PRIM_RETURN:          return "RET";
        case PRIM_JUMP:            return "JMP";
        case PRIM_BRANCH:          return "BR";
        case PRIM_OVERFLOW:        return "OVFL";
        case PRIM_UAF:             return "UAF";
        case PRIM_DOUBLE_FREE:     return "DFREE";
        case PRIM_RACE:            return "RACE";
        case PRIM_INTEGER_OVERFLOW: return "IOVFL";
        case PRIM_FORMAT_STRING:   return "FMTSTR";
        case PRIM_SYSCALL:         return "SYSCL";
        case PRIM_MMAP:            return "MMAP";
        case PRIM_MPROTECT:        return "MPROT";
        case PRIM_PRIV_ESCALATION: return "PRIV";
        case PRIM_CHECKPOINT:      return "CHKPT";
        case PRIM_ANNOTATION:      return "NOTE";
        case PRIM_CUSTOM:          return "CUST";
        case PRIM_NONE:            return "NONE";
        default:                   return "UNK";
    }
}

uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    if (start_time_ns == 0) {
        start_time_ns = now;
    }

    return now - start_time_ns;
}

primitive_event_t primitive_event_create(primitive_type_t type) {
    primitive_event_t evt = {0};
    evt.type = type;
    evt.timestamp = get_timestamp_ns();
    evt.pid = getpid();
    evt.tid = 0;  // Could use gettid() on Linux
    evt.is_exploit_relevant = true;
    return evt;
}

primitive_event_t *primitive_event_clone(const primitive_event_t *evt) {
    if (!evt) return NULL;

    primitive_event_t *clone = malloc(sizeof(primitive_event_t));
    if (!clone) return NULL;

    memcpy(clone, evt, sizeof(primitive_event_t));

    // Deep copy strings
    if (evt->description) {
        clone->description = strdup(evt->description);
    }
    if (evt->source_file) {
        clone->source_file = strdup(evt->source_file);
    }

    // Deep copy type-specific strings
    switch (evt->type) {
        case PRIM_CALL:
        case PRIM_RETURN:
            if (evt->data.call.func_name) {
                clone->data.call.func_name = strdup(evt->data.call.func_name);
            }
            break;
        case PRIM_CHECKPOINT:
        case PRIM_ANNOTATION:
            if (evt->data.marker.message) {
                clone->data.marker.message = strdup(evt->data.marker.message);
            }
            break;
        default:
            break;
    }

    return clone;
}

void primitive_event_free(primitive_event_t *evt) {
    if (!evt) return;

    // Free copied strings
    free((void*)evt->description);
    free((void*)evt->source_file);

    switch (evt->type) {
        case PRIM_CALL:
        case PRIM_RETURN:
            free((void*)evt->data.call.func_name);
            break;
        case PRIM_CHECKPOINT:
        case PRIM_ANNOTATION:
            free((void*)evt->data.marker.message);
            break;
        default:
            break;
    }

    free(evt);
}
