#include "dump.h"
#include "sds.c"
#include <ctype.h>
#include <libgen.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define Asset_FIELDS(X)                                                        \
    X(sds, file_path)                                                          \
    X(sds, var_name)                                                           \
    X(sds, var_size_name)                                                      \
    X(void *, content)                                                         \
    X(uint64_t, size)
DECLARE_STRUCT(Asset);

typedef struct {
    sds file_path;
    bool add_zero_at_the_end;
    uint64_t *out_file_size;
} fread_all_args;

uint8_t *fread_all_fn(fread_all_args args) {
    FILE *file = fopen(args.file_path, "rb");
    fseek(file, 0, SEEK_END);
    uint64_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *buf = malloc(file_size + args.add_zero_at_the_end);
    fread(buf, 1, file_size, file);
    fclose(file);
    if (args.add_zero_at_the_end) {
        buf[file_size] = 0;
    }
    if (args.out_file_size) {
        *args.out_file_size = file_size + args.add_zero_at_the_end;
    }
    return buf;
}

#define fread_all(...) fread_all_fn((fread_all_args){__VA_ARGS__})

uint64_t ceil_to_alignment(uint64_t cur, uint64_t alignment) {
    return (cur + alignment - 1) / alignment * alignment;
}

typedef struct {
    FILE *file;
    uint64_t cur;
    uint8_t alignment;
} fill_to_alignment_args;

void fill_to_alignment_fn(fill_to_alignment_args args) {
    uint64_t to = ceil_to_alignment(args.cur, args.alignment);
    if (args.cur < to) {
        const uint8_t tmp[8] = {};
        fwrite(tmp, 1, to - args.cur, args.file);
    }
}

#define fill_to_alignment(...)                                                 \
    fill_to_alignment_fn((fill_to_alignment_args){__VA_ARGS__})

Asset *load_assets(sds config_file_path, uint32_t *out_count) {
    uint64_t config_size;
    uint8_t *config =
        fread_all(.file_path = config_file_path, .add_zero_at_the_end = true,
                  .out_file_size = &config_size);

    uint32_t lines_count;
    sds *assets_configs =
        sdssplitlen(config, config_size, "\n", 1, &lines_count);

    uint32_t assets_count = 0;
    for (uint32_t i = 0; i < lines_count; i++) {
        sds trimmed = sdstrim(sdsdup(assets_configs[i]), " ");
        if (sdscmp(trimmed, sdsempty()) != 0) {
            assets_count += 1;
        }
        sdsfree(trimmed);
    }
    *out_count = assets_count;

    Asset *assets = calloc(assets_count, sizeof(Asset));
    uint32_t asset_index = 0;
    for (uint32_t i = 0; i < lines_count; i++) {
        sds trimmed = sdstrim(sdsdup(assets_configs[i]), " ");
        if (sdscmp(trimmed, sdsempty()) == 0) {
            sdsfree(trimmed);
            continue;
        }
        sdsfree(trimmed);

        uint32_t parameters_count;
        sds *parameters = sdssplitargs(assets_configs[i], &parameters_count);

        sds file_path = sdsdup(parameters[0]);

        char type = 'b';
        if (parameters_count > 1) {
            type = parameters[1][0];
        }

        uint64_t size;
        uint8_t *content = fread_all(.file_path = file_path,
                                     .add_zero_at_the_end = type == 's',
                                     .out_file_size = &size);

        sds var_name;
        if (parameters_count > 2) {
            sds tmp = sdsdup(parameters[2]);
            var_name = sdscat(sdsnew("_"), tmp);
            sdsfree(tmp);
        } else {
            sds tmp = sdsdup(parameters[0]);
            var_name = sdscat(sdsnew("_"), basename(tmp));
            sdsfree(tmp);
            for (int i = 0; i < sdslen(var_name); i++) {
                if (!isalnum(var_name[i])) {
                    var_name[i] = '_';
                }
            }
        }

        sds var_size_name;
        if (parameters_count > 3) {
            var_size_name = sdscat(sdsnew("_"), parameters[3]);
        } else {
            var_size_name = sdscatfmt(sdsempty(), "%S_len", var_name);
        }

        assets[asset_index] = (Asset){
            .file_path = file_path,
            .content = content,
            .size = size,
            .var_name = var_name,
            .var_size_name = var_size_name,
        };

        asset_index += 1;
        sdsfreesplitres(parameters, parameters_count);
    }
    sdsfreesplitres(assets_configs, lines_count);
    return assets;
}

