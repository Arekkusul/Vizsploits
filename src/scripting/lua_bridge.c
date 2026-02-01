#include "lua_bridge.h"

#ifdef HAS_LUA

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "../exploits/api/exploit_api.h"
#include "../core/event_bus.h"

#define MAX_LUA_EXPLOITS 32

typedef struct {
    lua_State *L;
    char name[128];
    char description[256];
    exploit_t exploit;
} lua_exploit_t;

static lua_exploit_t g_lua_exploits[MAX_LUA_EXPLOITS];
static int g_num_lua_exploits = 0;
static bool g_initialized = false;

/* ===== Lua C bindings: viz.* API ===== */

static int l_viz_alloc(lua_State *L) {
    size_t size = (size_t)luaL_checkinteger(L, 1);
    const char *desc = luaL_optstring(L, 2, "Lua alloc");
    void *ptr = prim_alloc(size, desc);
    lua_pushlightuserdata(L, ptr);
    return 1;
}

static int l_viz_free(lua_State *L) {
    void *ptr = lua_touserdata(L, 1);
    const char *desc = luaL_optstring(L, 2, "Lua free");
    prim_free(ptr, desc);
    return 0;
}

static int l_viz_realloc(lua_State *L) {
    void *ptr = lua_touserdata(L, 1);
    size_t size = (size_t)luaL_checkinteger(L, 2);
    const char *desc = luaL_optstring(L, 3, "Lua realloc");
    void *new_ptr = prim_realloc(ptr, size, desc);
    lua_pushlightuserdata(L, new_ptr);
    return 1;
}

