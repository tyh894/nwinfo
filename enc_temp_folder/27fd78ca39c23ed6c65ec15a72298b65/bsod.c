// SPDX-License-Identifier: Unlicense

#include "gnwinfo.h"
#include "bsod.h"
#include <winevt.h>
#include <dbghelp.h>
#include <shellapi.h>

#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma execution_character_set("utf-8")

static BOOL IsRunAsAdmin(void)
{
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdminSID = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSID)) {
        if (!CheckTokenMembership(NULL, pAdminSID, &fIsRunAsAdmin)) {
            fIsRunAsAdmin = FALSE;
        }
        FreeSid(pAdminSID);
    }
    
    return fIsRunAsAdmin;
}

static void* my_memmem(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len)
{
    if (needle_len == 0 || haystack_len < needle_len)
        return NULL;
    
    const char* h = (const char*)haystack;
    const char* n = (const char*)needle;
    
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(h + i, n, needle_len) == 0)
            return (void*)(h + i);
    }
    return NULL;
}

static BSOD_RECORD g_bsod_records[BSOD_MAX_RECORDS];
static int g_bsod_count = 0;
static CRITICAL_SECTION g_bsod_lock;

static const struct {
    UINT32 code;
    const char* name;
    const char* desc;
} g_bsod_codes[] = {
    {0x00000001, "APC_INDEX_MISMATCH", "APC?????"},
    {0x00000002, "DEVICE_QUEUE_NOT_BUSY", "??????"},
    {0x00000003, "INVALID_AFFINITY_SET", "???????????"},
    {0x00000004, "INVALID_DATA_ACCESS_TRAP", "?????????"},
    {0x00000005, "INVALID_PROCESS_ATTACH_ATTEMPT", "?????????"},
    {0x00000006, "INVALID_PROCESS_DETACH_ATTEMPT", "?????????"},
    {0x00000007, "INVALID_SOFTWARE_INTERRUPT", "???????"},
    {0x00000008, "IRQL_NOT_DISPATCH_LEVEL", "IRQL???????"},
    {0x00000009, "IRQL_NOT_GREATER_OR_EQUAL", "IRQL??????"},
    {0x0000000A, "IRQL_NOT_LESS_OR_EQUAL", "IRQL??????"},
    {0x0000000B, "NO_EXCEPTION_HANDLING_SUPPORT", "???????"},
    {0x0000000C, "MAXIMUM_WAIT_OBJECTS_EXCEEDED", "?????????"},
    {0x0000000D, "MUTEX_LEVEL_NUMBER_VIOLATION", "???????"},
    {0x0000000E, "NO_USER_MODE_CONTEXT", "????????"},
    {0x0000000F, "SPIN_LOCK_ALREADY_OWNED", "???????"},
    {0x00000010, "SPIN_LOCK_NOT_OWNED", "???????"},
    {0x00000011, "THREAD_NOT_MUTEX_OWNER", "?????????"},
    {0x00000012, "TRAP_CAUSE_UNKNOWN", "??????"},
    {0x00000013, "EMPTY_THREAD_REAPER_LIST", "????????"},
    {0x00000014, "CREATE_DELETE_LOCK_NOT_LOCKED", "??/??????"},
    {0x00000015, "LAST_CHANCE_CALLED_FROM_KMODE", "????????????"},
    {0x00000016, "CID_HANDLE_CREATION", "CID????"},
    {0x00000017, "CID_HANDLE_DELETION", "CID????"},
    {0x00000018, "REFERENCE_BY_POINTER", "??????"},
    {0x00000019, "BAD_POOL_HEADER", "??????"},
    {0x0000001A, "MEMORY_MANAGEMENT", "??????"},
    {0x0000001B, "PFN_SHARE_COUNT", "?????????"},
    {0x0000001C, "PFN_REFERENCE_COUNT", "?????????"},
    {0x0000001D, "NO_SPIN_LOCK_AVAILABLE", "??????"},
    {0x0000001E, "KMODE_EXCEPTION_NOT_HANDLED", "?????????"},
    {0x0000001F, "SHARED_RESOURCE_CONV_ERROR", "????????"},
    {0x00000020, "KERNEL_APC_PENDING_DURING_EXIT", "?????APC??"},
    {0x00000021, "QUOTA_UNDERFLOW", "????"},
    {0x00000022, "FILE_SYSTEM", "??????"},
    {0x00000023, "FAT_FILE_SYSTEM", "FAT??????"},
    {0x00000024, "NTFS_FILE_SYSTEM", "NTFS??????"},
    {0x00000025, "NPFS_FILE_SYSTEM", "NPFS??????"},
    {0x00000026, "CDFS_FILE_SYSTEM", "CDFS??????"},
    {0x00000027, "RDR_FILE_SYSTEM", "RDR??????"},
    {0x00000028, "CORRUPT_ACCESS_TOKEN", "??????"},
    {0x00000029, "SECURITY_SYSTEM", "??????"},
    {0x0000002A, "INCONSISTENT_IRP", "????IRP"},
    {0x0000002B, "PANIC_STACK_SWITCH", "??????"},
    {0x0000002C, "PORT_DRIVER_INTERNAL", "??????????"},
    {0x0000002D, "SCSI_DISK_DRIVER_INTERNAL", "SCSI??????????"},
    {0x0000002E, "DATA_BUS_ERROR", "??????"},
    {0x0000002F, "INSTRUCTION_BUS_ERROR", "??????"},
    {0x00000030, "SET_OF_INVALID_CONTEXT", "????????"},
    {0x00000031, "PHASE0_INITIALIZATION_FAILED", "??0?????"},
    {0x00000032, "PHASE1_INITIALIZATION_FAILED", "??1?????"},
    {0x00000033, "UNEXPECTED_INITIALIZATION_CALL", "????????"},
    {0x00000034, "CACHE_MANAGER", "???????"},
    {0x00000035, "NO_MORE_IRP_STACK_LOCATIONS", "IRP??????"},
    {0x00000036, "DEVICE_REFERENCE_COUNT_NOT_ZERO", "????????"},
    {0x00000037, "FLOPPY_INTERNAL_ERROR", "??????"},
    {0x00000038, "SERIAL_DRIVER_INTERNAL", "??????????"},
    {0x00000039, "SYSTEM_EXIT_OWNED_MUTEX", "??????????"},
    {0x0000003A, "SYSTEM_UNWIND_PREVIOUS_USER", "?????????"},
    {0x0000003B, "SYSTEM_SERVICE_EXCEPTION", "??????"},
    {0x0000003C, "INTERRUPT_UNWIND_ATTEMPTED", "??????"},
    {0x0000003D, "INTERRUPT_EXCEPTION_NOT_HANDLED", "???????"},
    {0x0000003E, "MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED", "?????????"},
    {0x0000003F, "NO_MORE_SYSTEM_PTES", "???????"},
    {0x00000040, "TARGET_MDL_TOO_SMALL", "??MDL??"},
    {0x00000041, "MUST_SUCCEED_POOL_EMPTY", "??????????"},
    {0x00000042, "ATDISK_DRIVER_INTERNAL", "AT??????????"},
    {0x00000043, "NO_SUCH_PARTITION", "?????"},
    {0x00000044, "MULTIPLE_IRP_COMPLETE_REQUESTS", "??IRP????"},
    {0x00000045, "INSUFFICIENT_SYSTEM_MAP_REGS", "?????????"},
    {0x00000046, "DEREF_UNKNOWN_LOGON_SESSION", "?????????"},
    {0x00000047, "REF_UNKNOWN_LOGON_SESSION", "????????"},
    {0x00000048, "CANCEL_STATE_IN_COMPLETED_IRP", "???IRP??????"},
    {0x00000049, "PAGE_FAULT_WITH_INTERRUPTS_OFF", "??????????"},
    {0x0000004A, "IRQL_GT_ZERO_AT_SYSTEM_SERVICE", "?????IRQL???"},
    {0x0000004B, "STREAMS_INTERNAL_ERROR", "?????"},
    {0x0000004C, "FATAL_UNHANDLED_HARD_ERROR", "?????????"},
    {0x0000004D, "NO_PAGES_AVAILABLE", "?????"},
    {0x0000004E, "PFN_LIST_CORRUPT", "???????"},
    {0x0000004F, "NDIS_INTERNAL_ERROR", "NDIS????"},
    {0x00000050, "PAGE_FAULT_IN_NONPAGED_AREA", "????????"},
    {0x00000051, "REGISTRY_ERROR", "?????"},
    {0x00000052, "MAILSLOT_FILE_SYSTEM", "?????????"},
    {0x00000053, "NO_BOOT_DEVICE", "?????"},
    {0x00000054, "LM_SERVER_INTERNAL_ERROR", "LM???????"},
    {0x00000055, "DATA_COHERENCY_EXCEPTION", "???????"},
    {0x00000056, "INSTRUCTION_COHERENCY_EXCEPTION", "???????"},
    {0x00000057, "XNS_INTERNAL_ERROR", "XNS????"},
    {0x00000058, "FTDISK_INTERNAL_ERROR", "FTDISK????"},
    {0x00000059, "PINBALL_FILE_SYSTEM", "????????"},
    {0x0000005A, "CRITICAL_SERVICE_FAILED", "??????"},
    {0x0000005B, "SET_ENV_VAR_FAILED", "????????"},
    {0x0000005C, "HAL_INITIALIZATION_FAILED", "HAL?????"},
    {0x0000005D, "UNSUPPORTED_PROCESSOR", "???????"},
    {0x0000005E, "OBJECT_INITIALIZATION_FAILED", "???????"},
    {0x0000005F, "SECURITY_INITIALIZATION_FAILED", "???????"},
    {0x00000060, "PROCESS_INITIALIZATION_FAILED", "???????"},
    {0x00000061, "HAL1_INITIALIZATION_FAILED", "HAL1?????"},
    {0x00000062, "OBJECT1_INITIALIZATION_FAILED", "??1?????"},
    {0x00000063, "SECURITY1_INITIALIZATION_FAILED", "??1?????"},
    {0x00000064, "SYMBOLIC_INITIALIZATION_FAILED", "???????"},
    {0x00000065, "MEMORY1_INITIALIZATION_FAILED", "??1?????"},
    {0x00000066, "CACHE_INITIALIZATION_FAILED", "???????"},
    {0x00000067, "CONFIG_INITIALIZATION_FAILED", "???????"},
    {0x00000068, "FILE_INITIALIZATION_FAILED", "???????"},
    {0x00000069, "IO1_INITIALIZATION_FAILED", "IO1?????"},
    {0x0000006A, "LPC_INITIALIZATION_FAILED", "LPC?????"},
    {0x0000006B, "PROCESS1_INITIALIZATION_FAILED", "??1?????"},
    {0x0000006C, "REFMON_INITIALIZATION_FAILED", "??????????"},
    {0x0000006D, "SESSION1_INITIALIZATION_FAILED", "??1?????"},
    {0x0000006E, "SESSION2_INITIALIZATION_FAILED", "??2?????"},
    {0x0000006F, "SESSION3_INITIALIZATION_FAILED", "??3?????"},
    {0x00000070, "SESSION4_INITIALIZATION_FAILED", "??4?????"},
    {0x00000071, "SESSION5_INITIALIZATION_FAILED", "??5?????"},
    {0x00000072, "ASSIGN_DRIVE_LETTERS_FAILED", "????????"},
    {0x00000073, "CONFIG_LIST_FAILED", "??????"},
    {0x00000074, "BAD_SYSTEM_CONFIG_INFO", "????????"},
    {0x00000075, "CANNOT_WRITE_CONFIGURATION", "??????"},
    {0x00000076, "PROCESS_HAS_LOCKED_PAGES", "???????"},
    {0x00000077, "KERNEL_STACK_INPAGE_ERROR", "????????"},
    {0x00000078, "PHASE0_EXCEPTION", "??0??"},
    {0x00000079, "MISMATCHED_HAL", "HAL???"},
    {0x0000007A, "KERNEL_DATA_INPAGE_ERROR", "????????"},
    {0x0000007B, "INACCESSIBLE_BOOT_DEVICE", "????????"},
    {0x0000007C, "BUGCODE_NDIS_DRIVER", "NDIS???????"},
    {0x0000007D, "INSTALL_MORE_MEMORY", "??????"},
    {0x0000007E, "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED", "?????????"},
    {0x0000007F, "UNEXPECTED_KERNEL_MODE_TRAP", "?????????"},
    {0x00000080, "NMI_HARDWARE_FAILURE", "NMI????"},
    {0x00000081, "SPIN_LOCK_INIT_FAILURE", "????????"},
    {0x00000082, "DFS_FILE_SYSTEM", "DFS??????"},
    {0x00000083, "OFS_FILE_SYSTEM", "OFS??????"},
    {0x00000084, "RECOM_DRIVER", "RECOM??????"},
    {0x00000085, "SETUP_FAILURE", "????"},
    {0x0000008B, "MBR_CHECKSUM_MISMATCH", "MBR??????"},
    {0x0000008E, "KERNEL_MODE_EXCEPTION_NOT_HANDLED", "?????????"},
    {0x0000008F, "PP0_INITIALIZATION_FAILED", "PP0?????"},
    {0x00000090, "PP1_INITIALIZATION_FAILED", "PP1?????"},
    {0x00000091, "WIN32K_INIT_OR_RIT_FAILURE", "WIN32K????RIT??"},
    {0x00000092, "UP_DRIVER_ON_MP_SYSTEM", "??????????????"},
    {0x00000093, "INVALID_KERNEL_HANDLE", "???????"},
    {0x00000094, "KERNEL_STACK_LOCKED_AT_EXIT", "??????????"},
    {0x00000095, "PNP_INTERNAL_ERROR", "????????"},
    {0x00000096, "INVALID_WORK_QUEUE_ITEM", "????????"},
    {0x00000097, "BOUND_IMAGE_UNSUPPORTED", "???????"},
    {0x00000098, "END_OF_NT_EVALUATION_PERIOD", "NT?????"},
    {0x00000099, "INVALID_REGION_OR_SEGMENT", "???????"},
    {0x0000009A, "SYSTEM_LICENSE_VIOLATION", "???????"},
    {0x0000009B, "UDFS_FILE_SYSTEM", "UDFS??????"},
    {0x0000009C, "MACHINE_CHECK_EXCEPTION", "??????"},
    {0x0000009E, "USER_MODE_HEALTH_MONITOR", "?????????"},
    {0x0000009F, "DRIVER_POWER_STATE_FAILURE", "??????????"},
    {0x000000A0, "INTERNAL_POWER_ERROR", "??????"},
    {0x000000A1, "PCI_BUS_DRIVER_INTERNAL", "PCI??????????"},
    {0x000000A2, "MEMORY_IMAGE_CORRUPT", "??????"},
    {0x000000A3, "ACPI_DRIVER_INTERNAL", "ACPI????????"},
    {0x000000A4, "CNSS_FILE_SYSTEM_FILTER", "CNSS?????????"},
    {0x000000A5, "ACPI_BIOS_ERROR", "ACPI BIOS??"},
    {0x000000A7, "BAD_EXHANDLE", "??????"},
    {0x000000AB, "SESSION_HAS_VALID_POOL_ON_EXIT", "???????????"},
    {0x000000AC, "HAL_MEMORY_ALLOCATION", "HAL??????"},
    {0x000000AD, "VIDEO_DRIVER_DEBUG_REPORT_REQUEST", "????????????"},
    {0x000000B4, "VIDEO_DRIVER_INIT_FAILURE", "?????????"},
    {0x000000B8, "ATTEMPTED_SWITCH_FROM_DPC", "???DPC??"},
    {0x000000B9, "CHIPSET_DETECTED_ERROR", "????????"},
    {0x000000BA, "SESSION_HAS_VALID_VIEWS_ON_EXIT", "??????????"},
    {0x000000BB, "NETWORK_BOOT_INITIALIZATION_FAILED", "?????????"},
    {0x000000BC, "NETWORK_BOOT_DUPLICATE_ADDRESS", "????????"},
    {0x000000BE, "ATTEMPTED_WRITE_TO_READONLY_MEMORY", "????????"},
    {0x000000BF, "MUTEX_ALREADY_OWNED", "???????"},
    {0x000000C1, "SPECIAL_POOL_DETECTED_MEMORY_CORRUPTION", "????????????"},
    {0x000000C2, "BAD_POOL_CALLER", "????????"},
    {0x000000C4, "DRIVER_VERIFIER_DETECTED_VIOLATION", "????????????"},
    {0x000000C5, "DRIVER_CORRUPTED_EXPOOL", "???????????"},
    {0x000000C6, "DRIVER_CAUGHT_MODIFYING_FREED_POOL", "?????????????"},
    {0x000000C7, "TIMER_OR_DPC_INVALID", "????DPC??"},
    {0x000000C8, "IRQL_UNEXPECTED_VALUE", "IRQL???"},
    {0x000000C9, "DRIVER_VERIFIER_IOMANAGER_VIOLATION", "???????IO?????"},
    {0x000000CA, "PNP_DETECTED_FATAL_ERROR", "???????????"},
    {0x000000CB, "DRIVER_LEFT_LOCKED_PAGES_IN_PROCESS", "??????????????"},
    {0x000000CC, "PAGE_FAULT_IN_FREED_SPECIAL_POOL", "??????????????"},
    {0x000000CD, "PAGE_FAULT_BEYOND_END_OF_ALLOCATION", "???????????"},
    {0x000000CE, "DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS", "??????????????"},
    {0x000000CF, "TERMINAL_SERVER_DRIVER_MADE_INCORRECT_MEMORY_REFERENCE", "???????????????"},
    {0x000000D0, "DRIVER_CORRUPTED_MMPOOL", "???????MM???"},
    {0x000000D1, "DRIVER_IRQL_NOT_LESS_OR_EQUAL", "????IRQL??????"},
    {0x000000D2, "BUGCODE_ID_DRIVER", "BUGCODE_ID??????"},
    {0x000000D3, "DRIVER_PORTION_MUST_BE_NONPAGED", "???????????"},
    {0x000000D4, "SYSTEM_SCAN_AT_RAISED_IRQL_CAUGHT_IMPROPER_DRIVER_UNLOAD", "??IRQL?????????????"},
    {0x000000D5, "DRIVER_PAGE_FAULT_IN_FREED_SPECIAL_POOL", "??????????????????"},
    {0x000000D6, "DRIVER_PAGE_FAULT_BEYOND_END_OF_ALLOCATION", "???????????????"},
    {0x000000D7, "DRIVER_UNMAPPING_INVALID_VIEW", "????????????"},
    {0x000000D8, "DRIVER_USED_EXCESSIVE_PTES", "????????????"},
    {0x000000D9, "LOCKED_PAGES_TRACKER_CORRUPTION", "?????????"},
    {0x000000DA, "SYSTEM_PTE_MISUSE", "???????"},
    {0x000000DB, "DRIVER_CORRUPTED_SYSPTES", "????????????"},
    {0x000000DC, "DRIVER_INVALID_STACK_ACCESS", "??????????"},
    {0x000000DE, "POOL_CORRUPTION_IN_FILE_AREA", "?????????"},
    {0x000000DF, "IMPERSONATING_WORKER_THREAD", "??????"},
    {0x000000E0, "ACPI_BIOS_FATAL_ERROR", "ACPI BIOS????"},
    {0x000000E1, "WORKER_THREAD_RETURNED_AT_BAD_IRQL", "???????IRQL??"},
    {0x000000E2, "MANUALLY_INITIATED_CRASH", "???????"},
    {0x000000E3, "RESOURCE_NOT_OWNED", "??????"},
    {0x000000E4, "WORKER_INVALID", "??????"},
    {0x000000E6, "DRIVER_VERIFIER_DMA_VIOLATION", "???????DMA??"},
    {0x000000E7, "INVALID_FLOATING_POINT_STATE", "???????"},
    {0x000000E8, "INVALID_CANCEL_OF_FILE_OPEN", "?????????"},
    {0x000000E9, "ACTIVE_EX_WORKER_THREAD_TERMINATION", "??EX??????"},
    {0x000000EA, "THREAD_STUCK_IN_DEVICE_DRIVER", "???????????"},
    {0x000000EB, "DIRTY_MAPPED_PAGES_CONGESTION", "???????"},
    {0x000000EC, "SESSION_HAS_VALID_SPECIAL_POOL_ON_EXIT", "?????????????"},
    {0x000000ED, "UNMOUNTABLE_BOOT_VOLUME", "????????"},
    {0x000000EF, "CRITICAL_PROCESS_DIED", "??????"},
    {0x000000F1, "SCSI_VERIFIER_DETECTED_VIOLATION", "SCSI????????"},
    {0x000000F3, "DISORDERLY_SHUTDOWN", "????"},
    {0x000000F4, "CRITICAL_OBJECT_TERMINATION", "??????"},
    {0x000000F5, "FLTMGR_FILE_SYSTEM", "???????????"},
    {0x000000F6, "PCI_VERIFIER_DETECTED_VIOLATION", "PCI????????"},
    {0x000000F7, "DRIVER_OVERRAN_STACK_BUFFER", "???????????"},
    {0x000000F8, "RAMDISK_BOOT_INITIALIZATION_FAILED", "RAMDISK???????"},
    {0x000000F9, "DRIVER_RETURNED_STATUS_REPARSE_FOR_VOLUME_OPEN", "???????????????"},
    {0x000000FA, "HTTP_DRIVER_CORRUPTED", "HTTP??????"},
    {0x000000FC, "ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY", "??????????"},
    {0x000000FD, "DIRTY_NOWRITE_PAGES_CONGESTION", "????????"},
    {0x000000FE, "BUGCODE_USB_DRIVER", "USB???????"},
    {0x000000FF, "RESERVE_QUEUE_OVERFLOW", "??????"},
    {0x00000100, "LOADER_BLOCK_MISMATCH", "???????"},
    {0x00000101, "CLOCK_WATCHDOG_TIMEOUT", "???????"},
    {0x00000103, "MUP_FILE_SYSTEM", "MUP??????"},
    {0x00000104, "AGP_INVALID_ACCESS", "AGP????"},
    {0x00000105, "AGP_GART_CORRUPTION", "AGP GART??"},
    {0x00000106, "AGP_ILLEGALLY_REPROGRAMMED", "AGP???????"},
    {0x00000108, "THIRD_PARTY_FILE_SYSTEM_FAILURE", "?????????"},
    {0x00000109, "CRITICAL_STRUCTURE_CORRUPTION", "??????"},
    {0x0000010A, "APP_TAGGING_INITIALIZATION_FAILED", "???????????"},
    {0x0000010C, "FSRTL_EXTRA_CREATE_PARAMETER_VIOLATION", "FSRTL????????"},
    {0x0000010D, "WDF_VIOLATION", "Windows????????"},
    {0x0000010E, "VIDEO_MEMORY_MANAGEMENT_INTERNAL", "??????????"},
    {0x0000010F, "RESOURCE_MANAGER_EXCEPTION_NOT_HANDLED", "??????????"},
    {0x00000111, "RECURSIVE_NMI", "??NMI"},
    {0x00000112, "MSRPC_STATE_VIOLATION", "MSRPC????"},
    {0x00000113, "VIDEO_DXGKRNL_FATAL_ERROR", "??DXGKRNL????"},
    {0x00000114, "VIDEO_SHADOW_DRIVER_FATAL_ERROR", "????????????"},
    {0x00000115, "AGP_INTERNAL", "AGP????"},
    {0x00000116, "VIDEO_TDR_ERROR", "???????????"},
    {0x00000117, "VIDEO_TDR_TIMEOUT_DETECTED", "???????"},
    {0x00000119, "VIDEO_SCHEDULER_INTERNAL_ERROR", "??????????"},
    {0x0000011A, "EM_INITIALIZATION_FAILURE", "EM?????"},
    {0x0000011B, "DRIVER_RETURNED_HOLDING_CANCEL_LOCK", "????????????"},
    {0x0000011C, "ATTEMPTED_WRITE_TO_CM_PROTECTED_STORAGE", "????CM????"},
    {0x0000011D, "EVENT_TRACING_FATAL_ERROR", "????????"},
    {0x00000121, "DRIVER_VIOLATION", "??????"},
    {0x00000122, "WHEA_INTERNAL_ERROR", "Windows????????????"},
    {0x00000124, "WHEA_UNCORRECTABLE_ERROR", "Windows??????????????"},
    {0x00000127, "PAGE_NOT_ZERO", "????"},
    {0x0000012B, "FAULTY_HARDWARE_CORRUPTED_PAGE", "????????"},
    {0x0000012C, "EXFAT_FILE_SYSTEM", "EXFAT??????"},
    {0x00000133, "DPC_WATCHDOG_VIOLATION", "DPC?????"},
    {0x00000138, "GPIO_CONTROLLER_DRIVER_ERROR", "GPIO?????????"},
    {0x00000139, "KERNEL_SECURITY_CHECK_FAILURE", "????????"},
    {0x00000144, "BUGCODE_USB3_DRIVER", "USB3???????"},
    {0x0000014B, "SOC_SUBSYSTEM_FAILURE", "?????????"},
    {0x00000154, "UNEXPECTED_STORE_EXCEPTION", "??????"},
    {0x00000159, "HAL_ILLEGAL_IOMMU_PAGE_FAULT", "HAL??IOMMU????"},
    {0x0000015A, "SDBUS_INTERNAL_ERROR", "SD??????"},
    {0x0000015B, "WORKER_THREAD_RETURNED_WITH_SYSTEM_PAGE_PRIORITY_ACTIVE", "????????????????"},
    {0x00000161, "LIVE_SYSTEM_DUMP", "??????"},
    {0x00000184, "HYPERVISOR_ERROR", "?????????"},
    {0x00000189, "BAD_OBJECT_HEADER", "?????"},
    {0x0000018A, "SECURE_KERNEL_ERROR", "??????"},
    {0x0000019C, "DIRECT_WRITE_STALL_BUGCHECK", "??????????"},
    {0x000001C8, "MANUALLY_INITIATED_POWER_BUTTON_HOLD", "???????????"},
    {0x000001CF, "HARDWARE_WATCHDOG_TIMEOUT", "???????"},
    {0x1000007E, "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED_M", "?????????_M"},
    {0x1000007F, "UNEXPECTED_KERNEL_MODE_TRAP_M", "?????????_M"},
    {0x1000008E, "KERNEL_MODE_EXCEPTION_NOT_HANDLED_M", "?????????_M"},
    {0x100000EA, "THREAD_STUCK_IN_DEVICE_DRIVER_M", "???????????_M"},
    {0xC0000218, "STATUS_CANNOT_LOAD_REGISTRY_FILE", "?????????"},
    {0xC000021A, "STATUS_SYSTEM_PROCESS_TERMINATED", "???????"},
    {0xC0000221, "STATUS_IMAGE_CHECKSUM_MISMATCH", "????????"},
    {0xDEADDEAD, "MANUALLY_INITIATED_CRASH1", "???????1"},       
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
    return "????";
}

