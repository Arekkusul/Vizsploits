#include "elf_symbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>

elf_symtab_t *elf_symtab_create(pid_t pid) {
    elf_symtab_t *tab = calloc(1, sizeof(elf_symtab_t));
    if (!tab) return NULL;
    tab->pid = pid;
    return tab;
}

void elf_symtab_destroy(elf_symtab_t *tab) {
    free(tab);
}

uintptr_t elf_symtab_resolve(const elf_symtab_t *tab, const char *name) {
    if (!tab || !name) return 0;
    for (int i = 0; i < tab->num_symbols; i++) {
        if (strcmp(tab->symbols[i].name, name) == 0) {
            return tab->symbols[i].address;
        }
    }
    return 0;
}

const char *elf_symtab_lookup(const elf_symtab_t *tab, uintptr_t addr) {
    if (!tab) return NULL;
    for (int i = 0; i < tab->num_symbols; i++) {
        uintptr_t sym_start = tab->symbols[i].address;
        uintptr_t sym_end = sym_start + tab->symbols[i].size;
        if (addr >= sym_start && addr < sym_end) {
            return tab->symbols[i].name;
        }
    }
    return NULL;
}

/* Read memory from a remote process using process_vm_readv */
static ssize_t read_remote(pid_t pid, uintptr_t addr, void *buf, size_t len) {
    struct iovec local = { .iov_base = buf, .iov_len = len };
    struct iovec remote = { .iov_base = (void *)addr, .iov_len = len };
    return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

/* Add a symbol to the table */
static void add_symbol(elf_symtab_t *tab, const char *name,
                        uintptr_t addr, size_t size) {
    if (tab->num_symbols >= ELF_MAX_SYMBOLS) return;
    elf_symbol_t *sym = &tab->symbols[tab->num_symbols];
    snprintf(sym->name, sizeof(sym->name), "%s", name);
    sym->address = addr;
    sym->size = size > 0 ? size : 16; /* Default size if unknown */
    tab->num_symbols++;
}

/**
 * Parse ELF dynamic symbol table from a loaded shared library.
 * base_addr is the load address from /proc/pid/maps.
 * path is the filesystem path to the library.
 * Only resolves symbols in wanted_names (NULL-terminated array).
 */
static int parse_elf_dynsym(elf_symtab_t *tab, pid_t pid,
                              uintptr_t base_addr, const char *path,
                              const char **wanted_names) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        close(fd);
        return 0;
    }

    /* Verify ELF magic */
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        close(fd);
        return 0;
    }

    /* Find .dynsym and .dynstr sections via section headers */
    Elf64_Shdr *shdrs = calloc(ehdr.e_shnum, sizeof(Elf64_Shdr));
    if (!shdrs) { close(fd); return 0; }

    lseek(fd, (off_t)ehdr.e_shoff, SEEK_SET);
    if (read(fd, shdrs, ehdr.e_shnum * sizeof(Elf64_Shdr)) !=
        (ssize_t)(ehdr.e_shnum * sizeof(Elf64_Shdr))) {
        free(shdrs);
        close(fd);
        return 0;
    }

    int resolved = 0;
    for (int si = 0; si < ehdr.e_shnum; si++) {
        if (shdrs[si].sh_type != SHT_DYNSYM) continue;

        Elf64_Shdr *dynsym_shdr = &shdrs[si];
        Elf64_Shdr *dynstr_shdr = &shdrs[dynsym_shdr->sh_link];

        size_t num_syms = dynsym_shdr->sh_size / sizeof(Elf64_Sym);
        Elf64_Sym *syms = malloc(dynsym_shdr->sh_size);
        char *strtab = malloc(dynstr_shdr->sh_size);
        if (!syms || !strtab) {
            free(syms); free(strtab);
            break;
        }

        lseek(fd, (off_t)dynsym_shdr->sh_offset, SEEK_SET);
        if (read(fd, syms, dynsym_shdr->sh_size) < 0) {
            free(syms); free(strtab); break;
        }
        lseek(fd, (off_t)dynstr_shdr->sh_offset, SEEK_SET);
        if (read(fd, strtab, dynstr_shdr->sh_size) < 0) {
            free(syms); free(strtab); break;
        }

        for (size_t i = 0; i < num_syms; i++) {
            if (syms[i].st_name == 0) continue;
            if (ELF64_ST_TYPE(syms[i].st_info) != STT_FUNC) continue;
            if (syms[i].st_value == 0) continue;

            const char *sym_name = strtab + syms[i].st_name;

            /* Check if this symbol is wanted */
            if (wanted_names) {
                bool wanted = false;
                for (const char **w = wanted_names; *w; w++) {
                    if (strcmp(sym_name, *w) == 0) {
                        wanted = true;
                        break;
                    }
                }
                if (!wanted) continue;
            }

            uintptr_t runtime_addr = base_addr + syms[i].st_value;

            /* Verify the address is readable in the target process */
            uint8_t test_byte;
            if (read_remote(pid, runtime_addr, &test_byte, 1) == 1) {
                add_symbol(tab, sym_name, runtime_addr, syms[i].st_size);
                resolved++;
            }
        }

        free(syms);
        free(strtab);
        break; /* Only process first .dynsym */
    }

    free(shdrs);
    close(fd);
    return resolved;
}

int elf_symtab_resolve_libc(elf_symtab_t *tab, pid_t pid) {
    if (!tab) return 0;

    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *fp = fopen(maps_path, "r");
    if (!fp) return 0;

    static const char *libc_symbols[] = {
        "malloc", "free", "realloc", "calloc",
        "mmap", "mmap64", "munmap", "mprotect",
        "read", "write", "open", "close",
        NULL
    };

    int total_resolved = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        /* Look for libc mapping */
        if (!strstr(line, "libc") || !strstr(line, "r-xp")) continue;

        uintptr_t base_addr = 0;
        char perms[8] = {0};
        unsigned long offset = 0;
        char path[256] = {0};

        unsigned long end_addr;
        int dev_major, dev_minor;
        unsigned long inode;
        if (sscanf(line, "%lx-%lx %7s %lx %d:%d %lu %255s",
                   &base_addr, &end_addr, perms, &offset,
                   &dev_major, &dev_minor, &inode, path) < 8) continue;
        (void)end_addr; (void)dev_major; (void)dev_minor; (void)inode;

        if (offset != 0) continue; /* Only the first mapping has offset 0 */

        total_resolved = parse_elf_dynsym(tab, pid, base_addr, path,
                                            libc_symbols);
        break;
    }

    fclose(fp);
    return total_resolved;
}
