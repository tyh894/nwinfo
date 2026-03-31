// SPDX-License-Identifier: Unlicense

#include "gnwinfo.h"
#include "bsod.h"
#include <winevt.h>
#include <dbghelp.h>

#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma execution_character_set("utf-8")

static BSOD_RECORD g_bsod_records[BSOD_MAX_RECORDS];
static int g_bsod_count = 0;
static CRITICAL_SECTION g_bsod_lock;

static const struct {
    UINT32 code;
    const char* name;
    const char* desc;
} g_bsod_codes[] = {
    {0x00000001, "APC_INDEX_MISMATCH", "APC索引不匹配"},
    {0x0000000A, "IRQL_NOT_LESS_OR_EQUAL", "IRQL不小于或等于"},
    {0x00000012, "NOT_IMPLEMENTED", "未实现"},
    {0x0000001A, "MEMORY_MANAGEMENT", "内存管理错误"},
    {0x0000001E, "KMODE_EXCEPTION_NOT_HANDLED", "内核模式异常未处理"},
    {0x00000024, "NTFS_FILE_SYSTEM", "NTFS文件系统错误"},
    {0x00000034, "CACHE_MANAGER", "缓存管理器错误"},
    {0x0000003B, "SYSTEM_SERVICE_EXCEPTION", "系统服务异常"},
    {0x00000044, "MULTIPLE_IRP_COMPLETE_REQUESTS", "多个IRP完成请求"},
    {0x00000050, "PAGE_FAULT_IN_NONPAGED_AREA", "非分页区域页面错误"},
    {0x00000051, "REGISTRY_ERROR", "注册表错误"},
    {0x0000005A, "CRITICAL_SERVICE_FAILED", "关键服务失败"},
    {0x0000005D, "UNSUPPORTED_PROCESSOR", "不支持的处理器"},
    {0x0000006B, "PROCESS1_INITIALIZATION_FAILED", "进程1初始化失败"},
    {0x00000073, "CONFIG_LIST_FAILED", "配置列表失败"},
    {0x00000074, "BAD_SYSTEM_CONFIG_INFO", "错误的系统配置信息"},
    {0x00000077, "KERNEL_STACK_INPAGE_ERROR", "内核堆栈页面错误"},
    {0x00000079, "MISMATCHED_HAL", "HAL不匹配"},
    {0x0000007A, "KERNEL_DATA_INPAGE_ERROR", "内核数据页面错误"},
    {0x0000007B, "INACCESSIBLE_BOOT_DEVICE", "无法访问启动设备"},
    {0x0000007E, "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED", "系统线程异常未处理"},
    {0x0000007F, "UNEXPECTED_KERNEL_MODE_TRAP", "意外的内核模式陷阱"},
    {0x00000080, "NMI_HARDWARE_FAILURE", "NMI硬件故障"},
    {0x0000008E, "KERNEL_MODE_EXCEPTION_NOT_HANDLED", "内核模式异常未处理"},
    {0x00000093, "INVALID_KERNEL_HANDLE", "无效的内核句柄"},
    {0x0000009C, "MACHINE_CHECK_EXCEPTION", "机器检查异常"},
    {0x0000009F, "DRIVER_POWER_STATE_FAILURE", "驱动程序电源状态失败"},
    {0x000000A5, "ACPI_BIOS_ERROR", "ACPI BIOS错误"},
    {0x000000B4, "VIDEO_DRIVER_INIT_FAILURE", "视频驱动初始化失败"},
    {0x000000C2, "BAD_POOL_CALLER", "错误的内存池调用者"},
    {0x000000C4, "DRIVER_VERIFIER_DETECTED_VIOLATION", "驱动程序验证程序检测到违规"},
    {0x000000CA, "PNP_DETECTED_FATAL_ERROR", "PNP检测到致命错误"},
    {0x000000CE, "DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS", "驱动程序卸载时未取消挂起操作"},
    {0x000000D1, "DRIVER_IRQL_NOT_LESS_OR_EQUAL", "驱动程序IRQL不小于或等于"},
    {0x000000D5, "DRIVER_PAGE_FAULT_IN_FREED_SPECIAL_POOL", "驱动程序在已释放特殊内存池中页面错误"},
    {0x000000E1, "WORKER_THREAD_RETURNED_AT_BAD_IRQL", "工作线程在错误IRQL返回"},
    {0x000000E3, "RESOURCE_NOT_OWNED", "资源未被拥有"},
    {0x000000EA, "THREAD_STUCK_IN_DEVICE_DRIVER", "线程卡在设备驱动程序中"},
    {0x000000ED, "UNMOUNTABLE_BOOT_VOLUME", "无法挂载的启动卷"},
    {0x000000EF, "CRITICAL_PROCESS_DIED", "关键进程已终止"},
    {0x000000F2, "HARDWARE_INTERRUPT_STORM", "硬件中断风暴"},
    {0x000000F4, "CRITICAL_OBJECT_TERMINATION", "关键对象终止"},
    {0x000000FC, "ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY", "尝试执行不可执行内存"},
    {0x00000101, "CLOCK_WATCHDOG_TIMEOUT", "时钟看门狗超时"},
    {0x00000102, "DPC_WATCHDOG_TIMEOUT", "DPC看门狗超时"},
    {0x00000109, "CRITICAL_STRUCTURE_CORRUPTION", "关键结构损坏"},
    {0x00000116, "VIDEO_TDR_FAILURE", "视频TDR失败"},
    {0x00000119, "VIDEO_SCHEDULER_INTERNAL_ERROR", "视频调度程序内部错误"},
    {0x00000124, "WHEA_UNCORRECTABLE_ERROR", "WHEA不可纠正错误"},
    {0x00000133, "DPC_WATCHDOG_VIOLATION", "DPC看门狗违规"},
    {0x00000139, "KERNEL_SECURITY_CHECK_FAILURE", "内核安全检查失败"},
    {0x0000013A, "KERNEL_MODE_HEAP_CORRUPTION", "内核模式堆损坏"},
    {0x00000141, "VIDEO_ENGINE_TIMEOUT_DETECTED", "视频引擎超时检测"},
    {0x00000154, "UNEXPECTED_STORE_EXCEPTION", "意外存储异常"},
    {0x00000159, "HAL_ILLEGAL_IOMMU_PAGE_FAULT", "HAL非法IOMMU页面错误"},
    {0x0000015A, "SDBUS_INTERNAL_ERROR", "SDBUS内部错误"},
    {0x0000015B, "WORKER_THREAD_RETURNED_WITH_SYSTEM_PAGE_PRIORITY_ACTIVE", "工作线程返回时系统页面优先级活动"},
    {0x00000161, "LIVE_SYSTEM_DUMP", "活动系统转储"},
    {0x00000184, "HYPERVISOR_ERROR", "虚拟机监控程序错误"},
    {0x00000189, "BAD_OBJECT_HEADER", "错误的对象头"},
    {0x0000018A, "SECURE_KERNEL_ERROR", "安全内核错误"},
    {0x0000019C, "DIRECT_WRITE_STALL_BUGCHECK", "直接写入停滞错误检查"},
    {0x000001C8, "MANUALLY_INITIATED_POWER_BUTTON_HOLD", "手动启动的电源按钮按住"},
    {0x000001CF, "HARDWARE_WATCHDOG_TIMEOUT", "硬件看门狗超时"},
    {0, NULL, NULL}
};