static int l_viz_write(lua_State *L) {
    void *dst = lua_touserdata(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    const char *desc = luaL_optstring(L, 3, "Lua write");
    prim_write(dst, data, len, desc);
    return 0;
}

static int l_viz_read(lua_State *L) {
    void *src = lua_touserdata(L, 1);
    size_t len = (size_t)luaL_checkinteger(L, 2);
    const char *desc = luaL_optstring(L, 3, "Lua read");
    prim_read(src, len, desc);
    return 0;
}

static int l_viz_use_after_free(lua_State *L) {
    void *ptr = lua_touserdata(L, 1);
    const char *desc = luaL_optstring(L, 2, "Lua UAF");
    prim_use_after_free(ptr, desc);
    return 0;
}

static int l_viz_double_free(lua_State *L) {
    void *ptr = lua_touserdata(L, 1);
    const char *desc = luaL_optstring(L, 2, "Lua double-free");
    prim_double_free(ptr, desc);
    return 0;
}

static int l_viz_overflow(lua_State *L) {
    void *buf = lua_touserdata(L, 1);
    size_t buf_size = (size_t)luaL_checkinteger(L, 2);
    size_t write_size = (size_t)luaL_checkinteger(L, 3);
    const char *desc = luaL_optstring(L, 4, "Lua overflow");
    prim_overflow(buf, buf_size, write_size, desc);
    return 0;
}

static int l_viz_checkpoint(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    const char *desc = luaL_optstring(L, 2, "Checkpoint");
    prim_checkpoint(id, desc);
    return 0;
}

static int l_viz_note(lua_State *L) {
    const char *desc = luaL_checkstring(L, 1);
    prim_note(desc);
    return 0;
}

static int l_viz_call(lua_State *L) {
    const char *func = luaL_checkstring(L, 1);
    const char *desc = luaL_optstring(L, 2, "Lua call");
    prim_call(func, desc);
    return 0;
}

static int l_viz_ret(lua_State *L) {
    const char *func = luaL_checkstring(L, 1);
    const char *desc = luaL_optstring(L, 2, "Lua return");
    prim_return(func, desc);
    return 0;
}

static const luaL_Reg viz_lib[] = {
    {"alloc",          l_viz_alloc},
    {"free",           l_viz_free},
    {"realloc",        l_viz_realloc},
    {"write",          l_viz_write},
    {"read",           l_viz_read},
    {"use_after_free", l_viz_use_after_free},
    {"double_free",    l_viz_double_free},
    {"overflow",       l_viz_overflow},
    {"checkpoint",     l_viz_checkpoint},
    {"note",           l_viz_note},
    {"call",           l_viz_call},
    {"ret",            l_viz_ret},
    {NULL, NULL}
};

static int luaopen_viz(lua_State *L) {
    luaL_newlib(L, viz_lib);
    return 1;
}

/* ===== Exploit lifecycle callbacks ===== */

static void lua_exploit_setup(exploit_t *exp) {
    lua_exploit_t *le = (lua_exploit_t *)exp->user_data;
    if (!le || !le->L) return;

    lua_getglobal(le->L, "setup");
    if (lua_isfunction(le->L, -1)) {
        if (lua_pcall(le->L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "Lua setup error: %s\n", lua_tostring(le->L, -1));
            lua_pop(le->L, 1);
        }
    } else {
        lua_pop(le->L, 1);
    }
}

static void lua_exploit_run(exploit_t *exp) {
    lua_exploit_t *le = (lua_exploit_t *)exp->user_data;
    if (!le || !le->L) return;

    lua_getglobal(le->L, "run");
    if (lua_isfunction(le->L, -1)) {
        if (lua_pcall(le->L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "Lua run error: %s\n", lua_tostring(le->L, -1));
            lua_pop(le->L, 1);
        }
    } else {
        lua_pop(le->L, 1);
    }
}

static void lua_exploit_cleanup(exploit_t *exp) {
    lua_exploit_t *le = (lua_exploit_t *)exp->user_data;
    if (!le || !le->L) return;

    lua_getglobal(le->L, "cleanup");
    if (lua_isfunction(le->L, -1)) {
        if (lua_pcall(le->L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "Lua cleanup error: %s\n", lua_tostring(le->L, -1));
            lua_pop(le->L, 1);
        }
    } else {
        lua_pop(le->L, 1);
    }
}

/* ===== Public API ===== */

int lua_bridge_init(void) {
    g_initialized = true;
    g_num_lua_exploits = 0;
    return 0;
}

void lua_bridge_cleanup(void) {
    for (int i = 0; i < g_num_lua_exploits; i++) {
        if (g_lua_exploits[i].L) {
            lua_close(g_lua_exploits[i].L);
            g_lua_exploits[i].L = NULL;
        }
    }
    g_num_lua_exploits = 0;
    g_initialized = false;
}

int lua_bridge_load_script(const char *path) {
    if (!g_initialized || !path) return -1;
    if (g_num_lua_exploits >= MAX_LUA_EXPLOITS) return -1;

    lua_exploit_t *le = &g_lua_exploits[g_num_lua_exploits];
    memset(le, 0, sizeof(*le));

    le->L = luaL_newstate();
    if (!le->L) return -1;

    luaL_openlibs(le->L);

    /* Register viz library */
    luaL_requiref(le->L, "viz", luaopen_viz, 1);
    lua_pop(le->L, 1);

    /* Load and execute the script */
    if (luaL_dofile(le->L, path) != LUA_OK) {
        fprintf(stderr, "Lua load error (%s): %s\n", path, lua_tostring(le->L, -1));
        lua_close(le->L);
        le->L = NULL;
        return -1;
    }

    /* Read exploit table */
    lua_getglobal(le->L, "exploit");
    if (!lua_istable(le->L, -1)) {
        fprintf(stderr, "Lua script %s: missing 'exploit' table\n", path);
        lua_close(le->L);
        le->L = NULL;
        return -1;
    }

    lua_getfield(le->L, -1, "name");
    const char *name = lua_tostring(le->L, -1);
    if (name) {
        snprintf(le->name, sizeof(le->name), "%s", name);
    } else {
        snprintf(le->name, sizeof(le->name), "Lua Script %d", g_num_lua_exploits + 1);
    }
    lua_pop(le->L, 1);

    lua_getfield(le->L, -1, "description");
    const char *desc = lua_tostring(le->L, -1);
    if (desc) {
        snprintf(le->description, sizeof(le->description), "%s", desc);
    }
    lua_pop(le->L, 1);

    lua_pop(le->L, 1); /* pop exploit table */

    /* Set up exploit_t wrapper */
    le->exploit.meta.name = le->name;
    le->exploit.meta.description = le->description;
    le->exploit.meta.author = "Lua Script";
    le->exploit.setup = lua_exploit_setup;
    le->exploit.run = lua_exploit_run;
    le->exploit.cleanup = lua_exploit_cleanup;
    le->exploit.user_data = le;

    /* Register with exploit system */
    exploit_register(&le->exploit);

    g_num_lua_exploits++;
    return 0;
}

int lua_bridge_load_directory(const char *dir_path) {
    if (!dir_path) return -1;

    DIR *dir = opendir(dir_path);
    if (!dir) return -1;

    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        size_t namelen = strlen(ent->d_name);
        if (namelen < 4) continue;
        if (strcmp(ent->d_name + namelen - 4, ".lua") != 0) continue;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, ent->d_name);

        if (lua_bridge_load_script(fullpath) == 0) {
            loaded++;
        }
    }

    closedir(dir);
    return loaded;
}

bool lua_bridge_available(void) {
    return true;
}

#else /* !HAS_LUA */

#include <stdio.h>

int lua_bridge_init(void) {
    return 0;
}

void lua_bridge_cleanup(void) {
}

int lua_bridge_load_script(const char *path) {
    (void)path;
    fprintf(stderr, "Lua support not compiled in. Install liblua5.4-dev and rebuild.\n");
    return -1;
}

int lua_bridge_load_directory(const char *dir_path) {
    (void)dir_path;
    return 0;
}

bool lua_bridge_available(void) {
    return false;
}

#endif /* HAS_LUA */
