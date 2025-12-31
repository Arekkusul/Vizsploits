#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "primitives.h"

/**
 * Event Bus Architecture
 *
 * All instrumentation backends emit events to a central bus.
 * Subscribers receive events for specific primitive types.
 * This decouples instrumentation from visualization.
 */

// Maximum number of subscribers per event type
#define MAX_SUBSCRIBERS 16

// Maximum events in the queue
#define EVENT_QUEUE_SIZE 1024

/**
 * Event handler callback type
 */
typedef void (*event_handler_t)(const primitive_event_t *event, void *userdata);

/**
 * Event filter callback type (return true to keep event)
 */
typedef bool (*event_filter_t)(const primitive_event_t *event, void *userdata);

/**
 * Initialize the event bus
 * Must be called before any other event_bus functions
 */
int event_bus_init(void);

/**
 * Cleanup the event bus
 * Frees all resources
 */
void event_bus_cleanup(void);

/**
 * Subscribe to events of a specific type
 *
 * @param type      Primitive type to subscribe to (PRIM_NONE for all events)
 * @param handler   Callback function
 * @param userdata  User data passed to callback
 * @return          Subscription ID on success, -1 on failure
 */
int event_bus_subscribe(primitive_type_t type, event_handler_t handler, void *userdata);

/**
 * Unsubscribe from events
 *
 * @param subscription_id  ID returned by event_bus_subscribe
 */
void event_bus_unsubscribe(int subscription_id);

/**
 * Emit an event to the bus
 * Called by instrumentation layer
 *
 * @param event  The event to emit (will be copied)
 * @return       0 on success, -1 on failure
 */
int event_bus_emit(const primitive_event_t *event);

/**
 * Process pending events
 * Dispatches queued events to subscribers
 *
 * @return  Number of events processed
 */
int event_bus_process(void);

/**
 * Process a single event (blocking)
 * Waits for an event if queue is empty
 *
 * @param timeout_ms  Timeout in milliseconds (-1 for infinite)
 * @return            1 if event processed, 0 if timeout, -1 on error
 */
int event_bus_process_one(int timeout_ms);

/**
 * Set a global event filter
 * Events that don't pass the filter won't be dispatched
 *
 * @param filter    Filter function (NULL to clear)
 * @param userdata  User data for filter
 */
void event_bus_set_filter(event_filter_t filter, void *userdata);

/**
 * Get number of events in queue
 */
size_t event_bus_pending_count(void);

/**
 * Clear the event queue
 */
void event_bus_clear(void);

/**
 * Get total events emitted since init
 */
uint64_t event_bus_total_events(void);

/**
 * Pause event processing
 */
void event_bus_pause(void);

/**
 * Resume event processing
 */
void event_bus_resume(void);

/**
 * Check if event bus is paused
 */
bool event_bus_is_paused(void);

#endif // EVENT_BUS_H