const char* gnwinfo_bsod_get_code_name(UINT32 code)
{
    for (int i = 0; g_bsod_codes[i].name != NULL; i++) {
        if (g_bsod_codes[i].code == code)
            return g_bsod_codes[i].name;
    }
    return "UNKNOWN";
}

const char* gnwinfo_bsod_get_code_desc(UINT32 code)
{
    for (int i = 0; g_bsod_codes[i].desc != NULL; i++) {
        if (g_bsod_codes[i].code == code)
            return g_bsod_codes[i].desc;
    }
    return "未知错误";
}

void gnwinfo_bsod_init(void)
{
    InitializeCriticalSection(&g_bsod_lock);
    gnwinfo_bsod_refresh();
}

void gnwinfo_bsod_fini(void)
{
    DeleteCriticalSection(&g_bsod_lock);
}

int gnwinfo_bsod_get_record_count(void)
{
    return g_bsod_count;
}

const BSOD_RECORD* gnwinfo_bsod_get_record(int index)
{
    if (index < 0 || index >= g_bsod_count)
        return NULL;
    return &g_bsod_records[index];
}

static void parse_event_time(ULONGLONG time, char* buffer, size_t size)
{
    FILETIME ft;
    ft.dwHighDateTime = (DWORD)(time >> 32);
    ft.dwLowDateTime = (DWORD)(time & 0xFFFFFFFF);
    
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    
    _snprintf_s(buffer, size, _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static void read_bugcheck_event(EVT_HANDLE hEvent, BSOD_RECORD* record)
{
    EVT_HANDLE hContext = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);
    if (hContext == NULL)
        return;
    
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD dwPropertyCount = 0;
    PEVT_VARIANT pRenderedValues = NULL;
    
    EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        dwBufferSize = dwBufferUsed;
        pRenderedValues = (PEVT_VARIANT)malloc(dwBufferSize);
        if (pRenderedValues) {
            if (!EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount)) {
                free(pRenderedValues);
                EvtClose(hContext);
                return;
            }
        } else {
            EvtClose(hContext);
            return;
        }
    }
    
    EvtClose(hContext);
    
    if (pRenderedValues && dwPropertyCount >= 5) {
        if (pRenderedValues[EvtSystemTimeCreated].Type == EvtVarTypeFileTime) {
            parse_event_time(pRenderedValues[EvtSystemTimeCreated].FileTimeVal, record->timestamp, sizeof(record->timestamp));
        }
    }
    
    free(pRenderedValues);
    
    LPCWSTR pwsQuery = L"Event/EventData/Data";
    hContext = EvtCreateRenderContext(0, &pwsQuery, EvtRenderContextUser);
    if (hContext == NULL)
        return;
    
    dwBufferSize = 0;
    dwBufferUsed = 0;
    dwPropertyCount = 0;
    pRenderedValues = NULL;
    
    EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        dwBufferSize = dwBufferUsed;
        pRenderedValues = (PEVT_VARIANT)malloc(dwBufferSize);
        if (pRenderedValues) {
            if (!EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount)) {
                free(pRenderedValues);
                EvtClose(hContext);
                return;
            }
        } else {
            EvtClose(hContext);
            return;
        }
    }
    
    EvtClose(hContext);
    
    if (pRenderedValues && dwPropertyCount >= 1) {
        if (pRenderedValues[0].Type == EvtVarTypeUInt32) {
            record->bugcheck_id = pRenderedValues[0].UInt32Val;
            _snprintf_s(record->bugcheck_code, sizeof(record->bugcheck_code), _TRUNCATE, "0x%08X", record->bugcheck_id);
            strncpy_s(record->bugcheck_name, sizeof(record->bugcheck_name), 
                     gnwinfo_bsod_get_code_name(record->bugcheck_id), _TRUNCATE);
        }
        
        if (dwPropertyCount >= 4) {
            if (pRenderedValues[1].Type == EvtVarTypeUInt64)
                record->param1 = pRenderedValues[1].UInt64Val;
            if (pRenderedValues[2].Type == EvtVarTypeUInt64)
                record->param2 = pRenderedValues[2].UInt64Val;
            if (pRenderedValues[3].Type == EvtVarTypeUInt64)
                record->param3 = pRenderedValues[3].UInt64Val;
            if (dwPropertyCount >= 5 && pRenderedValues[4].Type == EvtVarTypeUInt64)
                record->param4 = pRenderedValues[4].UInt64Val;
        }
    }
    
    free(pRenderedValues);
}