typedef struct {
    sds output_file;
    sds config_file;
    bool quiet;
} Settings;

Settings parse_args(int argc, char **argv) {
    Settings settings = {
        .config_file = sdsnew("crp.conf"),
        .output_file = sdsnew("assets.o"),
        .quiet = false,
    };

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'q':
                settings.quiet = true;
                break;
            case 'c':
                i++;
                sdsfree(settings.config_file);
                settings.config_file = sdsnew(argv[i]);
                break;
            }
        } else {
            sdsfree(settings.output_file);
            settings.output_file = sdsnew(argv[i]);
        }
    }
    return settings;
}

int main(int argc, char **argv) {
    uint32_t assets_count;

    Settings settings = parse_args(argc, argv);
    Asset *assets = load_assets(settings.config_file, &assets_count);
    if (!settings.quiet) {
        printf("assets count: %d\n", assets_count);
        for (uint32_t i = 0; i < assets_count; i++) {
            printf("%d:", i);
            DUMP(assets[i], Asset);
        }
    }

    const char local_symbols_str[] = "\0ltmp1\0ltmp0";
    uint32_t symbol_names_length = sizeof(local_symbols_str);
    for (uint32_t i = 0; i < assets_count; i++) {
        symbol_names_length += sdslen(assets[i].var_name) + 1 +
                               sdslen(assets[i].var_size_name) + 1;
    }

    const uint32_t align = 2;
    const uint32_t alignment = 1 << align; // align = 2
    uint64_t assets_content_aligned_size = 0;
    for (uint32_t i = 0; i < assets_count; i++) {
        assets_content_aligned_size +=
            ceil_to_alignment(assets[i].size, alignment);
        assets_content_aligned_size +=
            ceil_to_alignment(sizeof(assets[i].size), alignment);
    }

    const uint32_t sizeofcmds =
        sizeof(struct segment_command_64) + sizeof(struct section_64) * 2 +
        sizeof(struct build_version_command) + sizeof(struct symtab_command) +
        sizeof(struct dysymtab_command);
    const uint32_t data_offset = sizeof(struct mach_header_64) + sizeofcmds;
    const uint32_t sym_table_offset =
        data_offset + ceil_to_alignment(assets_content_aligned_size,
                                        sizeof(long)); // todo: calc % 8
    const uint32_t sym_str_offset =
        sym_table_offset + sizeof(struct nlist_64) * (assets_count * 2 + 2);

    FILE *out_object_file = fopen(settings.output_file, "wb");

    {
        struct mach_header_64 m_header = {
            .magic = MH_MAGIC_64,
            .cputype = CPU_TYPE_ARM64,
            .cpusubtype = CPU_SUBTYPE_ARM64_ALL,
            .filetype = MH_OBJECT,
            .ncmds = 4,
            .sizeofcmds = sizeofcmds,
            .flags = MH_SUBSECTIONS_VIA_SYMBOLS,
        };
        fwrite(&m_header, sizeof(struct mach_header_64), 1, out_object_file);
    }
    {
        struct segment_command_64 load_command_segment = {
            .cmd = LC_SEGMENT_64,
            .cmdsize = (sizeof(struct section_64) * 2 +
                        sizeof(struct segment_command_64)),
            .segname = {},
            .vmaddr = 0,
            .vmsize = assets_content_aligned_size,
            .fileoff = data_offset,
            .filesize = assets_content_aligned_size,
            .maxprot = VM_PROT_ALL,
            .initprot = VM_PROT_ALL,
            .nsects = 2,
            .flags = 0,
        };
        fwrite(&load_command_segment, sizeof(struct segment_command_64), 1,
               out_object_file);
    }
    {
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
        fwrite(&section_text, sizeof(struct section_64), 1, out_object_file);
    }
    {
        struct section_64 section_data = {
            .sectname = SECT_DATA,
            .segname = SEG_DATA,
            .addr = 0,
            .size = assets_content_aligned_size,
            .offset = data_offset,
            .align = align,
            .reloff = 0,
            .nreloc = 0,
            .flags = 0,
            .reserved1 = 0,
            .reserved2 = 0,
            .reserved3 = 0,
        };
        fwrite(&section_data, sizeof(struct section_64), 1, out_object_file);
    }
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
               out_object_file);
    }
    {
        struct symtab_command load_command_symtab = {
            .cmd = LC_SYMTAB,
            .cmdsize = sizeof(struct symtab_command),
            .symoff = sym_table_offset,
            .nsyms = assets_count * 2 +
                     2, // symbols and their sizes symbols + local symbols,
            .stroff = sym_str_offset,
            .strsize = ceil_to_alignment(symbol_names_length, sizeof(long)),
        };
        fwrite(&load_command_symtab, sizeof(struct symtab_command), 1,
               out_object_file);
    }
    {
        struct dysymtab_command load_command_dysymtab = {
            .cmd = LC_DYSYMTAB,
            .cmdsize = sizeof(struct dysymtab_command),
            .ilocalsym = 0,
            .nlocalsym = 2,
            .iextdefsym = 2,
            .nextdefsym = assets_count * 2,
            .iundefsym = assets_count * 2 + 2,
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
        fwrite(&load_command_dysymtab, sizeof(struct dysymtab_command), 1,
               out_object_file);
    }
    {
        for (uint32_t i = 0; i < assets_count; i++) {
            fwrite(assets[i].content, 1, assets[i].size, out_object_file);
            fill_to_alignment(.file = out_object_file, .cur = assets[i].size,
                              alignment);
            fwrite(&assets[i].size, sizeof(assets[i].size), 1, out_object_file);
            fill_to_alignment(.file = out_object_file,
                              .cur = sizeof(assets[i].size), alignment);
        }

        fill_to_alignment(.file = out_object_file,
                          .cur = assets_content_aligned_size,
                          .alignment = sizeof(long));
    }

    {
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

        struct nlist_64 *symbols_table =
            calloc(sizeof(struct nlist_64), assets_count * 2);
        uint32_t current_pos = 13;
        uint64_t current_offset = 0;
        for (uint32_t i = 0; i < assets_count; i++) {
            symbols_table[i * 2] = (struct nlist_64){
                .n_un.n_strx = current_pos,
                .n_type = N_TYPE & N_SECT | N_EXT,
                .n_sect = 2,
                .n_desc = 0,
                .n_value = current_offset,
            };
            current_pos += sdslen(assets[i].var_name) + 1;
            current_offset += ceil_to_alignment(assets[i].size, alignment);

            symbols_table[i * 2 + 1] = (struct nlist_64){
                .n_un.n_strx = current_pos,
                .n_type = N_TYPE & N_SECT | N_EXT,
                .n_sect = 2,
                .n_desc = 0,
                .n_value = current_offset,
            };
            current_pos += sdslen(assets[i].var_size_name) + 1;
            current_offset +=
                ceil_to_alignment(sizeof(assets[i].size), alignment);
        }

        fwrite(local_symbols, sizeof(struct nlist_64), 2, out_object_file);
        fwrite(symbols_table, sizeof(struct nlist_64), assets_count * 2,
               out_object_file);
    }

    {
        fwrite(local_symbols_str, 1, sizeof(local_symbols_str),
               out_object_file);
        for (uint32_t i = 0; i < assets_count; i++) {
            fwrite(assets[i].var_name, 1, sdslen(assets[i].var_name) + 1,
                   out_object_file);
            fwrite(assets[i].var_size_name, 1,
                   sdslen(assets[i].var_size_name) + 1, out_object_file);
        }

        fill_to_alignment(.file = out_object_file, .cur = symbol_names_length,
                          .alignment = sizeof(long));
    }

    fflush(out_object_file);
    fclose(out_object_file);
}
