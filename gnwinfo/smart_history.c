// SPDX-License-Identifier: Unlicense

#include "gnwinfo.h"
#include "../libcdi/libcdi.h"
#include <time.h>

#define MAX_PATH_LEN 512
#define MAX_CSV_COLS 64
#define MAX_CSV_ROWS 1000
#define MAX_COL_WIDTH 256

static const BYTE g_critical_smart_ids[] = {
    0x01, 0x03, 0x05, 0xBF, 0xBB, 0xC7, 0xE7, 0
};

LPCSTR NWL_Ucs2ToUtf8(LPCWSTR src);

static CDI_SMART* g_smart = NULL;
static char g_csv_path[MAX_PATH_LEN] = {0};

typedef struct {
    char data[MAX_CSV_ROWS][MAX_CSV_COLS][MAX_COL_WIDTH];
    int rows;
    int cols;
    char filename[MAX_PATH_LEN];
} csv_data_t;

typedef struct {
    BYTE id;
    UINT64 value;
} smart_value_t;

typedef struct {
    smart_value_t values[64];
    int count;
    char serial[64];
    int initialized;
} disk_last_values_t;

static csv_data_t g_csv_files[16];
static int g_csv_count = 0;
static disk_last_values_t g_last_values[16];
static int g_last_values_count = 0;

static void get_timestamp(char* buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", t);
}

static int is_critical_smart_id(BYTE id)
{
    for (int i = 0; g_critical_smart_ids[i] != 0; i++) {
        if (g_critical_smart_ids[i] == id)
            return 1;
    }
    return 0;
}

static disk_last_values_t* get_last_values(const char* serial)
{
    for (int i = 0; i < g_last_values_count; i++) {
        if (strcmp(g_last_values[i].serial, serial) == 0)
            return &g_last_values[i];
    }
    return NULL;
}

static disk_last_values_t* create_last_values(const char* serial)
{
    if (g_last_values_count >= 16)
        return NULL;
    
    disk_last_values_t* dv = &g_last_values[g_last_values_count];
    memset(dv, 0, sizeof(disk_last_values_t));
    strcpy_s(dv->serial, sizeof(dv->serial), serial);
    g_last_values_count++;
    return dv;
}

static int load_last_values_from_csv(disk_last_values_t* last, const char* filename, BOOL is_nvme)
{
    FILE* fp = NULL;
    fopen_s(&fp, filename, "r");
    if (fp == NULL)
        return 0;
    
    char line[8192];
    char* header_line = NULL;
    char* last_data_line = NULL;
    char* context = NULL;
    
    header_line = fgets(line, sizeof(line), fp);
    if (header_line == NULL) {
        fclose(fp);
        return 0;
    }
    
    BYTE id_list[MAX_CSV_COLS] = {0};
    int id_count = 0;
    
    char* token = strtok_s(header_line, ",\n", &context);
    token = strtok_s(NULL, ",\n", &context);
    
    while (token && id_count < MAX_CSV_COLS) {
        unsigned int id = 0;
        if (sscanf_s(token, "%u", &id) == 1 && id > 0 && id < 256) {
            id_list[id_count] = (BYTE)id;
        }
        id_count++;
        token = strtok_s(NULL, ",\n", &context);
    }
    
    char prev_line[8192] = {0};
    while (fgets(line, sizeof(line), fp) != NULL) {
        strcpy_s(prev_line, sizeof(prev_line), line);
    }
    
    fclose(fp);
    
    if (prev_line[0] == '\0')
        return 0;
    
    last->count = 0;
    context = NULL;
    token = strtok_s(prev_line, ",\n", &context);
    token = strtok_s(NULL, ",\n", &context);
    
    int col = 0;
    while (token && col < id_count && last->count < 64) {
        if (id_list[col] > 0) {
            last->values[last->count].id = id_list[col];
            last->values[last->count].value = 0;
            
            if (is_nvme) {
                unsigned long long val = 0;
                sscanf_s(token, "%llu", &val);
                last->values[last->count].value = val;
            } else {
                unsigned int val = 0;
                sscanf_s(token, "%u", &val);
                last->values[last->count].value = val;
            }
            last->count++;
        }
        col++;
        token = strtok_s(NULL, ",\n", &context);
    }
    
    return last->count > 0;
}

static int check_critical_change(disk_last_values_t* last, int disk_index, DWORD attr_count, BOOL is_nvme)
{
    if (last == NULL)
        return 1;
    
    if (last->count == 0)
        return 0;
    
    for (DWORD i = 0; i < attr_count; i++) {
        BYTE id = cdi_get_smart_id(g_smart, disk_index, i);
        if (id == 0) continue;
        
        if (!is_critical_smart_id(id))
            continue;
        
        UINT64 current_val = 0;
        if (is_nvme) {
            current_val = cdi_get_smart_raw_value(g_smart, disk_index, i);
        } else {
            current_val = cdi_get_smart_current_value(g_smart, disk_index, i);
        }
        
        int found = 0;
        for (int j = 0; j < last->count; j++) {
            if (last->values[j].id == id) {
                found = 1;
                if (last->values[j].value != current_val)
                    return 1;
                break;
            }
        }
        
        if (!found) {
            return 1;
        }
    }
    
    return 0;
}