void gnwinfo_bsod_refresh(void)
{
    EnterCriticalSection(&g_bsod_lock);
    
    g_bsod_count = 0;
    memset(g_bsod_records, 0, sizeof(g_bsod_records));
    
    EVT_HANDLE hQuery = EvtQuery(NULL, L"System", 
        L"Event/System[Provider[@Name='Microsoft-Windows-WER-SystemErrorReporting'] and (EventID=1001)]",
        EvtQueryChannelPath | EvtQueryReverseDirection);
    
    if (hQuery == NULL) {
        LeaveCriticalSection(&g_bsod_lock);
        return;
    }
    
    EVT_HANDLE hEvents[BSOD_MAX_RECORDS];
    DWORD dwReturned = 0;
    
    while (g_bsod_count < BSOD_MAX_RECORDS) {
        if (!EvtNext(hQuery, 1, &hEvents[g_bsod_count], INFINITE, 0, &dwReturned)) {
            break;
        }
        
        if (dwReturned == 0)
            break;
        
        read_bugcheck_event(hEvents[g_bsod_count], &g_bsod_records[g_bsod_count]);
        EvtClose(hEvents[g_bsod_count]);
        g_bsod_count++;
    }
    
    EvtClose(hQuery);
    
    char minidump_path[MAX_PATH];
    GetWindowsDirectoryA(minidump_path, MAX_PATH);
    strcat_s(minidump_path, MAX_PATH, "\\Minidump\\*.dmp");
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(minidump_path, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        char full_path[MAX_PATH];
        GetWindowsDirectoryA(full_path, MAX_PATH);
        strcat_s(full_path, MAX_PATH, "\\Minidump\\");
        
        do {
            char dump_file[MAX_PATH];
            strcpy_s(dump_file, MAX_PATH, full_path);
            strcat_s(dump_file, MAX_PATH, findData.cFileName);
            
            int found = 0;
            for (int i = 0; i < g_bsod_count; i++) {
                if (g_bsod_records[i].dump_file[0] == '\0') {
                    strcpy_s(g_bsod_records[i].dump_file, MAX_PATH, dump_file);
                    found = 1;
                    break;
                }
            }
            
            if (!found && g_bsod_count < BSOD_MAX_RECORDS) {
                strcpy_s(g_bsod_records[g_bsod_count].dump_file, MAX_PATH, dump_file);
                g_bsod_count++;
            }
        } while (FindNextFileA(hFind, &findData));
        
        FindClose(hFind);
    }
    
    LeaveCriticalSection(&g_bsod_lock);
}

