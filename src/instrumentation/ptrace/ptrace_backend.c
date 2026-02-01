#include "ptrace_backend.h"
#include "elf_symbols.h"
#include "../../core/event_bus.h"
#include "../../core/primitives.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/syscall.h>

/* Breakpoint: saves original byte replaced by INT3 */
typedef struct {
    uintptr_t address;
    uint8_t original_byte;
    const char *symbol_name;
    bool active;
} breakpoint_t;

/* Watch region with snapshot for change detection */
typedef struct {
    uintptr_t address;
    size_t size;
    char label[64];
    uint8_t *prev_data;
    bool has_prev;
} watch_region_t;

struct ptrace_session {
    ptrace_config_t config;
    pid_t child_pid;
    bool running;
    bool in_syscall;      /* Toggled on syscall enter/exit */

    /* Breakpoints */
    breakpoint_t breakpoints[PTRACE_MAX_BREAKPOINTS];
    int num_breakpoints;

    /* Watch regions */
    watch_region_t watches[PTRACE_MAX_WATCHES];
    int num_watches;

    /* Symbol resolution */
    elf_symtab_t *symtab;

    /* Step counter for events */
    int step_count;
};

/* ===== Memory access helpers ===== */

static ssize_t read_memory(pid_t pid, uintptr_t addr, void *buf, size_t len) {
    struct iovec local = { .iov_base = buf, .iov_len = len };
    struct iovec remote = { .iov_base = (void *)addr, .iov_len = len };
    return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

static int write_byte(pid_t pid, uintptr_t addr, uint8_t byte) {
    /* Read the word, replace the low byte, write it back */
    errno = 0;
    long word = ptrace(PTRACE_PEEKTEXT, pid, (void *)addr, NULL);
    if (errno != 0) return -1;

    long new_word = (word & ~0xFFL) | byte;
    if (ptrace(PTRACE_POKETEXT, pid, (void *)addr, (void *)new_word) < 0) {
        return -1;
    }
    return 0;
}

/* ===== Event emission helpers ===== */

static void emit_event(ptrace_session_t *s, primitive_type_t type,
                        uintptr_t addr, size_t size, const char *desc) {
    primitive_event_t evt = primitive_event_create(type);
    evt.pid = s->child_pid;
    evt.address = (void *)addr;
    evt.size = size;
    evt.description = desc;
    evt.step_number = ++s->step_count;
    event_bus_emit(&evt);
}

static void emit_alloc_event(ptrace_session_t *s, uintptr_t ptr,
                               size_t size, const char *desc) {
    primitive_event_t evt = primitive_event_create(PRIM_ALLOC);
    evt.pid = s->child_pid;
    evt.address = (void *)ptr;
    evt.size = size;
    evt.description = desc;
    evt.step_number = ++s->step_count;
    evt.data.alloc.ptr = (void *)ptr;
    evt.data.alloc.requested_size = size;
    evt.data.alloc.actual_size = size;
    event_bus_emit(&evt);
}

static void emit_free_event(ptrace_session_t *s, uintptr_t ptr, const char *desc) {
    primitive_event_t evt = primitive_event_create(PRIM_FREE);
    evt.pid = s->child_pid;
    evt.address = (void *)ptr;
    evt.description = desc;
    evt.step_number = ++s->step_count;
    evt.data.free.ptr = (void *)ptr;
    event_bus_emit(&evt);
}

static void emit_syscall_event(ptrace_session_t *s, int syscall_nr,
                                 uintptr_t rip) {
    primitive_event_t evt = primitive_event_create(PRIM_SYSCALL);
    evt.pid = s->child_pid;
    evt.address = (void *)rip;
    evt.step_number = ++s->step_count;
    evt.data.call.syscall_num = syscall_nr;

    /* Name common syscalls */
    const char *name = "syscall";
    switch (syscall_nr) {
        case SYS_read:      name = "sys_read"; break;
        case SYS_write:     name = "sys_write"; break;
        case SYS_open:      name = "sys_open"; break;
        case SYS_close:     name = "sys_close"; break;
        case SYS_mmap:      name = "sys_mmap"; break;
        case SYS_mprotect:  name = "sys_mprotect"; break;
        case SYS_munmap:    name = "sys_munmap"; break;
        case SYS_brk:       name = "sys_brk"; break;
        case SYS_execve:    name = "sys_execve"; break;
        case SYS_exit_group: name = "sys_exit_group"; break;
    }
    evt.description = name;
    evt.data.call.func_name = name;
    event_bus_emit(&evt);
}

static void emit_write_event(ptrace_session_t *s, uintptr_t addr,
                               const uint8_t *old_data __attribute__((unused)),
                               const uint8_t *new_data,
                               size_t len, const char *label) {
    primitive_event_t evt = primitive_event_create(PRIM_WRITE);
    evt.pid = s->child_pid;
    evt.address = (void *)addr;
    evt.size = len;
    evt.step_number = ++s->step_count;

    char desc[128];
    snprintf(desc, sizeof(desc), "Memory changed: %s +0x%lx (%zu bytes)",
             label, (unsigned long)addr, len);
    evt.description = desc;

    evt.data.memop.dst = (void *)addr;
    evt.data.memop.len = len > 16 ? 16 : len;
    memcpy(evt.data.memop.preview, new_data, evt.data.memop.len);
    event_bus_emit(&evt);
}

/* ===== Breakpoint management ===== */

static int set_breakpoint(ptrace_session_t *s, uintptr_t addr, const char *name) {
    if (s->num_breakpoints >= PTRACE_MAX_BREAKPOINTS) return -1;

    breakpoint_t *bp = &s->breakpoints[s->num_breakpoints];

    /* Save original byte */
    if (read_memory(s->child_pid, addr, &bp->original_byte, 1) != 1) {
        return -1;
    }

    /* Write INT3 (0xCC) */
    if (write_byte(s->child_pid, addr, 0xCC) < 0) {
        return -1;
    }

    bp->address = addr;
    bp->symbol_name = name;
    bp->active = true;
    s->num_breakpoints++;
    return 0;
}

static breakpoint_t *find_breakpoint(ptrace_session_t *s, uintptr_t addr) {
    for (int i = 0; i < s->num_breakpoints; i++) {
        if (s->breakpoints[i].address == addr && s->breakpoints[i].active) {
            return &s->breakpoints[i];
        }
    }
    return NULL;
}

static int handle_breakpoint(ptrace_session_t *s, breakpoint_t *bp) {
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, s->child_pid, NULL, &regs) < 0) return -1;

    /* RIP is one past the INT3 — move it back */
    regs.rip = bp->address;
    if (ptrace(PTRACE_SETREGS, s->child_pid, NULL, &regs) < 0) return -1;

    /* Restore original byte */
    write_byte(s->child_pid, bp->address, bp->original_byte);

    /* Determine what happened at this breakpoint */
    if (bp->symbol_name) {
        if (strcmp(bp->symbol_name, "malloc") == 0) {
            /* rdi = size argument */
            size_t alloc_size = regs.rdi;

            /* Single-step past the call, then read rax for return value */
            ptrace(PTRACE_SINGLESTEP, s->child_pid, NULL, NULL);
            int status;
            waitpid(s->child_pid, &status, 0);

            /* Re-read regs to get return address, then continue to return */
            /* For simplicity, emit with the size now; full interception
               would require a return breakpoint. */
            emit_alloc_event(s, 0, alloc_size, "malloc()");
        } else if (strcmp(bp->symbol_name, "free") == 0) {
            uintptr_t ptr = regs.rdi;
            emit_free_event(s, ptr, "free()");
        } else if (strcmp(bp->symbol_name, "realloc") == 0) {
            uintptr_t ptr = regs.rdi;
            size_t size = regs.rsi;
            char desc[64];
            snprintf(desc, sizeof(desc), "realloc(%p, %zu)",
                     (void *)ptr, size);
            emit_event(s, PRIM_REALLOC, ptr, size, desc);
        } else {
            char desc[128];
            snprintf(desc, sizeof(desc), "breakpoint: %s", bp->symbol_name);
            emit_event(s, PRIM_CALL, bp->address, 0, desc);
        }
    }

    /* Re-set the breakpoint by writing INT3 again */
    /* First, single-step one instruction with original byte */
    ptrace(PTRACE_SINGLESTEP, s->child_pid, NULL, NULL);
    int status;
    waitpid(s->child_pid, &status, 0);

    /* Re-insert INT3 */
    write_byte(s->child_pid, bp->address, 0xCC);

    return 0;
}