static void save_current_values(disk_last_values_t* last, int disk_index, DWORD attr_count, BOOL is_nvme)
{
    if (last == NULL)
        return;
    
    last->count = 0;
    for (DWORD i = 0; i < attr_count && last->count < 64; i++) {
        BYTE id = cdi_get_smart_id(g_smart, disk_index, i);
        if (id == 0) continue;
        
        last->values[last->count].id = id;
        if (is_nvme) {
            last->values[last->count].value = cdi_get_smart_raw_value(g_smart, disk_index, i);
        } else {
            last->values[last->count].value = cdi_get_smart_current_value(g_smart, disk_index, i);
        }
        last->count++;
    }
}

void gnwinfo_smart_history_init(void)
{
    if (g_smart != NULL)
        return;
    
    g_smart = cdi_create_smart();
    if (g_smart == NULL)
        return;
    
    cdi_init_smart(g_smart, CDI_FLAG_DEFAULT);
    
    GetModuleFileNameA(NULL, g_csv_path, MAX_PATH_LEN);
    char* last_slash = strrchr(g_csv_path, '\\');
    if (last_slash)
        *(last_slash + 1) = '\0';
    strcat_s(g_csv_path, MAX_PATH_LEN, "data");
    CreateDirectoryA(g_csv_path, NULL);
    strcat_s(g_csv_path, MAX_PATH_LEN, "\\");
}

void gnwinfo_smart_history_fini(void)
{
    if (g_smart)
    {
        cdi_destroy_smart(g_smart);
        g_smart = NULL;
    }
}

void gnwinfo_save_smart_history(void)
{
    if (g_smart == NULL)
        return;
    
    int disk_count = cdi_get_disk_count(g_smart);
    if (disk_count <= 0)
        return;
    
    for (int disk_index = 0; disk_index < disk_count; disk_index++)
    {
        cdi_update_smart(g_smart, disk_index);
        
        DWORD attr_count = cdi_get_dword(g_smart, disk_index, CDI_DWORD_ATTR_COUNT);
        if (attr_count == 0)
            continue;
        
        WCHAR* serial = cdi_get_string(g_smart, disk_index, CDI_STRING_SN);
        WCHAR* drive_map = cdi_get_string(g_smart, disk_index, CDI_STRING_DRIVE_MAP);
        
        if (serial == NULL)
        {
            if (drive_map) cdi_free_string(drive_map);
            continue;
        }
        
        char serial_a[64] = {0};
        WideCharToMultiByte(CP_ACP, 0, serial, -1, serial_a, sizeof(serial_a), NULL, NULL);
        
        BOOL is_nvme = cdi_get_is_nvme(g_smart, disk_index);
        
        char filename[MAX_PATH_LEN];
        char drive_letter = '\0';
        if (drive_map && wcslen(drive_map) > 0)
        {
            drive_letter = (char)drive_map[0];
        }
        
        if (drive_letter)
        {
            _snprintf_s(filename, MAX_PATH_LEN, _TRUNCATE, "%s%c_%S_diskdata.csv", 
                       g_csv_path, drive_letter, serial);
        }
        else
        {
            _snprintf_s(filename, MAX_PATH_LEN, _TRUNCATE, "%s%S_diskdata.csv", 
                       g_csv_path, serial);
        }
        
        disk_last_values_t* last = get_last_values(serial_a);
        int is_new_disk = 0;
        if (last == NULL) {
            last = create_last_values(serial_a);
            is_new_disk = 1;
        }
        
        if (!last->initialized) {
            if (load_last_values_from_csv(last, filename, is_nvme)) {
                last->initialized = 1;
            } else {
                save_current_values(last, disk_index, attr_count, is_nvme);
                last->initialized = 1;
                
                FILE* fp = NULL;
                fopen_s(&fp, filename, "w");
                if (fp != NULL) {
                    fprintf(fp, "Time,");
                    for (DWORD i = 0; i < attr_count; i++) {
                        BYTE id = cdi_get_smart_id(g_smart, disk_index, i);
                        if (id == 0) continue;
                        
                        WCHAR* name = cdi_get_smart_name(g_smart, disk_index, id);
                        if (name) {
                            fprintf(fp, "%02u %s,", (unsigned int)id, NWL_Ucs2ToUtf8(name));
                            cdi_free_string(name);
                        } else {
                            fprintf(fp, "%02u Unknown,", (unsigned int)id);
                        }
                    }
                    fprintf(fp, "\n");
                    
                    char timestamp[32];
                    get_timestamp(timestamp, sizeof(timestamp));
                    fprintf(fp, "%s,", timestamp);
                    
                    for (DWORD i = 0; i < attr_count; i++) {
                        BYTE id = cdi_get_smart_id(g_smart, disk_index, i);
                        if (id == 0) continue;
                        
                        if (is_nvme) {
                            UINT64 raw_value = cdi_get_smart_raw_value(g_smart, disk_index, i);
                            fprintf(fp, "%llu,", (unsigned long long)raw_value);
                        } else {
                            BYTE current_value = cdi_get_smart_current_value(g_smart, disk_index, i);
                            fprintf(fp, "%u,", (unsigned int)current_value);
                        }
                    }
                    fprintf(fp, "\n");
                    fclose(fp);
                }
            }
            cdi_free_string(serial);
            if (drive_map) cdi_free_string(drive_map);
            continue;
        }
        
        int need_save = check_critical_change(last, disk_index, attr_count, is_nvme);
        
        if (!need_save) {
            cdi_free_string(serial);
            if (drive_map) cdi_free_string(drive_map);
            continue;
        }
        
        save_current_values(last, disk_index, attr_count, is_nvme);
        
        FILE* fp = NULL;
        fopen_s(&fp, filename, "a");
        if (fp == NULL)
        {
            cdi_free_string(serial);
            if (drive_map) cdi_free_string(drive_map);
            continue;
        }
        
        char timestamp[32];
        get_timestamp(timestamp, sizeof(timestamp));
        fprintf(fp, "%s,", timestamp);
        
        for (DWORD i = 0; i < attr_count; i++) {
            BYTE id = cdi_get_smart_id(g_smart, disk_index, i);
            if (id == 0) continue;
            
            if (is_nvme) {
                UINT64 raw_value = cdi_get_smart_raw_value(g_smart, disk_index, i);
                fprintf(fp, "%llu,", (unsigned long long)raw_value);
            } else {
                BYTE current_value = cdi_get_smart_current_value(g_smart, disk_index, i);
                fprintf(fp, "%u,", (unsigned int)current_value);
            }
        }
        fprintf(fp, "\n");
        
        fclose(fp);
        cdi_free_string(serial);
        if (drive_map) cdi_free_string(drive_map);
    }
}