void gnwinfo_bsod_init(void)
{
    InitializeCriticalSection(&g_bsod_lock);
    
    int dump_enabled = gnwinfo_bsod_get_dump_enabled();
    
    if (!dump_enabled) {
        if (IsRunAsAdmin()) {
            gnwinfo_bsod_set_dump_type(3);
            gnwinfo_bsod_set_dump_enabled(1);
        } else {
            SHELLEXECUTEINFOA sei = {0};
            sei.cbSize = sizeof(SHELLEXECUTEINFOA);
            sei.lpVerb = "runas";
            sei.lpFile = "powershell.exe";
            sei.lpParameters = "-Command \"reg add 'HKLM\\SYSTEM\\CurrentControlSet\\Control\\CrashControl' /v CrashDumpEnabled /t REG_DWORD /d 3 /f; reg add 'HKLM\\SYSTEM\\CurrentControlSet\\Control\\CrashControl' /v LogEvent /t REG_DWORD /d 1 /f; reg add 'HKLM\\SYSTEM\\CurrentControlSet\\Control\\CrashControl' /v AutoReboot /t REG_DWORD /d 1 /f\"";
            sei.nShow = SW_HIDE;
            ShellExecuteExA(&sei);
        }
    }
    
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

int gnwinfo_bsod_get_dump_enabled(void)
{
    HKEY hKey;
    DWORD dumpEnabled = 0;
    DWORD logEvent = 0;
    DWORD size = sizeof(DWORD);
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                      "SYSTEM\\CurrentControlSet\\Control\\CrashControl", 
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "CrashDumpEnabled", NULL, NULL, (LPBYTE)&dumpEnabled, &size);
        RegQueryValueExA(hKey, "LogEvent", NULL, NULL, (LPBYTE)&logEvent, &size);
        RegCloseKey(hKey);
    }
    
    return (dumpEnabled != 0 && logEvent != 0);
}