/* ===== Watch region management ===== */

static void check_watches(ptrace_session_t *s) {
    for (int i = 0; i < s->num_watches; i++) {
        watch_region_t *w = &s->watches[i];
        if (w->size == 0) continue;

        uint8_t current[4096];
        size_t read_size = w->size < sizeof(current) ? w->size : sizeof(current);

        if (read_memory(s->child_pid, w->address, current, read_size) !=
            (ssize_t)read_size) continue;

        if (w->has_prev) {
            /* Compare with previous snapshot */
            for (size_t off = 0; off < read_size;) {
                if (current[off] != w->prev_data[off]) {
                    /* Find extent of change */
                    size_t start = off;
                    while (off < read_size && current[off] != w->prev_data[off]) {
                        off++;
                    }
                    emit_write_event(s, w->address + start,
                                     w->prev_data + start,
                                     current + start,
                                     off - start, w->label);
                } else {
                    off++;
                }
            }
        }

        /* Update snapshot */
        memcpy(w->prev_data, current, read_size);
        w->has_prev = true;
    }
}

/* ===== Public API ===== */

ptrace_session_t *ptrace_session_create(const ptrace_config_t *config) {
    if (!config || !config->target_path) return NULL;

    ptrace_session_t *s = calloc(1, sizeof(ptrace_session_t));
    if (!s) return NULL;

    memcpy(&s->config, config, sizeof(ptrace_config_t));
    s->child_pid = -1;

    /* Set up watch regions */
    for (int i = 0; i < config->num_watches && i < PTRACE_MAX_WATCHES; i++) {
        s->watches[i].address = config->watches[i].address;
        s->watches[i].size = config->watches[i].size;
        snprintf(s->watches[i].label, sizeof(s->watches[i].label),
                 "%s", config->watches[i].label);
        s->watches[i].prev_data = calloc(1, config->watches[i].size);
        s->num_watches++;
    }

    return s;
}

