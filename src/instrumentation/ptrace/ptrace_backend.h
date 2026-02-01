#ifndef PTRACE_BACKEND_H
#define PTRACE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * Watch region: a memory region to monitor for changes
 */
typedef struct {
    uintptr_t address;
    size_t size;
    char label[64];
} ptrace_watch_t;

#define PTRACE_MAX_WATCHES 16
#define PTRACE_MAX_BREAKPOINTS 32

/**
 * Ptrace session configuration
 */
typedef struct {
    const char *target_path;      /* Path to binary to trace */
    char **argv;                  /* Arguments (NULL-terminated) */
    int argc;

    /* Watch regions for memory change detection */
    ptrace_watch_t watches[PTRACE_MAX_WATCHES];
    int num_watches;

    /* Symbol names to set breakpoints on (e.g., "malloc", "free") */
    const char *break_symbols[PTRACE_MAX_BREAKPOINTS];
    int num_break_symbols;

    /* Options */
    bool trace_syscalls;          /* Emit PRIM_SYSCALL events */
    bool trace_malloc;            /* Auto-intercept malloc/free/realloc */
    bool verbose;
} ptrace_config_t;

/**
 * Register state (x86_64)
 */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
} ptrace_regs_t;

/**
 * Opaque session handle
 */
typedef struct ptrace_session ptrace_session_t;

/**
 * Create a new ptrace session
 */
ptrace_session_t *ptrace_session_create(const ptrace_config_t *config);

/**
 * Destroy a ptrace session (kills child if still running)
 */
void ptrace_session_destroy(ptrace_session_t *session);

/**
 * Start the traced process.
 * Returns 0 on success, -1 on error.
 */
int ptrace_session_start(ptrace_session_t *session);

/**
 * Single-step the tracee (execute one instruction).
 * Returns 0 on success, -1 on error/exit.
 */
int ptrace_session_step(ptrace_session_t *session);

/**
 * Continue execution until next breakpoint/signal/exit.
 * Returns 0 on success, -1 on error/exit.
 */
int ptrace_session_continue(ptrace_session_t *session);

/**
 * Read memory from the tracee.
 * Returns bytes read, or -1 on error.
 */
ssize_t ptrace_session_read_memory(ptrace_session_t *session,
                                    uintptr_t addr, void *buf, size_t len);

/**
 * Get the current register state.
 * Returns 0 on success.
 */
int ptrace_session_get_regs(ptrace_session_t *session, ptrace_regs_t *regs);

/**
 * Check if the tracee is still running.
 */
bool ptrace_session_is_running(ptrace_session_t *session);

/**
 * Stop the tracee (send SIGSTOP).
 */
int ptrace_session_stop(ptrace_session_t *session);

/**
 * Get the tracee PID.
 */
pid_t ptrace_session_get_pid(ptrace_session_t *session);

/**
 * Add a watch region at runtime.
 */
int ptrace_session_add_watch(ptrace_session_t *session,
                              uintptr_t addr, size_t size, const char *label);

#endif /* PTRACE_BACKEND_H */
