#include "event_bus.h"
#include "../exploits/api/exploit_api.h"
#include "../ui/tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    printf("  -h, --help       Show this help message\n");
    printf("  -l, --list       List available exploits\n");
    printf("  -e, --exploit N  Run exploit by number (non-interactive)\n");
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

// Event handler for headless mode
static void headless_event_handler(const primitive_event_t *evt, void *userdata) {
    (void)userdata;

    // Color codes for terminal
    const char *color_reset = "\033[0m";
    const char *color_green = "\033[32m";
    const char *color_yellow = "\033[33m";
    const char *color_red = "\033[31m";
    const char *color_cyan = "\033[36m";

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

    static struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"list",    no_argument,       0, 'l'},
        {"exploit", required_argument, 0, 'e'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "hle:", long_options, NULL)) != -1) {
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

    // Built-in exploits are auto-registered via constructor

    // Handle CLI modes
    if (list_only) {
        list_exploits_cli();
        exploit_api_cleanup();
        event_bus_cleanup();
        return 0;
    }

    if (exploit_num > 0) {
        run_exploit_headless(exploit_num);
        exploit_api_cleanup();
        event_bus_cleanup();
        return 0;
    }

    // Interactive TUI mode
    tui_t *tui = tui_init();
    if (!tui) {
        fprintf(stderr, "Failed to initialize TUI\n");
        exploit_api_cleanup();
        event_bus_cleanup();
        return 1;
    }

    // Run the main UI loop
    int result = tui_run(tui);

    // Cleanup
    tui_cleanup(tui);
    exploit_api_cleanup();
    event_bus_cleanup();

    return result;
}
