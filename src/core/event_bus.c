#include "event_bus.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/**
 * Subscription entry
 */
typedef struct {
    int id;
    primitive_type_t type;
    event_handler_t handler;
    void *userdata;
    bool active;
} subscription_t;

/**
 * Event bus state
 */
typedef struct {
    // Event queue (ring buffer)
    primitive_event_t *events[EVENT_QUEUE_SIZE];
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;

    // Subscribers
    subscription_t subscribers[MAX_SUBSCRIBERS * 32];  // Room for all types
    int num_subscribers;
    int next_subscription_id;

    // Global filter
    event_filter_t filter;
    void *filter_userdata;

    // Statistics
    uint64_t total_events;
    int current_step;

    // State
    bool initialized;
    bool paused;

    // Thread safety
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} event_bus_t;

static event_bus_t g_bus = {0};

int event_bus_init(void) {
    if (g_bus.initialized) {
        return 0;  // Already initialized
    }

    memset(&g_bus, 0, sizeof(g_bus));

    if (pthread_mutex_init(&g_bus.mutex, NULL) != 0) {
        return -1;
    }

    if (pthread_cond_init(&g_bus.cond, NULL) != 0) {
        pthread_mutex_destroy(&g_bus.mutex);
        return -1;
    }

    g_bus.next_subscription_id = 1;
    g_bus.initialized = true;
    g_bus.paused = false;

    return 0;
}

void event_bus_cleanup(void) {
    if (!g_bus.initialized) return;

    pthread_mutex_lock(&g_bus.mutex);

    // Free queued events
    for (size_t i = 0; i < EVENT_QUEUE_SIZE; i++) {
        if (g_bus.events[i]) {
            primitive_event_free(g_bus.events[i]);
            g_bus.events[i] = NULL;
        }
    }

    g_bus.initialized = false;

    pthread_mutex_unlock(&g_bus.mutex);

    pthread_cond_destroy(&g_bus.cond);
    pthread_mutex_destroy(&g_bus.mutex);
}

int event_bus_subscribe(primitive_type_t type, event_handler_t handler, void *userdata) {
    if (!g_bus.initialized || !handler) return -1;

    pthread_mutex_lock(&g_bus.mutex);

    if (g_bus.num_subscribers >= (int)(sizeof(g_bus.subscribers) / sizeof(g_bus.subscribers[0]))) {
        pthread_mutex_unlock(&g_bus.mutex);
        return -1;
    }

    int id = g_bus.next_subscription_id++;

    subscription_t *sub = &g_bus.subscribers[g_bus.num_subscribers++];
    sub->id = id;
    sub->type = type;
    sub->handler = handler;
    sub->userdata = userdata;
    sub->active = true;

    pthread_mutex_unlock(&g_bus.mutex);

    return id;
}

void event_bus_unsubscribe(int subscription_id) {
    if (!g_bus.initialized) return;

    pthread_mutex_lock(&g_bus.mutex);

    for (int i = 0; i < g_bus.num_subscribers; i++) {
        if (g_bus.subscribers[i].id == subscription_id) {
            g_bus.subscribers[i].active = false;
            break;
        }
    }

    pthread_mutex_unlock(&g_bus.mutex);
}

int event_bus_emit(const primitive_event_t *event) {
    if (!g_bus.initialized || !event) return -1;

    pthread_mutex_lock(&g_bus.mutex);

    // Check if queue is full
    if (g_bus.queue_count >= EVENT_QUEUE_SIZE) {
        pthread_mutex_unlock(&g_bus.mutex);
        return -1;  // Queue full
    }

    // Clone the event
    primitive_event_t *evt_copy = primitive_event_clone(event);
    if (!evt_copy) {
        pthread_mutex_unlock(&g_bus.mutex);
        return -1;
    }

    // Assign step number
    evt_copy->step_number = ++g_bus.current_step;

    // Add to queue
    g_bus.events[g_bus.queue_tail] = evt_copy;
    g_bus.queue_tail = (g_bus.queue_tail + 1) % EVENT_QUEUE_SIZE;
    g_bus.queue_count++;
    g_bus.total_events++;

    // Signal waiting threads
    pthread_cond_signal(&g_bus.cond);

    pthread_mutex_unlock(&g_bus.mutex);

    return 0;
}