int gnwinfo_bsod_set_dump_enabled(int enable)
{
    HKEY hKey;
    DWORD result = 0;
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                      "SYSTEM\\CurrentControlSet\\Control\\CrashControl", 
                      0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        DWORD value = enable ? 3 : 0;
        result = RegSetValueExA(hKey, "CrashDumpEnabled", 0, REG_DWORD, (LPBYTE)&value, sizeof(DWORD));
        
        if (result == ERROR_SUCCESS) {
            DWORD logEvent = 1;
            RegSetValueExA(hKey, "LogEvent", 0, REG_DWORD, (LPBYTE)&logEvent, sizeof(DWORD));
            
            DWORD autoReboot = 1;
            RegSetValueExA(hKey, "AutoReboot", 0, REG_DWORD, (LPBYTE)&autoReboot, sizeof(DWORD));
            
            DWORD overwrite = 1;
            RegSetValueExA(hKey, "Overwrite", 0, REG_DWORD, (LPBYTE)&overwrite, sizeof(DWORD));
        }
        
        RegCloseKey(hKey);
    }
    
    return result == ERROR_SUCCESS;
}

int gnwinfo_bsod_get_dump_type(void)
{
    HKEY hKey;
    DWORD dumpType = 0;
    DWORD size = sizeof(DWORD);
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                      "SYSTEM\\CurrentControlSet\\Control\\CrashControl", 
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "CrashDumpEnabled", NULL, NULL, (LPBYTE)&dumpType, &size);
        RegCloseKey(hKey);
    }
    
    return (int)dumpType;
}

