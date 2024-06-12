/* -*- mode: c; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil -*- */

/*
  MIT license (c) 2024 https://github.com/slowpeek
  Homepage: https://github.com/slowpeek/vddump
  About: Dump iso9660 volume descriptors
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

typedef unsigned int uint;
typedef unsigned char uchar;

void bye( const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

void bye_va( const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);

    exit(1);
}

char *term[] = {
    "\e[30m",
    "\e[31m",
    "\e[32m",
    "\e[33m",
    "\e[34m",
    "\e[35m",
    "\e[36m",
    "\e[37m",
    "\e[m"
};

enum {
    black,
    red,
    green,
    yellow,
    blue,
    magenta,
    cyan,
    white,
    rst,
    term_codes_count
} term_codes;

#define COUNT(x) (sizeof(x)/sizeof(x[0]))

typedef enum {
    _non,
    _711,
    _723,
    _731,
    _732,
    _733,
    _910,
    _915,
} ecma_format;

struct vd_field {
    char *name;
    uint o;
    uint l;
    ecma_format format;
};

/* Based on iso_primary_descriptor struct from /usr/include/linux/iso_fs.h */
struct vd_field iso_pvd[] = {
    {"code",                    0,     1,    _711},
    {"id",                      1,     5,    _non},
    {"version",                 6,     1,    _711},
    {"unused1",                 7,     1,    _non},
    {"system_id",               8,     32,   _non},
    {"volume_id",               40,    32,   _non},
    {"unused2",                 72,    8,    _non},
    {"volume_space_size",       80,    8,    _733},
    {"unused3",                 88,    32,   _non},
    {"volume_set_size",         120,   4,    _723},
    {"volume_sequence_number",  124,   4,    _723},
    {"logical_block_size",      128,   4,    _723},
    {"path_table_size",         132,   8,    _733},
    {"type_l_path_table",       140,   4,    _731},
    {"opt_type_l_path_table",   144,   4,    _731},
    {"type_m_path_table",       148,   4,    _732},
    {"opt_type_m_path_table",   152,   4,    _732},
    {"root_directory_record",   156,   34,   _910},
    {"volume_set_id",           190,   128,  _non},
    {"publisher_id",            318,   128,  _non},
    {"preparer_id",             446,   128,  _non},
    {"application_id",          574,   128,  _non},
    {"copyright_file_id",       702,   37,   _non},
    {"abstract_file_id",        739,   37,   _non},
    {"bibliographic_file_id",   776,   37,   _non},
    {"creation_date",           813,   17,   _non},
    {"modification_date",       830,   17,   _non},
    {"expiration_date",         847,   17,   _non},
    {"effective_date",          864,   17,   _non},
    {"file_structure_version",  881,   1,    _711},
    {"unused4",                 882,   1,    _non},
    {"application_data",        883,   512,  _non},
    {"unused5",                 1395,  653,  _non},
    {"", 0, 0, _non},
};

struct vd_field iso_svd[COUNT(iso_pvd)];

/* Based on iso_directory_record struct from /usr/include/linux/iso_fs.h */
struct vd_field iso_dir[] = {
    {"length",                  0,   1,  _711},
    {"ext_attr_length",         1,   1,  _711},
    {"extent",                  2,   8,  _733},
    {"size",                    10,  8,  _733},
    {"date",                    18,  7,  _915},
    {"flags",                   25,  1,  _711},
    {"file_unit_size",          26,  1,  _711},
    {"interleave",              27,  1,  _711},
    {"volume_sequence_number",  28,  4,  _723},
    {"name_len",                32,  1,  _711},
    {"name",                    33,  1,  _non},
    {"", 0, 0, _non},
};

struct vd_type {
    char *name;
    uchar code;
    bool supported;
    struct vd_field *desc;
};

const struct vd_type vd_types[] = {
    {"Boot Record",                       0,    false,  NULL},
    {"Primary Volume Descriptor",         1,    true,   iso_pvd},
    {"Supplementary Volume Descriptor",   2,    true,   iso_svd},
    {"Volume Partition Descriptor",       3,    false,  NULL},
    {"Volume Descriptor Set Terminator",  255,  false,  NULL},
    {"", 0, false, NULL},
};

void print_hex( const uchar *p, int n, int indent) {
    int i=0, j, m;

    for ( ; n>0; n-=16) {
        printf("%*s", indent, "");
        m = n >= 16 ? 16 : n;

        for ( j=0; j<m/2; j++) {
            printf("%02x%02x ", p[i+2*j], p[i+2*j+1]);
        }

        if (m&1) {
            printf("%02x   ", p[i+2*j]);
            j++;

        }

        for ( ; j<8; j++) {
            printf("     ");
        }

        putchar(' ');

        for ( j=0; j<m; j++) {
            printf("%c", p[i+j] >= 32 && p[i+j] <= 126 ? p[i+j] : '.');
        }

        putchar('\n');
        i += 16;
    }
}

