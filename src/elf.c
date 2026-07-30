#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elf.h"
#include "core.h"

uint8_t *load_elf(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("Errore apertura file");
        exit(-1);
    }

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buffer = malloc(*size);
    fread(buffer, 1, *size, f);
    fclose(f);
    return buffer;
}

void carica_elf_in_core(RISC_V *core, uint8_t *elf_content, Elf32_Ehdr *header) {
    Elf32_Shdr *sections = (Elf32_Shdr *)(elf_content + header->e_shoff);

    for (int i = 0; i < header->e_shnum; i++) {
        if (sections[i].sh_type == 1 && (sections[i].sh_flags & 2)) {
            if (sections[i].sh_addr + sections[i].sh_size > RAM_SIZE) {
                printf("Avviso: Sezione %d ignorata (fuori RAM)\n", i);
                continue;
            }
            uint8_t *ram_ptr = (uint8_t *)core->memory;
            memcpy(ram_ptr + sections[i].sh_addr,
                   elf_content + sections[i].sh_offset,
                   sections[i].sh_size);
        }
    }
}

void check_elf(uint8_t *content) {
    if (content[0] != 0x7F || content[1] != 'E' || content[2] != 'L' ||
        content[3] != 'F') {
        fprintf(stderr, "Errore: Non è un file ELF valido\n");
        exit(-1);
    }
    if (content[4] != 1) {
        fprintf(stderr, "Errore: Atteso ELF a 32 bit\n");
        exit(-1);
    }
    if (content[5] != 1) {
        fprintf(stderr, "Errore: Atteso Little-Endian\n");
        exit(-1);
    }
}
