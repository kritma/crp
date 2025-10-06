
#include <assert.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    void *ptr;
    uint32_t size;
} Data;

int main(int argc, char **argv) {

    const uint32_t data_len = argc - 2;

    Data *data = calloc(data_len, sizeof(Data));

    const char local_symbols_str[] = "\0ltmp1\0ltmp0";
    uint32_t names_length = sizeof(local_symbols_str);
    for (uint32_t i = 1; i < argc - 1; i++) {
        FILE *f = fopen(argv[i], "rb");
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        fseek(f, 0, SEEK_SET);

        uint8_t *buf = malloc(file_size);
        fread(buf, 1, file_size, f);
        data[i - 1].ptr = buf;
        data[i - 1].size = file_size;
        data[i - 1].name = argv[i];
        fclose(f);

        names_length += strlen(argv[i]) + 2;
    }

    const uint32_t aligned_names_length =
        (names_length + sizeof(long) + 1) / sizeof(long) * sizeof(long);

    const uint32_t align = 2;
    const uint32_t alignment = 1 << align; // align = 2
    uint32_t data_aligned_size = 0;
    for (uint32_t i = 0; i < data_len; i++) {
        data_aligned_size +=
            (data[i].size + alignment - 1) / alignment * alignment;
    }

    const uint32_t sizeofcmds =
        sizeof(struct segment_command_64) + sizeof(struct section_64) * 2 +
        sizeof(struct build_version_command) + sizeof(struct symtab_command) +
        sizeof(struct dysymtab_command);
    const uint32_t data_offset = sizeof(struct mach_header_64) + sizeofcmds;
    const uint32_t sym_table_offset =
        data_offset + (data_aligned_size + sizeof(long) - 1) / sizeof(long) *
                          sizeof(long); // todo: calc % 8
    const uint32_t sym_str_offset =
        sym_table_offset + sizeof(struct nlist_64) * (data_len + 2);

    FILE *out = fopen(argv[argc - 1], "w");

    struct mach_header_64 header = {
        .magic = MH_MAGIC_64,
        .cputype = CPU_TYPE_ARM64,
        .cpusubtype = CPU_SUBTYPE_ARM64_ALL,
        .filetype = MH_OBJECT,
        .ncmds = 4,
        .sizeofcmds = sizeofcmds,
        .flags = MH_SUBSECTIONS_VIA_SYMBOLS,
    };
    fwrite(&header, sizeof(struct mach_header_64), 1, out);

    assert((sizeof(struct section_64) * 2 + sizeof(struct segment_command_64)) %
               sizeof(long) ==
           0);

    struct segment_command_64 load_command_segment = {
        .cmd = LC_SEGMENT_64,
        .cmdsize = sizeof(struct section_64) * 2 +
                   sizeof(struct segment_command_64), // must be a multiple of
                                                      // sizeof(long) The
                                                      // padded bytes must be
                                                      // zero-filled.
        .segname = {},
        .vmaddr = 0,
        .vmsize = data_aligned_size,
        .fileoff = data_offset,
        .filesize = data_aligned_size,
        .maxprot = VM_PROT_ALL,
        .initprot = VM_PROT_ALL,
        .nsects = 2,
        .flags = 0,
    };
    fwrite(&load_command_segment, sizeof(struct segment_command_64), 1, out);

    struct section_64 section_text = {
        .sectname = SECT_TEXT,
        .segname = SEG_TEXT,
        .addr = 0,
        .size = 0,
        .offset = data_offset,
        .align = 0,
        .reloff = 0,
        .nreloc = 0,
        .flags = S_ATTR_PURE_INSTRUCTIONS,
        .reserved1 = 0,
        .reserved2 = 0,
        .reserved3 = 0,
    };
    fwrite(&section_text, sizeof(struct section_64), 1, out);

    struct section_64 section_data = {
        .sectname = SECT_DATA,
        .segname = SEG_DATA,
        .addr = 0,
        .size = data_aligned_size,
        .offset = data_offset,
        .align = align,
        .reloff = 0,
        .nreloc = 0,
        .flags = 0,
        .reserved1 = 0,
        .reserved2 = 0,
        .reserved3 = 0,
    };
    fwrite(&section_data, sizeof(struct section_64), 1, out);

    {
        struct build_version_command command_build_version = {
            .cmd = LC_BUILD_VERSION,
            .cmdsize = sizeof(struct build_version_command),
            .platform = PLATFORM_MACOS,
            .minos = 0x000e0000,
            .sdk = 0x000f0200,
            .ntools = 0,
        };
        fwrite(&command_build_version, sizeof(struct build_version_command), 1,
               out);
    }

    struct symtab_command load_command_symtab = {
        .cmd = LC_SYMTAB,
        .cmdsize = sizeof(struct symtab_command),
        .symoff = sym_table_offset,
        .nsyms = data_len + 2,
        .stroff = sym_str_offset,
        .strsize = aligned_names_length,
    };
    fwrite(&load_command_symtab, sizeof(struct symtab_command), 1, out);

    struct dysymtab_command load_command_dysymtab = {
        .cmd = LC_DYSYMTAB,
        .cmdsize = sizeof(struct dysymtab_command),
        .ilocalsym = 0,
        .nlocalsym = 2,
        .iextdefsym = 2,
        .nextdefsym = data_len,
        .iundefsym = data_len + 2,
        .nundefsym = 0,
        .tocoff = 0,
        .ntoc = 0,
        .modtaboff = 0,
        .nmodtab = 0,
        .extrefsymoff = 0,
        .nextrefsyms = 0,
        .indirectsymoff = 0,
        .nindirectsyms = 0,
        .extreloff = 0,
        .nextrel = 0,
        .locreloff = 0,
        .nlocrel = 0,
    };
    fwrite(&load_command_dysymtab, sizeof(struct dysymtab_command), 1, out);

    for (uint32_t i = 0; i < data_len; i++) {
        fwrite(data[i].ptr, 1, data[i].size, out);
        if (data[i].size <
            (data[i].size + alignment - 1) / alignment * alignment) {
            const uint8_t tmp[8] = {};
            fwrite(tmp, 1,
                   (data[i].size + alignment - 1) / alignment * alignment -
                       data[i].size,
                   out);
        }
    }

    if (data_aligned_size + data_offset < sym_table_offset) {
        const uint8_t tmp[8] = {};
        fwrite(tmp, 1, sym_table_offset - data_aligned_size - data_offset, out);
    }

    struct nlist_64 local_symbols[] = {
        {
         .n_un.n_strx = 1,
         .n_type = N_TYPE & N_SECT,
         .n_sect = 1,
         .n_desc = 0,
         .n_value = 0,
         },
        {
         .n_un.n_strx = 7,
         .n_type = N_TYPE & N_SECT,
         .n_sect = 2,
         .n_desc = 0,
         .n_value = 0,
         }
    };

    struct nlist_64 *symbols_table = calloc(sizeof(struct nlist_64), data_len);
    uint32_t current_pos = 13;
    uint32_t current_offset = 0;
    for (uint32_t i = 0; i < data_len; i++) {
        symbols_table[i] = (struct nlist_64){
            .n_un.n_strx = current_pos,
            .n_type = N_TYPE & N_SECT | N_EXT,
            .n_sect = 2,
            .n_desc = current_offset,
            .n_value = 0,
        };
        current_pos += strlen(data[i].name) + 2;
        current_offset +=
            (data[i].size + alignment - 1) / alignment * alignment;
    }

    fwrite(local_symbols, sizeof(struct nlist_64), 2, out);
    fwrite(symbols_table, sizeof(struct nlist_64), data_len, out);

    fwrite(local_symbols_str, 1, sizeof(local_symbols_str), out);
    for (uint32_t i = 0; i < data_len; i++) {
        fputc('_', out);
        fwrite(data[i].name, 1, strlen(data[i].name) + 1, out);
    }

    if (names_length < aligned_names_length) {
        const uint8_t tmp[8] = {0};
        fwrite(tmp, 1, aligned_names_length - names_length, out);
    }
    fflush(out);
    fclose(out);
    printf("good\n");
}