void ptrace_session_destroy(ptrace_session_t *session) {
    if (!session) return;

    if (session->child_pid > 0 && session->running) {
        kill(session->child_pid, SIGKILL);
        waitpid(session->child_pid, NULL, 0);
    }

    if (session->symtab) {
        elf_symtab_destroy(session->symtab);
    }

    for (int i = 0; i < session->num_watches; i++) {
        free(session->watches[i].prev_data);
    }

    free(session);
}

int ptrace_session_start(ptrace_session_t *session) {
    if (!session) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        /* Child: request tracing and exec the target */
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        raise(SIGSTOP);

        if (session->config.argv) {
            execv(session->config.target_path, session->config.argv);
        } else {
            char *argv[] = { (char *)session->config.target_path, NULL };
            execv(session->config.target_path, argv);
        }
        _exit(127);
    }

    /* Parent: wait for child to stop */
    session->child_pid = pid;
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        session->child_pid = -1;
        return -1;
    }

    if (!WIFSTOPPED(status)) {
        session->child_pid = -1;
        return -1;
    }

    /* Set ptrace options */
    long opts = PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
                PTRACE_O_TRACEEXEC;
    if (session->config.trace_syscalls) {
        opts |= PTRACE_O_TRACESYSGOOD;
    }
    ptrace(PTRACE_SETOPTIONS, pid, NULL, (void *)opts);

    session->running = true;

    /* Resolve symbols after exec */
    /* Continue past the SIGSTOP to the exec, then stop again */
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, &status, 0);

    /* Now resolve symbols */
    session->symtab = elf_symtab_create(pid);
    if (session->symtab && session->config.trace_malloc) {
        elf_symtab_resolve_libc(session->symtab, pid);

        /* Set breakpoints on malloc/free/realloc */
        uintptr_t malloc_addr = elf_symtab_resolve(session->symtab, "malloc");
        uintptr_t free_addr = elf_symtab_resolve(session->symtab, "free");
        uintptr_t realloc_addr = elf_symtab_resolve(session->symtab, "realloc");

        if (malloc_addr) set_breakpoint(session, malloc_addr, "malloc");
        if (free_addr) set_breakpoint(session, free_addr, "free");
        if (realloc_addr) set_breakpoint(session, realloc_addr, "realloc");
    }

    /* Set user-requested breakpoints */
    if (session->symtab) {
        for (int i = 0; i < session->config.num_break_symbols; i++) {
            uintptr_t addr = elf_symtab_resolve(session->symtab,
                                                 session->config.break_symbols[i]);
            if (addr) {
                set_breakpoint(session, addr, session->config.break_symbols[i]);
            }
        }
    }

    /* Emit initial checkpoint */
    emit_event(session, PRIM_CHECKPOINT, 0, 0, "Process started");

    return 0;
}

int ptrace_session_step(ptrace_session_t *session) {
    if (!session || !session->running) return -1;

    if (ptrace(PTRACE_SINGLESTEP, session->child_pid, NULL, NULL) < 0) {
        return -1;
    }

    int status;
    if (waitpid(session->child_pid, &status, 0) < 0) return -1;

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        session->running = false;
        emit_event(session, PRIM_CHECKPOINT, 0, 0, "Process exited");
        return -1;
    }

    /* Check watch regions */
    check_watches(session);

    return 0;
}

