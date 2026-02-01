#include "event_bus.h"
#include "serialize.h"
#include "../education/lesson.h"
#include "../exploits/api/exploit_api.h"
#include "../instrumentation/ptrace/ptrace_backend.h"
#include "../scripting/lua_bridge.h"
#include "../ui/tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>

/**
 * Kernel Exploit Visualizer
 *
 * Phase 1 MVP - Interactive demo mode with step-by-step visualization
 */

static void print_usage(const char *prog) {
    printf("Kernel Exploit Visualizer - Phase 1 MVP\n");
    printf("\n");
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help          Show this help message\n");
    printf("  -l, --list          List available exploits\n");
    printf("  -e, --exploit N     Run exploit by number (non-interactive)\n");
    printf("  -r, --replay FILE   Replay a saved trace file in TUI\n");
    printf("  -s, --script FILE   Load a Lua exploit script\n");
    printf("  -d, --script-dir D  Load all Lua scripts from directory\n");
    printf("  -p, --ptrace BIN    Trace a binary with ptrace\n");
    printf("  -w, --watch A:S     Watch memory at addr:size (hex, with --ptrace)\n");
    printf("\n");
    printf("Interactive mode (default):\n");
    printf("  Use arrow keys to select exploit, Enter to run\n");
    printf("  Space to step through, R to run to checkpoint\n");
    printf("  Q to quit\n");
    printf("\n");
}