void gnwinfo_bsod_parse_minidump(const char* dump_path, BSOD_RECORD* record)
{
    if (dump_path == NULL || record == NULL)
        return;
    
    HANDLE hFile = CreateFileA(dump_path, GENERIC_READ, FILE_SHARE_READ, 
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    
    HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMap == NULL) {
        CloseHandle(hFile);
        return;
    }
    
    PVOID pView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (pView == NULL) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return;
    }
    
    MINIDUMP_DIRECTORY* pDir = NULL;
    ULONG64* pException = NULL;
    ULONG cbStreamSize = 0;
    
    if (MiniDumpReadDumpStream(pView, ExceptionStream, &pDir, (PVOID*)&pException, &cbStreamSize)) {
        if (cbStreamSize >= sizeof(ULONG64)) {
        }
    }
    
    MINIDUMP_MODULE_LIST* pModuleList = NULL;
    if (MiniDumpReadDumpStream(pView, ModuleListStream, &pDir, (PVOID*)&pModuleList, &cbStreamSize)) {
        if (pModuleList && pModuleList->NumberOfModules > 0) {
            record->module_count = 0;
            for (ULONG i = 0; i < pModuleList->NumberOfModules && record->module_count < BSOD_MAX_MODULES; i++) {
                MINIDUMP_MODULE* pMod = &pModuleList->Modules[i];
                
                RVA rva = pMod->ModuleNameRva;
                if (rva != 0) {
                    WCHAR* pName = (WCHAR*)((PBYTE)pView + rva + 4);
                    WideCharToMultiByte(CP_ACP, 0, pName, -1, 
                                        record->modules[record->module_count].name, 
                                        sizeof(record->modules[record->module_count].name), 
                                        NULL, NULL);
                }
                
                record->modules[record->module_count].base = pMod->BaseOfImage;
                record->modules[record->module_count].size = pMod->SizeOfImage;
                record->modules[record->module_count].timestamp = pMod->TimeDateStamp;
                record->module_count++;
            }
        }
    }
    
    UnmapViewOfFile(pView);
    CloseHandle(hMap);
    CloseHandle(hFile);
}