void dump_field_list( const uchar *buf, const struct vd_field *pf, int indent) {
    const uchar *data;
    union { int i; char s[128]; } value;
    int print;

    for ( ; *pf->name; pf++) {
        data = buf + pf->o;
        print = 0;

        switch (pf->format) {
        case _711:
            value.i = data[0];
            print = 1;
            break;
        case _723:
            value.i = data[0] | data[1]<<8;
            print = 1;
            break;
        case _731:
        case _733:
            value.i = data[0] | data[1]<<8 | data[2]<<16 | data[3]<<24;
            print = 1;
            break;
        case _732:
            value.i = data[3] | data[2]<<2 | data[1]<<16 | data[0]<<24;
            print = 1;
            break;
        case _915:
            if (data[0] > 0) {
                snprintf(value.s, sizeof(value.s),
                         "%d-%02d-%02d %02d:%02d:%02d GMT%+03d", data[0]+1900,
                         data[1], data[2], data[3], data[4], data[5], (char)data[6]/4);
                print = 2;
            }
            break;
        default:
            break;
        }

        printf("%*s-- %s%s%s %d %d", indent, "", term[blue], pf->name, term[rst], pf->o, pf->l);

        if (print == 1) {
            printf(" (%s%u%s)", term[green], value.i, term[rst]);
        } else if (print == 2) {
            printf(" (%s%s%s)", term[green], value.s, term[rst]);
        }

        putchar('\n');

        if (pf->format == _910) {
            dump_field_list(data, iso_dir, indent+2);
        } else {
            print_hex(data, pf->l, indent);
        }
    }
}

#define _(x) fputs(x, stdout)
int usage() {
    _(
      "Usage: vddump [-c]\n"
      "Dump iso9660 volume descriptors in human-readable form.\n"
      "\n"
      "It reads data from stdin. Input should be iso9660 VD. Only Primary VD [1] and\n"
      "Supplementary VD [2] are supported.\n"
      "\n"
      "Single-value fields are printed this way (the parsed value is optional):\n"
      "\n"
      "  -- name offset length (parsed value)\n"
      "  xxd-style dump of the raw value\n"
      "\n"
      "Root Directory Record is a structure, it is expanded with extra indentation.\n"
      "\n"
      "Options:\n"
      "  -h               Show usage\n"
      "  -c               Apply colors to field names and parsed values\n"
      "\n"
      "Dump the first VD (the primary one) from $iso:\n"
      "\n"
      "  dd bs=2048 skip=16 count=1 status=none < \"$iso\" | vddump\n"
      "\n"
      "[1] ECMA-119, section 8.4\n"
      "[2] ECMA-119, section 8.5\n"
      "\n"
      "Homepage https://github.com/slowpeek/vddump\n");

    return 0;
}
#undef _

#define SECTOR 2048
int main( int argc, char **argv) {
    bool colors = false;
    int opt;

    while ((opt = getopt(argc, argv, "ch")) != -1) {
        switch (opt) {
        case 'c':
            colors = true;
            break;
        case 'h':
            return usage();
            break;
        default:
            return 1;
        }
    }

    if (!colors) {
        for (int i=0; i<term_codes_count; i++) {
            term[i] = "";
        }
    }

    /* Make iso_svd out of iso_pvd according to iso_supplementary_descriptor
       from /usr/include/linux/iso_fs.h { */
    memcpy(iso_svd, iso_pvd, sizeof(iso_pvd));
    iso_svd[3].name = "flags";
    iso_svd[3].format = _711;
    iso_svd[8].name = "escape";
    /* } */

    uchar buf[SECTOR];
    ssize_t result = read(0, buf, SECTOR);

    if (result < 0) {
        bye_va("IO error: %s", strerror(errno));
    }

    if (result < SECTOR) {
        bye_va("Bytes expected: %d, available: %d", SECTOR, result);
    }

    if (memcmp(&buf[1], "CD001\x01", 6)) {
        bye("It does not look like some iso9660 volume descriptor");
    }

    const struct vd_type *pt = vd_types;
    for ( ; *pt->name; pt++) {
        if (pt->code == buf[0]) {
            if (pt->supported) {
                dump_field_list(buf, pt->desc, 0);
                return 0;
            } else {
                bye_va("This tool does not support VD of type '%s'", pt->name);
            }
        }
    }

    bye_va("Invalid VD code: %u", (uint)buf[0]);
}
