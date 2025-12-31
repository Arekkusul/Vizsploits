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

static void run_exploit_headless(int exploit_num) {
    exploit_t *exp = exploit_get_by_index(exploit_num - 1);
    if (!exp) {
        fprintf(stderr, "Error: Invalid exploit number: %d\n", exploit_num);
        return;
    }

    printf("Running exploit: %s\n", exp->meta.name);
    printf("═══════════════════════════════════════════\n\n");

    // Subscribe to print events
    event_bus_subscribe(PRIM_NONE, NULL, NULL);  // Just process them

    if (exp->setup) {
        printf("[*] Setup...\n");
        exp->setup(exp);
    }

    if (exp->run) {
        printf("[*] Running...\n");
        exp->run(exp);
    }

    // Process events and print them
    printf("\n[*] Events:\n");
    // In headless mode, events were emitted but we'd need a handler to print them
    // For now, the exploit itself prints progress

    if (exp->cleanup) {
        printf("\n[*] Cleanup...\n");
        exp->cleanup(exp);
    }

    printf("\n[*] Done.\n");
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
