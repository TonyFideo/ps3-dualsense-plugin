#include <ppu-types.h>

/* PSL1GHT intentionally does not provide the proprietary SYS_MODULE_* macros.
   Emit the documented CellOS module export records directly, using 32-bit PRX
   pointers inside the otherwise 64-bit PPU ABI. */

typedef void *prx_pointer ATTRIBUTE_PRXPTR;

typedef struct prx_library_common {
    u8 size;
    u8 auxiliary_attribute;
    u16 version;
    u16 attribute;
    u16 function_count;
    u16 variable_count;
    u16 tls_count;
    u8 hash_info;
    u8 tls_hash_info;
    u8 reserved;
    u8 alternative_nids;
} prx_library_common;

typedef struct prx_library_entry {
    prx_library_common common;
    prx_pointer library_name;
    prx_pointer nid_table;
    prx_pointer entry_table;
} prx_library_entry;

typedef struct prx_module_info {
    u16 attributes;
    u8 version[2];
    char name[27];
    u8 info_version;
    prx_pointer gp_value;
    prx_pointer library_entries_top;
    prx_pointer library_entries_bottom;
    prx_pointer library_stubs_top;
    prx_pointer library_stubs_bottom;
} prx_module_info;

extern s32 module_start(size_t args, void *argp);
extern s32 module_stop(size_t args, void *argp);
extern u8 __libentstart[];
extern u8 __libentend[];
extern u8 __libstubstart[];
extern u8 __libstubend[];

static const u32 g_module_nids[]
    __attribute__((section(".rodata.dsModuleNids"), used)) = {
        0xbc9a0086u, /* module_start */
        0xab779874u  /* module_stop */
    };

static const prx_pointer g_module_entries[]
    __attribute__((section(".data.dsModuleEntries"), used)) = {
        (prx_pointer)module_start,
        (prx_pointer)module_stop
    };

static const prx_library_entry g_module_exports
    __attribute__((section(".lib.ent"), aligned(4), used)) = {
        {
            sizeof(prx_library_entry), 0, 0, 0x8000u,
            2, 0, 0, 0, 0, 0, 0
        },
        (prx_pointer)0,
        (prx_pointer)g_module_nids,
        (prx_pointer)g_module_entries
    };

static const prx_module_info g_module_info
    __attribute__((section(".rodata.sceModuleInfo.modInfo#"), aligned(4), used)) = {
        0,
        {1, 0},
        "dualsense_fix",
        0,
        (prx_pointer)0,
        (prx_pointer)(__libentstart + 4),
        (prx_pointer)__libentend,
        (prx_pointer)(__libstubstart + 4),
        (prx_pointer)__libstubend
    };