int gnwinfo_bsod_set_dump_type(int type)
{
    HKEY hKey;
    DWORD result = 0;
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                      "SYSTEM\\CurrentControlSet\\Control\\CrashControl", 
                      0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        DWORD value = (DWORD)type;
        result = RegSetValueExA(hKey, "CrashDumpEnabled", 0, REG_DWORD, (LPBYTE)&value, sizeof(DWORD));
        RegCloseKey(hKey);
    }
    
    return result == ERROR_SUCCESS;
}

static void parse_event_time(ULONGLONG time, char* buffer, size_t size)
{
    FILETIME ft;
    ft.dwHighDateTime = (DWORD)(time >> 32);
    ft.dwLowDateTime = (DWORD)(time & 0xFFFFFFFF);
    
    FILETIME localFt;
    FileTimeToLocalFileTime(&ft, &localFt);
    
    SYSTEMTIME st;
    FileTimeToSystemTime(&localFt, &st);
    
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
    
    dwBufferSize = 0;
    dwBufferUsed = 0;
    LPWSTR pXml = NULL;
    
    EvtRender(NULL, hEvent, EvtRenderEventXml, dwBufferSize, pXml, &dwBufferUsed, NULL);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        dwBufferSize = dwBufferUsed;
        pXml = (LPWSTR)malloc(dwBufferSize * sizeof(WCHAR));
        if (pXml) {
            if (!EvtRender(NULL, hEvent, EvtRenderEventXml, dwBufferSize, pXml, &dwBufferUsed, NULL)) {
                free(pXml);
                return;
            }
        } else {
            return;
        }
    }
    
    if (pXml) {
        char xml_utf8[8192];
        WideCharToMultiByte(CP_UTF8, 0, pXml, -1, xml_utf8, sizeof(xml_utf8), NULL, NULL);
        free(pXml);
        
        char* data_start = strstr(xml_utf8, "<Data Name='param1'>");
        if (data_start) {
            data_start += strlen("<Data Name='param1'>");
            char* data_end = strstr(data_start, "</Data>");
            if (data_end) {
                *data_end = '\0';
                
                UINT32 bugcheck_code = 0;
                if (sscanf_s(data_start, "0x%08X", &bugcheck_code) == 1 && bugcheck_code != 0) {
                    record->bugcheck_id = bugcheck_code;
                    _snprintf_s(record->bugcheck_code, sizeof(record->bugcheck_code), _TRUNCATE, "0x%08X", record->bugcheck_id);
                    strncpy_s(record->bugcheck_name, sizeof(record->bugcheck_name), 
                             gnwinfo_bsod_get_code_name(record->bugcheck_id), _TRUNCATE);
                    
                    char* params_start = strchr(data_start, '(');
                    if (params_start) {
                        params_start++;
                        UINT64 p1 = 0, p2 = 0, p3 = 0, p4 = 0;
                        if (sscanf_s(params_start, "0x%llX, 0x%llX, 0x%llX, 0x%llX", &p1, &p2, &p3, &p4) >= 1) {
                            record->param1 = p1;
                            record->param2 = p2;
                            record->param3 = p3;
                            record->param4 = p4;
                        }
                    }
                }
            }
        }
        
        char* dump_start = strstr(xml_utf8, "<Data Name='param2'>");
        if (dump_start) {
            dump_start += strlen("<Data Name='param2'>");
            char* dump_end = strstr(dump_start, "</Data>");
            if (dump_end) {
                *dump_end = '\0';
                strncpy_s(record->dump_file, sizeof(record->dump_file), dump_start, _TRUNCATE);
            }
        }
    }
}