int ptrace_session_continue(ptrace_session_t *session) {
    if (!session || !session->running) return -1;

    long request = session->config.trace_syscalls ?
                   PTRACE_SYSCALL : PTRACE_CONT;

    if (ptrace(request, session->child_pid, NULL, NULL) < 0) {
        return -1;
    }

    int status;
    if (waitpid(session->child_pid, &status, 0) < 0) return -1;

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        session->running = false;
        emit_event(session, PRIM_CHECKPOINT, 0, 0, "Process exited");
        return -1;
    }

    if (WIFSTOPPED(status)) {
        int sig = WSTOPSIG(status);

        /* Syscall stop (bit 7 set when PTRACE_O_TRACESYSGOOD) */
        if (sig == (SIGTRAP | 0x80)) {
            struct user_regs_struct regs;
            ptrace(PTRACE_GETREGS, session->child_pid, NULL, &regs);

            if (!session->in_syscall) {
                /* Syscall entry */
                session->in_syscall = true;

                /* Handle mmap/mprotect specially */
                int nr = (int)regs.orig_rax;
                if (nr == SYS_mmap) {
                    primitive_event_t evt = primitive_event_create(PRIM_MMAP);
                    evt.pid = session->child_pid;
                    evt.step_number = ++session->step_count;
                    evt.data.mmap.start = (void *)regs.rdi;
                    evt.data.mmap.length = regs.rsi;
                    evt.data.mmap.prot = (int)regs.rdx;
                    evt.data.mmap.flags = (int)regs.r10;
                    evt.description = "mmap";
                    event_bus_emit(&evt);
                } else if (nr == SYS_mprotect) {
                    primitive_event_t evt = primitive_event_create(PRIM_MPROTECT);
                    evt.pid = session->child_pid;
                    evt.step_number = ++session->step_count;
                    evt.data.mmap.start = (void *)regs.rdi;
                    evt.data.mmap.length = regs.rsi;
                    evt.data.mmap.prot = (int)regs.rdx;
                    evt.description = "mprotect";
                    event_bus_emit(&evt);
                } else if (session->config.trace_syscalls) {
                    emit_syscall_event(session, nr, regs.rip);
                }
            } else {
                /* Syscall exit */
                session->in_syscall = false;
            }
        }
        /* Breakpoint (SIGTRAP without bit 7) */
        else if (sig == SIGTRAP) {
            struct user_regs_struct regs;
            ptrace(PTRACE_GETREGS, session->child_pid, NULL, &regs);

            /* RIP is one past the INT3 */
            uintptr_t bp_addr = regs.rip - 1;
            breakpoint_t *bp = find_breakpoint(session, bp_addr);
            if (bp) {
                handle_breakpoint(session, bp);
            }
        }
    }

    /* Check watch regions after each stop */
    check_watches(session);

    return 0;
}

ssize_t ptrace_session_read_memory(ptrace_session_t *session,
                                    uintptr_t addr, void *buf, size_t len) {
    if (!session || session->child_pid <= 0) return -1;
    return read_memory(session->child_pid, addr, buf, len);
}

int ptrace_session_get_regs(ptrace_session_t *session, ptrace_regs_t *regs) {
    if (!session || !regs || session->child_pid <= 0) return -1;

    struct user_regs_struct uregs;
    if (ptrace(PTRACE_GETREGS, session->child_pid, NULL, &uregs) < 0) {
        return -1;
    }

    regs->rax = uregs.rax;
    regs->rbx = uregs.rbx;
    regs->rcx = uregs.rcx;
    regs->rdx = uregs.rdx;
    regs->rsi = uregs.rsi;
    regs->rdi = uregs.rdi;
    regs->rbp = uregs.rbp;
    regs->rsp = uregs.rsp;
    regs->r8 = uregs.r8;
    regs->r9 = uregs.r9;
    regs->r10 = uregs.r10;
    regs->r11 = uregs.r11;
    regs->r12 = uregs.r12;
    regs->r13 = uregs.r13;
    regs->r14 = uregs.r14;
    regs->r15 = uregs.r15;
    regs->rip = uregs.rip;
    regs->rflags = uregs.eflags;

    return 0;
}

bool ptrace_session_is_running(ptrace_session_t *session) {
    return session && session->running;
}

int ptrace_session_stop(ptrace_session_t *session) {
    if (!session || session->child_pid <= 0 || !session->running) return -1;
    return kill(session->child_pid, SIGSTOP);
}

pid_t ptrace_session_get_pid(ptrace_session_t *session) {
    return session ? session->child_pid : -1;
}

int ptrace_session_add_watch(ptrace_session_t *session,
                              uintptr_t addr, size_t size, const char *label) {
    if (!session || session->num_watches >= PTRACE_MAX_WATCHES) return -1;

    watch_region_t *w = &session->watches[session->num_watches];
    w->address = addr;
    w->size = size < 4096 ? size : 4096;
    snprintf(w->label, sizeof(w->label), "%s", label ? label : "watch");
    w->prev_data = calloc(1, w->size);
    w->has_prev = false;
    session->num_watches++;
    return 0;
}
