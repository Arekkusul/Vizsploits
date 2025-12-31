#ifndef PRIMITIVES_H
#define PRIMITIVES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

/**
 * Exploit Primitive Types
 *
 * Every exploitation technique boils down to combinations of these primitives.
 * The visualizer understands primitives, not specific exploits.
 */
typedef enum {
    // Memory management
    PRIM_ALLOC,              // malloc, calloc, new
    PRIM_FREE,               // free, delete
    PRIM_REALLOC,            // realloc

    // Memory access
    PRIM_READ,               // Arbitrary read
    PRIM_WRITE,              // Arbitrary write
    PRIM_EXECUTE,            // Execute code at address

    // Control flow
    PRIM_CALL,               // Function call
    PRIM_RETURN,             // Function return
    PRIM_JUMP,               // Unconditional jump
    PRIM_BRANCH,             // Conditional branch

    // Vulnerability patterns
    PRIM_OVERFLOW,           // Buffer overflow detected
    PRIM_UAF,                // Use-after-free detected
    PRIM_DOUBLE_FREE,        // Double free detected
    PRIM_RACE,               // Race condition window
    PRIM_INTEGER_OVERFLOW,   // Integer overflow
    PRIM_FORMAT_STRING,      // Format string vulnerability

    // System operations
    PRIM_SYSCALL,            // System call
    PRIM_MMAP,               // Memory mapping
    PRIM_MPROTECT,           // Permission change

    // Privilege
    PRIM_PRIV_ESCALATION,    // Privilege level change

    // Markers
    PRIM_CHECKPOINT,         // User-defined checkpoint
    PRIM_ANNOTATION,         // Annotation/comment

    PRIM_CUSTOM,             // User-defined primitive
    PRIM_NONE                // Sentinel/terminator
} primitive_type_t;

/**
 * Memory region permissions
 */
typedef enum {
    PERM_NONE  = 0,
    PERM_READ  = 1,
    PERM_WRITE = 2,
    PERM_EXEC  = 4
} mem_perm_t;

/**
 * Primitive Event Structure
 *
 * This is the core data structure that flows through the event bus.
 * All instrumentation backends emit these events.
 */
typedef struct {
    primitive_type_t type;
    uint64_t timestamp;      // Nanoseconds since start
    pid_t pid;
    pid_t tid;

    // Context
    void *address;           // Primary address involved
    size_t size;             // Size of operation
    void *caller;            // Return address / caller

    // Type-specific data
    union {
        // PRIM_ALLOC
        struct {
            void *ptr;
            size_t requested_size;
            size_t actual_size;
        } alloc;

        // PRIM_FREE
        struct {
            void *ptr;
            bool was_freed;      // For double-free detection
            size_t chunk_size;   // If known
        } free;

        // PRIM_READ / PRIM_WRITE
        struct {
            void *src;
            void *dst;
            size_t len;
            bool is_controlled;  // User-controlled data?
            uint8_t preview[16]; // First bytes of data
        } memop;

        // PRIM_CALL / PRIM_RETURN
        struct {
            void *target;
            void *return_addr;
            int syscall_num;     // For syscalls
            const char *func_name;
        } call;

        // PRIM_OVERFLOW
        struct {
            void *buffer_start;
            size_t buffer_size;
            size_t overflow_bytes;
            void *corrupted_addr;
        } overflow;

        // PRIM_UAF
        struct {
            void *freed_ptr;
            void *reused_by;
            uint64_t free_timestamp;
            uint64_t use_timestamp;
        } uaf;

        // PRIM_DOUBLE_FREE
        struct {
            void *ptr;
            uint64_t first_free_time;
            uint64_t second_free_time;
        } double_free;

        // PRIM_MMAP / PRIM_MPROTECT
        struct {
            void *start;
            size_t length;
            int prot;
            int flags;
        } mmap;

        // PRIM_CHECKPOINT / PRIM_ANNOTATION
        struct {
            int checkpoint_id;
            const char *message;
        } marker;

    } data;

    // Metadata
    const char *description;     // Human-readable description
    const char *source_file;     // Source file (if known)
    int source_line;             // Source line (if known)
    void *custom_data;           // User data

    // State tracking
    int step_number;             // Sequential step number
    bool is_exploit_relevant;    // Part of exploit chain?
} primitive_event_t;

/**
 * Get string representation of primitive type
 */
const char *primitive_type_to_string(primitive_type_t type);

/**
 * Get short name for primitive type
 */
const char *primitive_type_short(primitive_type_t type);

/**
 * Get current timestamp in nanoseconds
 */
uint64_t get_timestamp_ns(void);

/**
 * Create a new primitive event (zeroed with timestamp set)
 */
primitive_event_t primitive_event_create(primitive_type_t type);

/**
 * Clone a primitive event (deep copy of strings)
 */
primitive_event_t *primitive_event_clone(const primitive_event_t *evt);

/**
 * Free a cloned primitive event
 */
void primitive_event_free(primitive_event_t *evt);

#endif // PRIMITIVES_H
