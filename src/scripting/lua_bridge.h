#ifndef LUA_BRIDGE_H
#define LUA_BRIDGE_H

#include <stdbool.h>

/**
 * Initialize the Lua scripting bridge.
 * Returns 0 on success, -1 if Lua support is not compiled in.
 */
int lua_bridge_init(void);

/**
 * Cleanup the Lua scripting bridge.
 */
void lua_bridge_cleanup(void);

/**
 * Load a single Lua exploit script.
 * The script is expected to define:
 *   exploit = { name = "...", description = "...", primitives = {...} }
 *   function setup() ... end
 *   function run() ... end
 *   function cleanup() ... end (optional)
 *
 * Returns 0 on success, -1 on error.
 */
int lua_bridge_load_script(const char *path);

/**
 * Load all .lua files from a directory.
 * Returns number of scripts loaded, or -1 on error.
 */
int lua_bridge_load_directory(const char *dir_path);

/**
 * Check if Lua support is available.
 */
bool lua_bridge_available(void);

#endif /* LUA_BRIDGE_H */