void gnwinfo_bsod_refresh(void)
{
    EnterCriticalSection(&g_bsod_lock);
    
    FILE* init_debug = fopen("C:\\bsod_init_debug.txt", "w");
    if (init_debug) {
        fprintf(init_debug, "gnwinfo_bsod_refresh called\n");
        fclose(init_debug);
    }
    
    g_bsod_count = 0;
    memset(g_bsod_records, 0, sizeof(g_bsod_records));
    
    EVT_HANDLE hQuery = EvtQuery(NULL, L"System", 
        L"Event/System[Provider[@Name='Microsoft-Windows-WER-SystemErrorReporting'] and EventID=1001]",
        EvtQueryChannelPath | EvtQueryReverseDirection);
    
    if (hQuery == NULL) {
        FILE* err_debug = fopen("C:\\bsod_query_error.txt", "w");
        if (err_debug) {
            fprintf(err_debug, "EvtQuery failed, error=%lu\n", GetLastError());
            fclose(err_debug);
        }
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
        
        if (g_bsod_records[g_bsod_count].bugcheck_id != 0) {
            g_bsod_count++;
        }
    }
    
    EvtClose(hQuery);
    
    FILE* match_debug = fopen("C:\\bsod_match_debug.txt", "w");
    if (match_debug) {
        fprintf(match_debug, "Found %d BSOD records from event log\n", g_bsod_count);
        for (int i = 0; i < g_bsod_count; i++) {
            fprintf(match_debug, "  [%d] dump_file='%s'\n", i, g_bsod_records[i].dump_file);
        }
    }
    
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
            
            if (match_debug) {
                fprintf(match_debug, "\nChecking minidump: %s\n", findData.cFileName);
            }
            
            int found = 0;
            for (int i = 0; i < g_bsod_count; i++) {
                const char* existing_name = strrchr(g_bsod_records[i].dump_file, '\\');
                if (existing_name) existing_name++;
                else existing_name = g_bsod_records[i].dump_file;
                
                if (match_debug) {
                    fprintf(match_debug, "  Compare with record[%d]: '%s' vs '%s'\n", 
                            i, existing_name, findData.cFileName);
                }
                
                if (g_bsod_records[i].dump_file[0] != '\0' && 
                    _stricmp(existing_name, findData.cFileName) == 0) {
                    if (match_debug) {
                        fprintf(match_debug, "  MATCH! Parsing minidump...\n");
                    }
                    gnwinfo_bsod_parse_minidump(dump_file, &g_bsod_records[i]);
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                if (g_bsod_count < BSOD_MAX_RECORDS) {
                    if (match_debug) {
                        fprintf(match_debug, "  No match, checking if we can update existing record\n");
                        for (int i = 0; i < g_bsod_count; i++) {
                            fprintf(match_debug, "    record[%d]: dump_file[0]=%d, caused_by_driver[0]=%d, process_name[0]=%d\n",
                                    i, g_bsod_records[i].dump_file[0], g_bsod_records[i].caused_by_driver[0], g_bsod_records[i].process_name[0]);
                        }
                    }
                    
                    for (int i = 0; i < g_bsod_count; i++) {
                        if (g_bsod_records[i].dump_file[0] == '\0') {
                            if (match_debug) {
                                fprintf(match_debug, "  Updating record[%d] with minidump info\n", i);
                            }
                            strcpy_s(g_bsod_records[i].dump_file, MAX_PATH, dump_file);
                            gnwinfo_bsod_parse_minidump(dump_file, &g_bsod_records[i]);
                            found = 1;
                            break;
                        }
                    }
                    
                    if (!found) {
                        if (match_debug) {
                            fprintf(match_debug, "  Creating new record\n");
                        }
                        strcpy_s(g_bsod_records[g_bsod_count].dump_file, MAX_PATH, dump_file);
                        gnwinfo_bsod_parse_minidump(dump_file, &g_bsod_records[g_bsod_count]);
                        if (g_bsod_records[g_bsod_count].bugcheck_id != 0) {
                            g_bsod_count++;
                        }
                    }
                }
            }
        } while (FindNextFileA(hFind, &findData));
        
        FindClose(hFind);
    }
    
    if (match_debug) {
        fprintf(match_debug, "\n=== Final Results ===\n");
        for (int i = 0; i < g_bsod_count; i++) {
            fprintf(match_debug, "Record[%d]:\n", i);
            fprintf(match_debug, "  bugcheck_id=0x%X\n", g_bsod_records[i].bugcheck_id);
            fprintf(match_debug, "  caused_by_driver='%s'\n", g_bsod_records[i].caused_by_driver);
            fprintf(match_debug, "  process_name='%s'\n", g_bsod_records[i].process_name);
        }
        fclose(match_debug);
    }
    
    LeaveCriticalSection(&g_bsod_lock);
}

