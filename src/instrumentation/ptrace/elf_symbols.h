#ifndef ELF_SYMBOLS_H
#define ELF_SYMBOLS_H

#include <stdint.h>
#include <sys/types.h>

/**
 * Resolved symbol information
 */
typedef struct {
    char name[128];
    uintptr_t address;
    size_t size;
} elf_symbol_t;

#define ELF_MAX_SYMBOLS 128

/**
 * Symbol table for a traced process
 */
typedef struct {
    elf_symbol_t symbols[ELF_MAX_SYMBOLS];
    int num_symbols;
    pid_t pid;
} elf_symtab_t;

/**
 * Create a symbol table for a process.
 * Parses /proc/<pid>/maps and ELF headers to resolve symbols.
 */
elf_symtab_t *elf_symtab_create(pid_t pid);

/**
 * Destroy a symbol table.
 */
void elf_symtab_destroy(elf_symtab_t *tab);

/**
 * Resolve a symbol name to an address.
 * Returns 0 if not found.
 */
uintptr_t elf_symtab_resolve(const elf_symtab_t *tab, const char *name);

/**
 * Find the symbol name for an address.
 * Returns NULL if not found.
 */
const char *elf_symtab_lookup(const elf_symtab_t *tab, uintptr_t addr);

/**
 * Resolve common libc symbols (malloc, free, realloc, etc.)
 * by parsing /proc/<pid>/maps for libc base and reading its symbol table.
 * Returns number of symbols resolved.
 */
int elf_symtab_resolve_libc(elf_symtab_t *tab, pid_t pid);

#endif /* ELF_SYMBOLS_H */