static int load_csv_file(const char* filename)
{
    FILE* fp = NULL;
    fopen_s(&fp, filename, "r");
    if (fp == NULL)
        return -1;
    
    if (g_csv_count >= 16) {
        fclose(fp);
        return -1;
    }
    
    csv_data_t* csv = &g_csv_files[g_csv_count];
    memset(csv, 0, sizeof(csv_data_t));
    strcpy_s(csv->filename, MAX_PATH_LEN, filename);
    
    char line[8192];
    int row = 0;
    
    while (fgets(line, sizeof(line), fp) && row < MAX_CSV_ROWS) {
        int col = 0;
        char* context = NULL;
        char* token = strtok_s(line, ",\n", &context);
        
        while (token && col < MAX_CSV_COLS) {
            strcpy_s(csv->data[row][col], MAX_COL_WIDTH, token);
            col++;
            token = strtok_s(NULL, ",\n", &context);
        }
        
        if (col > csv->cols)
            csv->cols = col;
        row++;
    }
    
    csv->rows = row;
    fclose(fp);
    
    g_csv_count++;
    return g_csv_count - 1;
}

void gnwinfo_load_smart_history(void)
{
    g_csv_count = 0;
    
    WIN32_FIND_DATAA findData;
    char search_path[MAX_PATH_LEN];
    _snprintf_s(search_path, MAX_PATH_LEN, _TRUNCATE, "%s*_diskdata.csv", g_csv_path);
    
    HANDLE hFind = FindFirstFileA(search_path, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    
    do {
        char full_path[MAX_PATH_LEN];
        _snprintf_s(full_path, MAX_PATH_LEN, _TRUNCATE, "%s%s", g_csv_path, findData.cFileName);
        load_csv_file(full_path);
    } while (FindNextFileA(hFind, &findData) && g_csv_count < 16);
    
    FindClose(hFind);
}

int gnwinfo_get_smart_history_count(void)
{
    return g_csv_count;
}

const char* gnwinfo_get_smart_history_filename(int index)
{
    if (index < 0 || index >= g_csv_count)
        return NULL;
    return g_csv_files[index].filename;
}

int gnwinfo_get_smart_history_rows(int index)
{
    if (index < 0 || index >= g_csv_count)
        return 0;
    return g_csv_files[index].rows;
}

int gnwinfo_get_smart_history_cols(int index)
{
    if (index < 0 || index >= g_csv_count)
        return 0;
    return g_csv_files[index].cols;
}

const char* gnwinfo_get_smart_history_cell(int index, int row, int col)
{
    if (index < 0 || index >= g_csv_count)
        return NULL;
    if (row < 0 || row >= g_csv_files[index].rows)
        return NULL;
    if (col < 0 || col >= g_csv_files[index].cols)
        return NULL;
    return g_csv_files[index].data[row][col];
}