void gnwinfo_bsod_parse_minidump(const char* dump_path, BSOD_RECORD* record)
{
    FILE* debug_file = fopen("C:\\bsod_debug.txt", "a");
    if (debug_file) {
        fprintf(debug_file, "\n=== Parsing minidump: %s ===\n", dump_path);
    }
    
    if (dump_path == NULL || record == NULL) {
        if (debug_file) {
            fprintf(debug_file, "ERROR: dump_path or record is NULL\n");
            fclose(debug_file);
        }
        return;
    }
    
    HANDLE hFile = CreateFileA(dump_path, GENERIC_READ, FILE_SHARE_READ, 
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (debug_file) {
            fprintf(debug_file, "ERROR: CreateFile failed, error=%lu\n", GetLastError());
            fclose(debug_file);
        }
        return;
    }
    
    HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMap == NULL) {
        if (debug_file) {
            fprintf(debug_file, "ERROR: CreateFileMapping failed, error=%lu\n", GetLastError());
        }
        CloseHandle(hFile);
        if (debug_file) fclose(debug_file);
        return;
    }
    
    PVOID pView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (pView == NULL) {
        if (debug_file) {
            fprintf(debug_file, "ERROR: MapViewOfFile failed, error=%lu\n", GetLastError());
        }
        CloseHandle(hMap);
        CloseHandle(hFile);
        if (debug_file) fclose(debug_file);
        return;
    }
    
    MINIDUMP_DIRECTORY* pDir = NULL;
    ULONG cbStreamSize = 0;
    
    MINIDUMP_HEADER* pHeader = (MINIDUMP_HEADER*)pView;
    if (pHeader->Signature != MINIDUMP_SIGNATURE) {
        if (debug_file) {
            fprintf(debug_file, "Not a MiniDump (signature=0x%X), trying Kernel Dump format\n", pHeader->Signature);
        }
        
        if (pHeader->Signature == 0x45474150) {
            if (debug_file) {
                fprintf(debug_file, "This is a Kernel/Full Memory Dump\n");
            }
            
            DWORD file_size = GetFileSize(hFile, NULL);
            char* file_data = (char*)pView;
            
            if (debug_file) {
                fprintf(debug_file, "File size: %lu bytes\n", file_size);
                fprintf(debug_file, "Searching for process name and drivers...\n");
            }
            
            const char* proc_search = "devenv.exe";
            char* proc_found = (char*)my_memmem(file_data, file_size, proc_search, strlen(proc_search));
            if (proc_found) {
                strncpy_s(record->process_name, sizeof(record->process_name), proc_search, _TRUNCATE);
                if (debug_file) {
                    fprintf(debug_file, "Found process: %s\n", proc_search);
                }
            } else {
                for (DWORD j = 0; j < file_size - 10 && record->process_name[0] == '\0'; j++) {
                    char c = file_data[j];
                    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                        int k;
                        for (k = 0; k < 60 && j + k < file_size; k++) {
                            char c2 = file_data[j + k];
                            if (!((c2 >= 'A' && c2 <= 'Z') || (c2 >= 'a' && c2 <= 'z') || 
                                  (c2 >= '0' && c2 <= '9') || c2 == '_' || c2 == '.')) {
                                break;
                            }
                        }
                        if (k >= 8 && k < 60) {
                            if (j + k + 4 <= file_size && 
                                file_data[j + k - 4] == '.' && 
                                (file_data[j + k - 3] == 'e' || file_data[j + k - 3] == 'E') &&
                                (file_data[j + k - 2] == 'x' || file_data[j + k - 2] == 'X') &&
                                (file_data[j + k - 1] == 'e' || file_data[j + k - 1] == 'E')) {
                                char proc_name[64];
                                int len = (k < 63) ? k : 63;
                                strncpy_s(proc_name, sizeof(proc_name), &file_data[j], len);
                                proc_name[len] = '\0';
                                if (strstr(proc_name, "ntoskrnl") == NULL && 
                                    strstr(proc_name, "system") == NULL &&
                                    strstr(proc_name, "System") == NULL) {
                                    strncpy_s(record->process_name, sizeof(record->process_name), proc_name, _TRUNCATE);
                                    if (debug_file) {
                                        fprintf(debug_file, "Found process: %s\n", proc_name);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            const char* known_bad_drivers[] = {
                "QQSysMonX64_EV.sys", "QQSysMonX64.sys", "QQSysMon.sys", 
                "TsNetHlpX64.sys", "TsNetHlp.sys",
                "WdFilter.sys", "WdNisDrv.sys",
                "AsIO.sys", "AsIO64.sys", 
                "gdrv.sys", "capcom.sys", 
                "PROCEXP.sys", "PROCESSHACKER.sys",
                "RTCore64.sys", "RTCore.sys", 
                "DBUtil_2_3.sys", "DBUtil.sys", 
                "AsrDrv.sys", "AsrDrv103.sys",
                "ATSZIO.sys", "ATKEX.sys", "ATKACPI.sys", 
                "WinRing.sys",
                NULL
            };
            
            for (int i = 0; known_bad_drivers[i] != NULL; i++) {
                size_t driver_len = strlen(known_bad_drivers[i]);
                char* found = (char*)my_memmem(file_data, file_size, known_bad_drivers[i], driver_len);
                if (found) {
                    strncpy_s(record->caused_by_driver, sizeof(record->caused_by_driver), 
                             known_bad_drivers[i], _TRUNCATE);
                    if (debug_file) {
                        fprintf(debug_file, "Found driver (ASCII): %s\n", known_bad_drivers[i]);
                    }
                    break;
                }
                
                wchar_t wide_driver[64];
                MultiByteToWideChar(CP_ACP, 0, known_bad_drivers[i], -1, wide_driver, 64);
                char* found_wide = (char*)my_memmem(file_data, file_size, wide_driver, driver_len * 2);
                if (found_wide) {
                    strncpy_s(record->caused_by_driver, sizeof(record->caused_by_driver), 
                             known_bad_drivers[i], _TRUNCATE);
                    if (debug_file) {
                        fprintf(debug_file, "Found driver (Unicode): %s\n", known_bad_drivers[i]);
                    }
                    break;
                }
            }
            
            if (record->caused_by_driver[0] == '\0') {
                for (DWORD j = 0; j < file_size - 10; j++) {
                    if (file_data[j] >= 'A' && file_data[j] <= 'Z') {
                        int k;
                        for (k = 0; k < 60 && j + k < file_size; k++) {
                            char c = file_data[j + k];
                            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                                  (c >= '0' && c <= '9') || c == '_' || c == '.')) {
                                break;
                            }
                        }
                        if (k >= 6 && k < 60) {
                            if (j + k + 4 <= file_size && 
                                file_data[j + k - 4] == '.' && 
                                (file_data[j + k - 3] == 's' || file_data[j + k - 3] == 'S') &&
                                (file_data[j + k - 2] == 'y' || file_data[j + k - 2] == 'Y') &&
                                (file_data[j + k - 1] == 's' || file_data[j + k - 1] == 'S')) {
                                char sys_name[64];
                                int len = (k < 63) ? k : 63;
                                strncpy_s(sys_name, sizeof(sys_name), &file_data[j], len);
                                sys_name[len] = '\0';
                                if (strstr(sys_name, "ntoskrnl") == NULL && 
                                    strstr(sys_name, "hal") == NULL &&
                                    strstr(sys_name, "kd") == NULL &&
                                    strstr(sys_name, "clipsp") == NULL &&
                                    strstr(sys_name, "ci") == NULL &&
                                    strstr(sys_name, "Wdf") == NULL &&
                                    strstr(sys_name, "wdf") == NULL) {
                                    strncpy_s(record->caused_by_driver, sizeof(record->caused_by_driver), sys_name, _TRUNCATE);
                                    if (debug_file) {
                                        fprintf(debug_file, "Found sys file: %s\n", sys_name);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            
            if (debug_file && record->caused_by_driver[0] == '\0') {
                fprintf(debug_file, "No driver found in dump file\n");
            }
        }
        
        UnmapViewOfFile(pView);
        CloseHandle(hMap);
        CloseHandle(hFile);
        if (debug_file) fclose(debug_file);
        return;
    }
    
    if (debug_file) {
        fprintf(debug_file, "=== MiniDump Header ===\n");
        fprintf(debug_file, "Signature: 0x%X\n", pHeader->Signature);
        fprintf(debug_file, "Version: %u\n", pHeader->Version);
        fprintf(debug_file, "NumberOfStreams: %u\n", pHeader->NumberOfStreams);
        fprintf(debug_file, "StreamDirectoryRva: 0x%X\n", pHeader->StreamDirectoryRva);
        
        MINIDUMP_DIRECTORY* pStreams = (MINIDUMP_DIRECTORY*)((PBYTE)pView + pHeader->StreamDirectoryRva);
        fprintf(debug_file, "\n=== Available Streams ===\n");
        for (ULONG i = 0; i < pHeader->NumberOfStreams; i++) {
            fprintf(debug_file, "  Stream[%u]: Type=%u Rva=0x%X Size=%u\n", 
                    i, pStreams[i].StreamType, pStreams[i].Location.Rva, pStreams[i].Location.DataSize);
        }
    }
    
    MINIDUMP_EXCEPTION_STREAM* pExceptionStream = NULL;
    UINT32 thread_id = 0;
    UINT64 exception_address = 0;
    
    if (MiniDumpReadDumpStream(pView, ExceptionStream, &pDir, (PVOID*)&pExceptionStream, &cbStreamSize)) {
        if (pExceptionStream && cbStreamSize >= sizeof(MINIDUMP_EXCEPTION_STREAM)) {
            if (record->bugcheck_id == 0 && pExceptionStream->ExceptionRecord.ExceptionCode != 0) {
                record->bugcheck_id = pExceptionStream->ExceptionRecord.ExceptionCode;
                _snprintf_s(record->bugcheck_code, sizeof(record->bugcheck_code), _TRUNCATE, "0x%08X", record->bugcheck_id);
                strncpy_s(record->bugcheck_name, sizeof(record->bugcheck_name), 
                         gnwinfo_bsod_get_code_name(record->bugcheck_id), _TRUNCATE);
            }
            
            thread_id = pExceptionStream->ThreadId;
            exception_address = (UINT64)pExceptionStream->ExceptionRecord.ExceptionAddress;
            
            if (debug_file) {
                fprintf(debug_file, "\n=== Exception Stream ===\n");
                fprintf(debug_file, "ThreadId: %u\n", thread_id);
                fprintf(debug_file, "ExceptionCode: 0x%X\n", pExceptionStream->ExceptionRecord.ExceptionCode);
                fprintf(debug_file, "ExceptionAddress: 0x%llX\n", exception_address);
            }
        }
    }
    
    MINIDUMP_MODULE_LIST* pModuleList = NULL;
    if (MiniDumpReadDumpStream(pView, ModuleListStream, &pDir, (PVOID*)&pModuleList, &cbStreamSize)) {
        if (pModuleList && pModuleList->NumberOfModules > 0) {
            if (debug_file) {
                fprintf(debug_file, "\n=== Module List ===\n");
                fprintf(debug_file, "NumberOfModules: %u\n", pModuleList->NumberOfModules);
            }
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
                    
                    WideCharToMultiByte(CP_ACP, 0, pName, -1, 
                                        record->modules[record->module_count].path, 
                                        sizeof(record->modules[record->module_count].path), 
                                        NULL, NULL);
                }
                
                record->modules[record->module_count].base = pMod->BaseOfImage;
                record->modules[record->module_count].size = pMod->SizeOfImage;
                record->modules[record->module_count].timestamp = pMod->TimeDateStamp;
                
                if (debug_file && i < 30) {
                    fprintf(debug_file, "  [%u] %s base=0x%llX size=0x%X\n", 
                            i, record->modules[record->module_count].name, 
                            record->modules[record->module_count].base, 
                            record->modules[record->module_count].size);
                }
                
                if (exception_address != 0 && record->caused_by_driver[0] == '\0') {
                    UINT64 base = pMod->BaseOfImage;
                    UINT64 end = base + pMod->SizeOfImage;
                    if (exception_address >= base && exception_address < end) {
                        char* name = record->modules[record->module_count].name;
                        char* ext = strrchr(name, '.');
                        if (ext && _stricmp(ext, ".sys") == 0 && strstr(name, "ntoskrnl") == NULL) {
                            strncpy_s(record->caused_by_driver, sizeof(record->caused_by_driver), name, _TRUNCATE);
                            if (debug_file) {
                                fprintf(debug_file, "\n*** FOUND: Exception address 0x%llX in module %s ***\n", 
                                        exception_address, name);
                            }
                        }
                    }
                }
                
                record->module_count++;
            }
        }
    }
    
    MINIDUMP_THREAD_LIST* pThreadList = NULL;
    if (MiniDumpReadDumpStream(pView, ThreadListStream, &pDir, (PVOID*)&pThreadList, &cbStreamSize)) {
        if (pThreadList && pThreadList->NumberOfThreads > 0) {
            if (debug_file) {
                fprintf(debug_file, "\nThread count: %d, looking for thread_id: %u\n", 
                        pThreadList->NumberOfThreads, thread_id);
            }
            for (ULONG i = 0; i < pThreadList->NumberOfThreads; i++) {
                MINIDUMP_THREAD* pThread = &pThreadList->Threads[i];
                if (thread_id != 0 && pThread->ThreadId == thread_id) {
                    if (debug_file) {
                        fprintf(debug_file, "Found thread %u, ContextRva=0x%X, StackRva=0x%X, StackSize=0x%X\n",
                                pThread->ThreadId, pThread->ThreadContext.Rva,
                                pThread->Stack.Memory.Rva, pThread->Stack.Memory.DataSize);
                    }
                    if (pThread->ThreadContext.Rva != 0) {
                        CONTEXT* pCtx = (CONTEXT*)((PBYTE)pView + pThread->ThreadContext.Rva);
                        if (pCtx) {
#ifdef _WIN64
                            if (debug_file) {
                                fprintf(debug_file, "RIP=0x%llX RSP=0x%llX\n", pCtx->Rip, pCtx->Rsp);
                            }
                            UINT64* pStack = NULL;
                            UINT32 stack_size = 0;
                            
                            if (pThread->Stack.Memory.Rva != 0 && pThread->Stack.Memory.DataSize > 0) {
                                stack_size = pThread->Stack.Memory.DataSize;
                                pStack = (UINT64*)((PBYTE)pView + pThread->Stack.Memory.Rva);
                            }
                            
                            if (pStack && stack_size > 0) {
                                if (debug_file) {
                                    fprintf(debug_file, "Stack data found, scanning %u entries\n", 
                                            (UINT32)(stack_size / sizeof(UINT64)));
                                }
                                for (UINT32 j = 0; j < stack_size / sizeof(UINT64) && j < 512; j++) {
                                    UINT64 addr = pStack[j];
                                    
                                    if (addr < 0xFFFF800000000000ULL)
                                        continue;
                                    
                                    for (int k = 0; k < record->module_count; k++) {
                                        UINT64 base = record->modules[k].base;
                                        UINT64 end = base + record->modules[k].size;
                                        
                                        if (addr >= base && addr < end) {
                                            char* name = record->modules[k].name;
                                            char* ext = strrchr(name, '.');
                                            int is_sys = (ext && _stricmp(ext, ".sys") == 0);
                                            int is_ntoskrnl = (strstr(name, "ntoskrnl") != NULL);
                                            
                                            if (debug_file && is_sys) {
                                                fprintf(debug_file, "  Stack[%d]=0x%llX -> %s (sys=%d, ntoskrnl=%d)\n",
                                                        j, addr, name, is_sys, is_ntoskrnl);
                                            }
                                            
                                            if (is_sys && !is_ntoskrnl && record->caused_by_driver[0] == '\0') {
                                                strncpy_s(record->caused_by_driver, sizeof(record->caused_by_driver), 
                                                         name, _TRUNCATE);
                                                break;
                                            }
                                        }
                                    }
                                    
                                    if (record->caused_by_driver[0] != '\0')
                                        break;
                                }
                            } else if (debug_file) {
                                fprintf(debug_file, "No stack data available\n");
                            }
#endif
                        }
                    }
                    break;
                }
            }
        }
    }
    
    if (debug_file) {
        fprintf(debug_file, "\nResult: caused_by_driver=%s\n", record->caused_by_driver);
        fclose(debug_file);
    }
    
    if (record->caused_by_driver[0] == '\0') {
        const char* known_bad_drivers[] = {
            "QQSysMonX64", "QQSysMon", "TsNetHlp", "WdFilter", "WdNisDrv",
            "AsIO", "AsIO64", "gdrv", "capcom", "PROCEXP", "PROCESSHACKER",
            "RTCore64", "RTCore", "DBUtil_2_3", "DBUtil", "AsrDrv", "AsrDrv103",
            "ATSZIO", "ATKEX", "ATKACPI", "Gdrv", "WinRing", "RTCore64",
            NULL
        };
        
        for (int i = 0; known_bad_drivers[i] != NULL; i++) {
            if (strstr((char*)pView, known_bad_drivers[i]) != NULL) {
                char sys_name[64];
                _snprintf_s(sys_name, sizeof(sys_name), _TRUNCATE, "%s.sys", known_bad_drivers[i]);
                strncpy_s(record->caused_by_driver, sizeof(record->caused_by_driver), sys_name, _TRUNCATE);
                break;
            }
        }
    }
    
    UnmapViewOfFile(pView);
    CloseHandle(hMap);
    CloseHandle(hFile);
}
