#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elf.h"
#include "risc.h"

uint8_t *load_elf(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        perror("Errore apertura file");
        exit(EXIT_FAILURE);
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        perror("Errore fseek");
        fclose(f);
        exit(EXIT_FAILURE);
    }

    *size = ftell(f);
    if (*size == -1L) {
        perror("Errore ftell");
        fclose(f);
        exit(EXIT_FAILURE);
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        perror("Errore fseek");
        fclose(f);
        exit(EXIT_FAILURE);
    }

    uint8_t *buffer = malloc((size_t)*size);
    if (buffer == NULL) {
        perror("Errore allocazione memoria");
        fclose(f);
        exit(EXIT_FAILURE);
    }

    size_t bytes_read = fread(buffer, 1, (size_t)*size, f);
    if (bytes_read != (size_t)*size) {
        if (ferror(f)) {
            perror("Errore lettura file");
        } else {
            fprintf(stderr, "Errore: file letto solo parzialmente (%zu/%ld byte)\n",
                    bytes_read, *size);
        }

        free(buffer);
        fclose(f);
        exit(EXIT_FAILURE);
    }

    fclose(f);
    return buffer;
}


void carica_elf_in_risc(RISC_V *risc, uint8_t *elf_content, Elf32_Ehdr *header, long size) {
    Elf32_Shdr *sections = (Elf32_Shdr *)(elf_content + header->e_shoff);

    for (int i = 0; i < header->e_shnum; i++) {
        if (sections[i].sh_type == 1 && (sections[i].sh_flags & 2)) {
            /* in 64 bit: due campi a 32 bit possono sommare oltre 2^32 */
            if ((uint64_t)sections[i].sh_addr + sections[i].sh_size > sizeof(risc -> memory)) {
                printf("Avviso: Sezione %d ignorata (fuori RAM)\n", i);
                continue;
            }

            if ((uint64_t)sections[i].sh_offset + sections[i].sh_size > (uint64_t)size) {
                fprintf(stderr, "Errore: sezione %d fuori dal file\n", i);
                exit(EXIT_FAILURE);
            }

            uint8_t *ram_ptr = (uint8_t *)risc->memory;
            memcpy(ram_ptr + sections[i].sh_addr,
                   elf_content + sections[i].sh_offset,
                   sections[i].sh_size);
        }
    }
}

void check_elf(uint8_t *content, long size) {
    if (size < (long)sizeof(Elf32_Ehdr)) {
        fprintf(stderr, "Errore: file troppo corto per un header ELF32 (%ld byte)\n", size);
        exit(-1);
    }
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

    Elf32_Ehdr *h = (Elf32_Ehdr *)content;

    if ((uint64_t)h->e_shoff + (uint64_t)h->e_shnum * h->e_shentsize > (uint64_t)size) {
        fprintf(stderr, "Errore: tabella delle sezioni fuori dal file\n");
        exit(-1);
    }
}