static void list_exploits_cli(void) {
    printf("\nAvailable Exploits:\n");
    printf("═══════════════════════════════════════════\n");

    int count = exploit_count();
    if (count == 0) {
        printf("  (no exploits registered)\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        exploit_t *exp = exploit_get_by_index(i);
        if (!exp) continue;

        printf("  %d. %s\n", i + 1, exp->meta.name);
        if (exp->meta.description) {
            printf("     %s\n", exp->meta.description);
        }
        printf("\n");
    }
}

// Helper to print hex preview with ASCII
static void print_hex_preview(const uint8_t *data, size_t len) {
    const char *color_dim = "\033[2m";
    const char *color_reset = "\033[0m";

    printf("\n%s         hex: ", color_dim);
    for (size_t i = 0; i < len && i < 16; i++) {
        printf("%02x ", data[i]);
    }
    if (len > 16) printf("...");

    printf("\n         ascii: \"");
    for (size_t i = 0; i < len && i < 16; i++) {
        char c = (char)data[i];
        if (c >= 32 && c < 127) {
            putchar(c);
        } else {
            putchar('.');
        }
    }
    if (len > 16) printf("...");
    printf("\"%s", color_reset);
}

// Event handler for headless mode
static void headless_event_handler(const primitive_event_t *evt, void *userdata) {
    (void)userdata;

    // Color codes for terminal
    const char *color_reset = "\033[0m";
    const char *color_green = "\033[32m";
    const char *color_yellow = "\033[33m";
    const char *color_red = "\033[31m";
    const char *color_cyan = "\033[36m";
    const char *color_magenta = "\033[35m";

    const char *color = color_reset;

    // Choose color based on event type
    switch (evt->type) {
        case PRIM_ALLOC:
            color = color_green;
            break;
        case PRIM_FREE:
            color = color_yellow;
            break;
        case PRIM_UAF:
        case PRIM_DOUBLE_FREE:
        case PRIM_OVERFLOW:
            color = color_red;
            break;
        case PRIM_CHECKPOINT:
            color = color_cyan;
            break;
        case PRIM_WRITE:
            color = color_magenta;
            break;
        default:
            break;
    }

    printf("%s[%3d] %-12s%s", color, evt->step_number,
           primitive_type_short(evt->type), color_reset);

    if (evt->address) {
        printf(" addr=0x%lx", (unsigned long)evt->address);
    }
    if (evt->size > 0) {
        printf(" size=%zu", evt->size);
    }

    if (evt->description) {
        printf(" | %s", evt->description);
    }

    // Show hex preview for WRITE events
    if (evt->type == PRIM_WRITE && evt->data.memop.len > 0) {
        print_hex_preview(evt->data.memop.preview, evt->data.memop.len);
    }

    printf("\n");
}

static void run_exploit_headless(int exploit_num) {
    exploit_t *exp = exploit_get_by_index(exploit_num - 1);
    if (!exp) {
        fprintf(stderr, "Error: Invalid exploit number: %d\n", exploit_num);
        return;
    }

    printf("Running exploit: %s\n", exp->meta.name);
    printf("═══════════════════════════════════════════\n\n");

    // Subscribe to print events
    event_bus_subscribe(PRIM_NONE, headless_event_handler, NULL);

    if (exp->setup) {
        exp->setup(exp);
    }

    if (exp->run) {
        exp->run(exp);
    }

    // Process all events
    event_bus_process();

    if (exp->cleanup) {
        exp->cleanup(exp);
    }

    printf("\n[*] Exploit complete.\n");
}

int main(int argc, char *argv[]) {
    int opt;
    int exploit_num = 0;
    bool list_only = false;
    const char *replay_file = NULL;
    const char *script_file = NULL;
    const char *script_dir = NULL;
    const char *ptrace_binary = NULL;
    ptrace_config_t ptrace_cfg = {0};
    ptrace_cfg.trace_malloc = true;

    static struct option long_options[] = {
        {"help",       no_argument,       0, 'h'},
        {"list",       no_argument,       0, 'l'},
        {"exploit",    required_argument, 0, 'e'},
        {"replay",     required_argument, 0, 'r'},
        {"script",     required_argument, 0, 's'},
        {"script-dir", required_argument, 0, 'd'},
        {"ptrace",     required_argument, 0, 'p'},
        {"watch",      required_argument, 0, 'w'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "hle:r:s:d:p:w:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;

            case 'l':
                list_only = true;
                break;

            case 'e':
                exploit_num = atoi(optarg);
                break;

            case 'r':
                replay_file = optarg;
                break;

            case 's':
                script_file = optarg;
                break;

            case 'd':
                script_dir = optarg;
                break;

            case 'p':
                ptrace_binary = optarg;
                ptrace_cfg.target_path = optarg;
                break;

            case 'w': {
                /* Parse addr:size (hex) */
                if (ptrace_cfg.num_watches < PTRACE_MAX_WATCHES) {
                    uintptr_t addr = 0;
                    size_t sz = 0;
                    if (sscanf(optarg, "%lx:%zu", &addr, &sz) == 2 ||
                        sscanf(optarg, "0x%lx:%zu", &addr, &sz) == 2) {
                        ptrace_watch_t *w = &ptrace_cfg.watches[ptrace_cfg.num_watches];
                        w->address = addr;
                        w->size = sz;
                        snprintf(w->label, sizeof(w->label), "watch_%d",
                                 ptrace_cfg.num_watches);
                        ptrace_cfg.num_watches++;
                    } else {
                        fprintf(stderr, "Invalid watch format: %s (expected addr:size)\n",
                                optarg);
                    }
                }
                break;
            }

            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Initialize subsystems
    if (event_bus_init() != 0) {
        fprintf(stderr, "Failed to initialize event bus\n");
        return 1;
    }

    if (exploit_api_init() != 0) {
        fprintf(stderr, "Failed to initialize exploit API\n");
        event_bus_cleanup();
        return 1;
    }

    lesson_init();
    lua_bridge_init();

    // Built-in exploits and lessons are auto-registered via constructors

    // Load Lua scripts if requested
    if (script_file) {
        if (lua_bridge_load_script(script_file) != 0) {
            fprintf(stderr, "Warning: Failed to load script: %s\n", script_file);
        }
    }
    if (script_dir) {
        int loaded = lua_bridge_load_directory(script_dir);
        if (loaded < 0) {
            fprintf(stderr, "Warning: Failed to load scripts from: %s\n", script_dir);
        }
    }

    // Handle CLI modes
    if (list_only) {
        list_exploits_cli();
        lua_bridge_cleanup();
        lesson_cleanup();
        exploit_api_cleanup();
        event_bus_cleanup();
        return 0;
    }

    if (exploit_num > 0) {
        run_exploit_headless(exploit_num);
        lua_bridge_cleanup();
        lesson_cleanup();
        exploit_api_cleanup();
        event_bus_cleanup();
        return 0;
    }

    // Replay mode: load trace file into TUI
    if (replay_file) {
        tui_t *tui = tui_init();
        if (!tui) {
            fprintf(stderr, "Failed to initialize TUI\n");
            lesson_cleanup();
            exploit_api_cleanup();
            event_bus_cleanup();
            return 1;
        }

        char exploit_name[128] = {0};
        int nevents = trace_load(replay_file, tui->timeline, exploit_name, sizeof(exploit_name));
        if (nevents < 0) {
            tui_cleanup(tui);
            fprintf(stderr, "Failed to load trace: %s\n", replay_file);
            lesson_cleanup();
            exploit_api_cleanup();
            event_bus_cleanup();
            return 1;
        }

        /* Start at step 0 in step mode */
        timeline_goto_step(tui->timeline, 0);
        tui->mode = UI_MODE_RUNNING;
        tui->step_mode = true;

        int result = tui_run(tui);

        tui_cleanup(tui);
        lua_bridge_cleanup();
        lesson_cleanup();
        exploit_api_cleanup();
        event_bus_cleanup();
        return result;
    }

    // Ptrace mode: trace a real binary
    if (ptrace_binary) {
        /* Collect remaining args as target argv */
        if (optind < argc) {
            ptrace_cfg.argc = argc - optind + 1;
            ptrace_cfg.argv = calloc((size_t)(ptrace_cfg.argc + 1), sizeof(char *));
            ptrace_cfg.argv[0] = (char *)ptrace_binary;
            for (int i = optind; i < argc; i++) {
                ptrace_cfg.argv[i - optind + 1] = argv[i];
            }
        }

        ptrace_session_t *ps = ptrace_session_create(&ptrace_cfg);
        if (!ps) {
            fprintf(stderr, "Failed to create ptrace session\n");
            free(ptrace_cfg.argv);
            lua_bridge_cleanup();
            lesson_cleanup();
            exploit_api_cleanup();
            event_bus_cleanup();
            return 1;
        }

        tui_t *tui = tui_init();
        if (!tui) {
            fprintf(stderr, "Failed to initialize TUI\n");
            ptrace_session_destroy(ps);
            free(ptrace_cfg.argv);
            lua_bridge_cleanup();
            lesson_cleanup();
            exploit_api_cleanup();
            event_bus_cleanup();
            return 1;
        }

        /* Subscribe TUI to events from ptrace */
        event_bus_subscribe(PRIM_NONE, tui_on_event, tui);

        /* Start the traced process */
        if (ptrace_session_start(ps) < 0) {
            fprintf(stderr, "Failed to start ptrace session\n");
            tui_cleanup(tui);
            ptrace_session_destroy(ps);
            free(ptrace_cfg.argv);
            lua_bridge_cleanup();
            lesson_cleanup();
            exploit_api_cleanup();
            event_bus_cleanup();
            return 1;
        }

        /* Store ptrace session in TUI for access during run loop */
        tui->ptrace_session = ps;
        tui->ptrace_mode = true;
        tui->mode = UI_MODE_RUNNING;
        tui->step_mode = true;

        int result = tui_run(tui);

        tui_cleanup(tui);
        ptrace_session_destroy(ps);
        free(ptrace_cfg.argv);
        lua_bridge_cleanup();
        lesson_cleanup();
        exploit_api_cleanup();
        event_bus_cleanup();
        return result;
    }

    // Interactive TUI mode
    tui_t *tui = tui_init();
    if (!tui) {
        fprintf(stderr, "Failed to initialize TUI\n");
        lua_bridge_cleanup();
        lesson_cleanup();
        exploit_api_cleanup();
        event_bus_cleanup();
        return 1;
    }

    // Run the main UI loop
    int result = tui_run(tui);

    // Cleanup
    tui_cleanup(tui);
    lua_bridge_cleanup();
    lesson_cleanup();
    exploit_api_cleanup();
    event_bus_cleanup();

    return result;
}
