#ifndef ELFFILE_H
#define ELFFILE_H

#include <stdint.h>
#include <stdio.h>
#include "risc.h"

#define RAM_SIZE 16384
#define EI_NIDENT 16
#define ELFCLASS32 1
#define ELFDATA2LSB 1

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

uint8_t *load_elf(const char *path, long *size);
void carica_elf_in_risc(RISC_V *risc, uint8_t *elf_content, Elf32_Ehdr *header, long size);
void check_elf(uint8_t *content, long size);

#endif