static void dispatch_event(const primitive_event_t *event) {
    // Apply global filter
    if (g_bus.filter) {
        if (!g_bus.filter(event, g_bus.filter_userdata)) {
            return;
        }
    }

    // Dispatch to subscribers
    for (int i = 0; i < g_bus.num_subscribers; i++) {
        subscription_t *sub = &g_bus.subscribers[i];

        if (!sub->active) continue;

        // PRIM_NONE means subscribe to all events
        if (sub->type == PRIM_NONE || sub->type == event->type) {
            sub->handler(event, sub->userdata);
        }
    }
}

int event_bus_process(void) {
    if (!g_bus.initialized) return -1;

    int processed = 0;

    pthread_mutex_lock(&g_bus.mutex);

    while (g_bus.queue_count > 0 && !g_bus.paused) {
        primitive_event_t *event = g_bus.events[g_bus.queue_head];
        g_bus.events[g_bus.queue_head] = NULL;
        g_bus.queue_head = (g_bus.queue_head + 1) % EVENT_QUEUE_SIZE;
        g_bus.queue_count--;

        // Dispatch outside of lock
        pthread_mutex_unlock(&g_bus.mutex);
        dispatch_event(event);
        primitive_event_free(event);
        processed++;
        pthread_mutex_lock(&g_bus.mutex);
    }

    pthread_mutex_unlock(&g_bus.mutex);

    return processed;
}

int event_bus_process_one(int timeout_ms) {
    if (!g_bus.initialized) return -1;

    pthread_mutex_lock(&g_bus.mutex);

    // Wait for event if queue is empty
    if (g_bus.queue_count == 0) {
        if (timeout_ms < 0) {
            // Wait indefinitely
            while (g_bus.queue_count == 0 && g_bus.initialized) {
                pthread_cond_wait(&g_bus.cond, &g_bus.mutex);
            }
        } else if (timeout_ms > 0) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }

            if (g_bus.queue_count == 0) {
                pthread_cond_timedwait(&g_bus.cond, &g_bus.mutex, &ts);
            }
        }
    }

    if (g_bus.queue_count == 0 || g_bus.paused) {
        pthread_mutex_unlock(&g_bus.mutex);
        return 0;
    }

    // Get event from queue
    primitive_event_t *event = g_bus.events[g_bus.queue_head];
    g_bus.events[g_bus.queue_head] = NULL;
    g_bus.queue_head = (g_bus.queue_head + 1) % EVENT_QUEUE_SIZE;
    g_bus.queue_count--;

    pthread_mutex_unlock(&g_bus.mutex);

    // Dispatch
    dispatch_event(event);
    primitive_event_free(event);

    return 1;
}

void event_bus_set_filter(event_filter_t filter, void *userdata) {
    pthread_mutex_lock(&g_bus.mutex);
    g_bus.filter = filter;
    g_bus.filter_userdata = userdata;
    pthread_mutex_unlock(&g_bus.mutex);
}

size_t event_bus_pending_count(void) {
    pthread_mutex_lock(&g_bus.mutex);
    size_t count = g_bus.queue_count;
    pthread_mutex_unlock(&g_bus.mutex);
    return count;
}

void event_bus_clear(void) {
    pthread_mutex_lock(&g_bus.mutex);

    while (g_bus.queue_count > 0) {
        primitive_event_t *event = g_bus.events[g_bus.queue_head];
        g_bus.events[g_bus.queue_head] = NULL;
        g_bus.queue_head = (g_bus.queue_head + 1) % EVENT_QUEUE_SIZE;
        g_bus.queue_count--;
        primitive_event_free(event);
    }

    pthread_mutex_unlock(&g_bus.mutex);
}

uint64_t event_bus_total_events(void) {
    pthread_mutex_lock(&g_bus.mutex);
    uint64_t total = g_bus.total_events;
    pthread_mutex_unlock(&g_bus.mutex);
    return total;
}

void event_bus_pause(void) {
    pthread_mutex_lock(&g_bus.mutex);
    g_bus.paused = true;
    pthread_mutex_unlock(&g_bus.mutex);
}

void event_bus_resume(void) {
    pthread_mutex_lock(&g_bus.mutex);
    g_bus.paused = false;
    pthread_cond_broadcast(&g_bus.cond);
    pthread_mutex_unlock(&g_bus.mutex);
}

bool event_bus_is_paused(void) {
    pthread_mutex_lock(&g_bus.mutex);
    bool paused = g_bus.paused;
    pthread_mutex_unlock(&g_bus.mutex);
    return paused;
}
