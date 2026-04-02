// SPDX-License-Identifier: Unlicense

#pragma once

#define BSOD_MAX_RECORDS       50
#define BSOD_MAX_MODULES       64
#define BSOD_MAX_STACK_FRAMES  32
#define BSOD_MAX_PATH          512
#define BSOD_MAX_DESC          256

typedef struct _BSOD_MODULE {
    char name[64];
    char path[BSOD_MAX_PATH];
    UINT64 base;
    UINT32 size;
    UINT32 timestamp;
    char version[32];
} BSOD_MODULE;

typedef struct _BSOD_STACK_FRAME {
    UINT64 address;
    char module[64];
    char symbol[128];
    UINT32 offset;
} BSOD_STACK_FRAME;

typedef struct _BSOD_RECORD {
    char timestamp[32];
    char bugcheck_code[16];
    char bugcheck_name[64];
    UINT32 bugcheck_id;
    UINT64 param1;
    UINT64 param2;
    UINT64 param3;
    UINT64 param4;
    char dump_file[BSOD_MAX_PATH];
    char process_name[64];
    char caused_by_driver[64];
    BSOD_MODULE modules[BSOD_MAX_MODULES];
    int module_count;
    BSOD_STACK_FRAME stack[BSOD_MAX_STACK_FRAMES];
    int stack_count;
} BSOD_RECORD;

void gnwinfo_bsod_init(void);
void gnwinfo_bsod_fini(void);

int gnwinfo_bsod_get_record_count(void);
const BSOD_RECORD* gnwinfo_bsod_get_record(int index);

void gnwinfo_bsod_refresh(void);
void gnwinfo_bsod_parse_minidump(const char* dump_path, BSOD_RECORD* record);

const char* gnwinfo_bsod_get_code_name(UINT32 code);
const char* gnwinfo_bsod_get_code_desc(UINT32 code);

int gnwinfo_bsod_get_dump_enabled(void);
int gnwinfo_bsod_set_dump_enabled(int enable);
int gnwinfo_bsod_get_dump_type(void);
int gnwinfo_bsod_set_dump_type(int type);
