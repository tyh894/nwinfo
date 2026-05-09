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
    const char* diagnosis;
} g_bsod_codes[] = {
    {0x00000001, "APC_INDEX_MISMATCH", "APC索引不匹配", "通常由驱动程序在持有锁的情况下尝试执行APC操作导致。建议更新或重新安装相关驱动程序，特别是显卡驱动或声卡驱动。"},
    {0x00000002, "DEVICE_QUEUE_NOT_BUSY", "设备队列不忙", "驱动程序尝试访问一个不忙的设备队列。可能是驱动程序问题，建议更新硬件驱动。"},
    {0x00000003, "INVALID_AFFINITY_SET", "无效的处理器亲和性设置", "程序尝试将线程设置到不存在的CPU核心上运行。检查应用程序是否与CPU核心数兼容。"},
    {0x00000004, "INVALID_DATA_ACCESS_TRAP", "无效的数据访问陷阱", "处理器遇到无效的数据访问。可能是内存损坏或驱动程序错误。"},
    {0x00000005, "INVALID_PROCESS_ATTACH_ATTEMPT", "无效的进程附加尝试", "尝试将进程附加到已附加的进程。"},
    {0x00000006, "INVALID_PROCESS_DETACH_ATTEMPT", "无效的进程分离尝试", "尝试分离未附加的进程。"},
    {0x00000007, "INVALID_SOFTWARE_INTERRUPT", "无效的软件中断", "软件中断处理程序无效。"},
    {0x00000008, "IRQL_NOT_DISPATCH_LEVEL", "IRQL未处于调度级别", "中断请求级别设置不正确。"},
    {0x00000009, "IRQL_NOT_GREATER_OR_EQUAL", "IRQL不高于或等于", "中断请求级别不匹配。"},
    {0x0000000A, "IRQL_NOT_LESS_OR_EQUAL", "IRQL不低于或等于", "驱动程序在IRQL过高时访问低IRQL内存。建议更新驱动。"},
    {0x0000000B, "NO_EXCEPTION_HANDLING_SUPPORT", "不支持异常处理", "内核不支持异常处理。"},
    {0x0000000C, "MAXIMUM_WAIT_OBJECTS_EXCEEDED", "超出最大等待对象数", "等待对象数量超过系统限制。"},
    {0x0000000D, "MUTEX_LEVEL_NUMBER_VIOLATION", "互斥锁级别违规", "互斥锁级别顺序违反。"},
    {0x0000000E, "NO_USER_MODE_CONTEXT", "无用户模式上下文", "用户模式上下文不存在。"},
    {0x0000000F, "SPIN_LOCK_ALREADY_OWNED", "自旋锁已被占用", "尝试获取已被占用的自旋锁。"},
    {0x00000010, "SPIN_LOCK_NOT_OWNED", "自旋锁未被占用", "尝试释放未被占用的自旋锁。"},
    {0x00000011, "THREAD_NOT_MUTEX_OWNER", "线程非互斥锁所有者", "线程尝试释放不拥有的互斥锁。"},
    {0x00000012, "TRAP_CAUSE_UNKNOWN", "陷阱原因未知", "处理器陷阱原因未知。"},
    {0x00000013, "EMPTY_THREAD_REAPER_LIST", "线程回收列表为空", "线程回收列表为空。"},
    {0x00000014, "CREATE_DELETE_LOCK_NOT_LOCKED", "创建/删除锁未锁定", "创建/删除锁未正确锁定。"},
    {0x00000015, "LAST_CHANCE_CALLED_FROM_KMODE", "从内核模式调用的最后机会", "内核模式异常处理程序调用了最后机会处理程序。"},
    {0x00000016, "CID_HANDLE_CREATION", "CID句柄创建", "客户端ID句柄创建失败。"},
    {0x00000017, "CID_HANDLE_DELETION", "CID句柄删除", "客户端ID句柄删除失败。"},
    {0x00000018, "REFERENCE_BY_POINTER", "指针引用错误", "通过指针引用对象时发生错误。"},
    {0x00000019, "BAD_POOL_HEADER", "内存池头损坏", "内存池头损坏。可能是驱动程序问题。"},
    {0x0000001A, "MEMORY_MANAGEMENT", "内存管理错误", "系统内存管理发生错误。可能是内存损坏或驱动程序问题。"},
    {0x0000001B, "PFN_SHARE_COUNT", "页帧号共享计数错误", "页帧号共享计数错误。"},
    {0x0000001C, "PFN_REFERENCE_COUNT", "页帧号引用计数错误", "页帧号引用计数错误。"},
    {0x0000001D, "NO_SPIN_LOCK_AVAILABLE", "无可用自旋锁", "系统无可用自旋锁。"},
    {0x0000001E, "KMODE_EXCEPTION_NOT_HANDLED", "内核模式异常未处理", "内核模式发生未处理的异常。建议更新驱动程序。"},
    {0x0000001F, "SHARED_RESOURCE_CONV_ERROR", "共享资源转换错误", "驱动程序在资源转换时发生错误。"},
    {0x00000020, "KERNEL_APC_PENDING_DURING_EXIT", "退出时内核APC未决", "线程退出时仍有未执行的APC。"},
    {0x00000021, "QUOTA_UNDERFLOW", "配额下溢", "进程配额计算出现负值。"},
    {0x00000022, "FILE_SYSTEM", "文件系统错误", "文件系统发生错误。可能由磁盘损坏、驱动程序问题或文件系统损坏引起。建议运行chkdsk检查磁盘。"},
    {0x00000023, "FAT_FILE_SYSTEM", "FAT文件系统错误", "FAT文件系统发生错误。建议备份数据并运行磁盘检查。"},
    {0x00000024, "NTFS_FILE_SYSTEM", "NTFS文件系统错误", "NTFS文件系统发生错误。可能由磁盘坏道、意外断电或文件系统损坏引起。建议运行chkdsk /f检查磁盘。"},
    {0x00000025, "NPFS_FILE_SYSTEM", "NPFS文件系统错误", "NPFS(命名管道文件系统)发生错误。"},
    {0x00000026, "CDFS_FILE_SYSTEM", "CDFS文件系统错误", "光盘文件系统发生错误。"},
    {0x00000027, "RDR_FILE_SYSTEM", "RDR文件系统错误", "远程磁盘读取器发生错误。"},
    {0x00000028, "CORRUPT_ACCESS_TOKEN", "访问令牌损坏", "进程访问令牌损坏。"},
    {0x00000029, "SECURITY_SYSTEM", "安全系统错误", "Windows安全子系统发生错误。"},
    {0x0000002A, "INCONSISTENT_IRP", "不一致的IRP", "I/O请求包(IRP)状态不一致。"},
    {0x0000002B, "PANIC_STACK_SWITCH", "紧急堆栈切换", "内核堆栈切换出现问题。"},
    {0x0000002C, "PORT_DRIVER_INTERNAL", "端口驱动程序内部错误", "端口驱动程序发生内部错误。"},
    {0x0000002D, "SCSI_DISK_DRIVER_INTERNAL", "SCSI磁盘驱动程序内部错误", "SCSI驱动程序发生内部错误。"},
    {0x0000002E, "DATA_BUS_ERROR", "数据总线错误", "硬件检测到数据总线错误。可能原因：内存条损坏、扩展卡接触不良、电源不稳定、硬盘数据线问题。建议更换内存条、检查硬盘数据线、检测电源。"},
    {0x0000002F, "INSTRUCTION_BUS_ERROR", "指令总线错误", "CPU无法读取指令。可能原因：CPU故障、内存问题、主板故障。"},
    {0x00000030, "SET_OF_INVALID_CONTEXT", "无效的上下文集合", "处理器上下文设置错误。"},
    {0x00000031, "PHASE0_INITIALIZATION_FAILED", "阶段0初始化失败", "系统引导阶段0初始化失败。可能是硬件或驱动问题。"},
    {0x00000032, "PHASE1_INITIALIZATION_FAILED", "阶段1初始化失败", "系统引导阶段1初始化失败。可能是硬件或驱动问题。"},
    {0x00000033, "UNEXPECTED_INITIALIZATION_CALL", "意外的初始化调用", "系统初始化调用顺序异常。"},
    {0x00000034, "CACHE_MANAGER", "缓存管理器错误", "Windows缓存管理器发生错误。"},
    {0x00000035, "NO_MORE_IRP_STACK_LOCATIONS", "IRP堆栈位置不足", "I/O请求包堆栈空间不足。可能是驱动程序问题。"},
    {0x00000036, "DEVICE_REFERENCE_COUNT_NOT_ZERO", "设备引用计数非零", "设备关闭时仍有引用计数。"},
    {0x00000037, "FLOPPY_INTERNAL_ERROR", "软盘内部错误", "软盘控制器发生内部错误。现在已很少见。"},
    {0x00000038, "SERIAL_DRIVER_INTERNAL", "串行驱动程序内部错误", "串口驱动程序发生内部错误。"},
    {0x00000039, "SYSTEM_EXIT_OWNED_MUTEX", "系统退出时占用互斥锁", "进程退出时仍占用互斥锁。"},
    {0x0000003A, "SYSTEM_UNWIND_PREVIOUS_USER", "系统回溯前一个用户", "系统回溯用户模式时发生错误。"},
    {0x0000003B, "SYSTEM_SERVICE_EXCEPTION", "系统服务异常", "系统服务发生异常。可能由驱动程序或服务问题引起。"},
    {0x0000003C, "INTERRUPT_UNWIND_ATTEMPTED", "尝试中断展开", "中断处理时发生堆栈展开错误。"},
    {0x0000003D, "INTERRUPT_EXCEPTION_NOT_HANDLED", "中断异常未处理", "硬件中断未得到正确处理。"},
    {0x0000003E, "MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED", "不支持多处理器配置", "系统配置了不支持的多处理器模式。"},
    {0x0000003F, "NO_MORE_SYSTEM_PTES", "系统页表项不足", "系统页表项资源耗尽。"},
    {0x00000040, "TARGET_MDL_TOO_SMALL", "目标MDL过小", "内存描述符列表太小。"},
    {0x00000041, "MUST_SUCCEED_POOL_EMPTY", "必需成功的内存池为空", "系统内存池耗尽。"},
    {0x00000042, "ATDISK_DRIVER_INTERNAL", "AT磁盘驱动程序内部错误", "IDE/ATA磁盘驱动程序发生内部错误。"},
    {0x00000043, "NO_SUCH_PARTITION", "分区不存在", "访问的分区不存在。"},
    {0x00000044, "MULTIPLE_IRP_COMPLETE_REQUESTS", "多个IRP完成请求", "驱动程序试图多次完成同一I/O请求。"},
    {0x00000045, "INSUFFICIENT_SYSTEM_MAP_REGS", "系统映射寄存器不足", "系统映射寄存器不足。"},
    {0x00000046, "DEREF_UNKNOWN_LOGON_SESSION", "解引用未知登录会话", "尝试解引用不存在的登录会话。"},
    {0x00000047, "REF_UNKNOWN_LOGON_SESSION", "引用未知登录会话", "尝试引用不存在的登录会话。"},
    {0x00000048, "CANCEL_STATE_IN_COMPLETED_IRP", "已完成IRP中的取消状态", "已完成的I/O请求中仍有取消状态。"},
    {0x00000049, "PAGE_FAULT_WITH_INTERRUPTS_OFF", "中断禁用时的页面错误", "禁用中断时发生页面错误。"},
    {0x0000004A, "IRQL_GT_ZERO_AT_SYSTEM_SERVICE", "系统服务时IRQL大于零", "系统服务在IRQL过高时执行。"},
    {0x0000004B, "STREAMS_INTERNAL_ERROR", "流内部错误", "Windows流系统发生内部错误。"},
    {0x0000004C, "FATAL_UNHANDLED_HARD_ERROR", "致命未处理硬件错误", "系统遇到未处理的硬件错误。"},
    {0x0000004D, "NO_PAGES_AVAILABLE", "无可用页面", "系统无可用内存页面。"},
    {0x0000004E, "PFN_LIST_CORRUPT", "页帧号列表损坏", "内存管理数据结构损坏。建议运行内存诊断工具。"},
    {0x0000004F, "NDIS_INTERNAL_ERROR", "NDIS内部错误", "网络驱动程序接口规范发生错误。"},
    {0x00000050, "PAGE_FAULT_IN_NONPAGED_AREA", "非分页区页面错误", "访问非分页内存区域时发生错误。可能是驱动程序问题。"},
    {0x00000051, "REGISTRY_ERROR", "注册表错误", "Windows注册表损坏或读取错误。建议运行sfc /scannow修复。"},
    {0x00000052, "MAILSLOT_FILE_SYSTEM", "邮件槽文件系统错误", "邮件槽文件系统发生错误。"},
    {0x00000053, "NO_BOOT_DEVICE", "无启动设备", "系统找不到启动设备。检查BIOS启动顺序和硬盘连接。"},
    {0x00000054, "LM_SERVER_INTERNAL_ERROR", "LM服务器内部错误", "LanManager服务器发生内部错误。"},
    {0x00000055, "DATA_COHERENCY_EXCEPTION", "数据一致性异常", "CPU缓存数据一致性检查失败。可能是硬件问题。"},
    {0x00000056, "INSTRUCTION_COHERENCY_EXCEPTION", "指令一致性异常", "CPU缓存指令一致性检查失败。可能是硬件问题。"},
    {0x00000057, "XNS_INTERNAL_ERROR", "XNS内部错误", "XNS协议发生内部错误。"},
    {0x00000058, "FTDISK_INTERNAL_ERROR", "FTDISK内部错误", "FTDISK磁盘管理发生错误。"},
    {0x00000059, "PINBALL_FILE_SYSTEM", "弹珠文件系统错误", "Pinball文件系统发生错误。"},
    {0x0000005A, "CRITICAL_SERVICE_FAILED", "关键服务失败", "系统关键服务启动失败。建议检查系统日志了解详情。"},
    {0x0000005B, "SET_ENV_VAR_FAILED", "设置环境变量失败", "无法设置环境变量。"},
    {0x0000005C, "HAL_INITIALIZATION_FAILED", "HAL初始化失败", "硬件抽象层初始化失败。可能是BIOS/固件问题。"},
    {0x0000005D, "UNSUPPORTED_PROCESSOR", "不支持的处理器", "CPU不被当前Windows版本支持。"},
    {0x0000005E, "OBJECT_INITIALIZATION_FAILED", "对象初始化失败", "系统对象初始化失败。"},
    {0x0000005F, "SECURITY_INITIALIZATION_FAILED", "安全初始化失败", "Windows安全子系统初始化失败。"},
    {0x00000060, "PROCESS_INITIALIZATION_FAILED", "进程初始化失败", "系统进程初始化失败。"},
    {0x00000061, "HAL1_INITIALIZATION_FAILED", "HAL1初始化失败", "硬件抽象层初始化失败。"},
    {0x00000062, "OBJECT1_INITIALIZATION_FAILED", "对象1初始化失败", "系统对象初始化失败。"},
    {0x00000063, "SECURITY1_INITIALIZATION_FAILED", "安全1初始化失败", "Windows安全子系统初始化失败。"},
    {0x00000064, "SYMBOLIC_INITIALIZATION_FAILED", "符号初始化失败", "系统符号初始化失败。"},
    {0x00000065, "MEMORY1_INITIALIZATION_FAILED", "内存1初始化失败", "系统内存管理初始化失败。"},
    {0x00000066, "CACHE_INITIALIZATION_FAILED", "缓存初始化失败", "系统缓存初始化失败。"},
    {0x00000067, "CONFIG_INITIALIZATION_FAILED", "配置初始化失败", "系统配置初始化失败。"},
    {0x00000068, "FILE_INITIALIZATION_FAILED", "文件初始化失败", "系统文件初始化失败。"},
    {0x00000069, "IO1_INITIALIZATION_FAILED", "IO1初始化失败", "系统I/O初始化失败。"},
    {0x0000006A, "LPC_INITIALIZATION_FAILED", "LPC初始化失败", "本地过程调用初始化失败。"},
    {0x0000006B, "PROCESS1_INITIALIZATION_FAILED", "进程1初始化失败", "系统进程初始化失败。"},
    {0x0000006C, "REFMON_INITIALIZATION_FAILED", "引用监视器初始化失败", "引用监视器初始化失败。"},
    {0x0000006D, "SESSION1_INITIALIZATION_FAILED", "会话1初始化失败", "Windows会话初始化失败。"},
    {0x0000006E, "SESSION2_INITIALIZATION_FAILED", "会话2初始化失败", "Windows会话初始化失败。"},
    {0x0000006F, "SESSION3_INITIALIZATION_FAILED", "会话3初始化失败", "Windows会话初始化失败。"},
    {0x00000070, "SESSION4_INITIALIZATION_FAILED", "会话4初始化失败", "Windows会话初始化失败。"},
    {0x00000071, "SESSION5_INITIALIZATION_FAILED", "会话5初始化失败", "Windows会话初始化失败。"},
    {0x00000072, "ASSIGN_DRIVE_LETTERS_FAILED", "分配驱动器号失败", "系统无法分配驱动器号。"},
    {0x00000073, "CONFIG_LIST_FAILED", "配置列表失败", "系统配置列表加载失败。"},
    {0x00000074, "BAD_SYSTEM_CONFIG_INFO", "系统配置信息错误", "系统配置信息损坏。"},
    {0x00000075, "CANNOT_WRITE_CONFIGURATION", "无法写入配置", "系统无法写入配置。"},
    {0x00000076, "PROCESS_HAS_LOCKED_PAGES", "进程有锁定页面", "内核模式驱动程序在进程退出时未能释放已锁定的页面内存。建议更新驱动程序。"},
    {0x00000077, "KERNEL_STACK_INPAGE_ERROR", "内核堆栈页面错误", "从页面文件读取内核堆栈失败。建议检查磁盘健康状态和内存。"},
    {0x00000078, "PHASE0_EXCEPTION", "阶段0异常", "系统引导阶段0发生异常。"},
    {0x00000079, "MISMATCHED_HAL", "HAL不匹配", "硬件抽象层与内核不匹配。"},
    {0x0000007A, "KERNEL_DATA_INPAGE_ERROR", "内核数据页面错误", "从页面文件读取内核数据失败。建议检查磁盘健康状态。"},
    {0x0000007B, "INACCESSIBLE_BOOT_DEVICE", "无法访问启动设备", "系统无法访问启动设备。建议检查硬盘连接和BIOS设置。"},
    {0x0000007C, "BUGCODE_NDIS_DRIVER", "NDIS驱动程序错误码", "网络驱动程序发生错误。建议更新网卡驱动。"},
    {0x0000007D, "INSTALL_MORE_MEMORY", "安装更多内存", "系统检测到内存不足。"},
    {0x0000007E, "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED", "系统线程异常未处理", "系统线程发生未处理的异常。建议更新驱动程序。"},
    {0x0000007F, "UNEXPECTED_KERNEL_MODE_TRAP", "意外的内核模式陷阱", "内核遇到意外陷阱。建议检查硬件和驱动。"},
    {0x00000080, "NMI_HARDWARE_FAILURE", "NMI硬件故障", "非屏蔽中断检测到硬件故障。可能是内存、CPU或主板问题。"},
    {0x00000081, "SPIN_LOCK_INIT_FAILURE", "自旋锁初始化失败", "系统自旋锁初始化失败。"},
    {0x00000082, "DFS_FILE_SYSTEM", "DFS文件系统错误", "分布式文件系统发生错误。"},
    {0x00000083, "OFS_FILE_SYSTEM", "OFS文件系统错误", "OS/2文件系统发生错误。"},
    {0x00000084, "RECOM_DRIVER", "RECOM驱动程序错误", "推荐驱动程序发生错误。"},
    {0x00000085, "SETUP_FAILURE", "安装失败", "Windows安装过程失败。"},
    {0x0000008B, "MBR_CHECKSUM_MISMATCH", "MBR校验和不匹配", "主引导记录(MBR)校验和失败。可能是病毒或硬盘问题。"},
    {0x0000008E, "KERNEL_MODE_EXCEPTION_NOT_HANDLED", "内核模式异常未处理", "内核模式发生未处理的异常。建议更新驱动程序。"},
    {0x0000008F, "PP0_INITIALIZATION_FAILED", "PP0初始化失败", "电源管理初始化失败。"},
    {0x00000090, "PP1_INITIALIZATION_FAILED", "PP1初始化失败", "电源管理初始化失败。"},
    {0x00000091, "WIN32K_INIT_OR_RIT_FAILURE", "WIN32K初始化或RIT失败", "Windows子系统和图形初始化失败。"},
    {0x00000092, "UP_DRIVER_ON_MP_SYSTEM", "多处理器系统上的单处理器驱动", "单处理器驱动程序在多处理器系统上运行。"},
    {0x00000093, "INVALID_KERNEL_HANDLE", "无效的内核句柄", "内核句柄无效。"},
    {0x00000094, "KERNEL_STACK_LOCKED_AT_EXIT", "退出时内核堆栈被锁定", "线程退出时内核堆栈被锁定。"},
    {0x00000095, "PNP_INTERNAL_ERROR", "即插即用内部错误", "即插即用管理器发生内部错误。"},
    {0x00000096, "INVALID_WORK_QUEUE_ITEM", "无效的工作队列项", "系统工作队列项无效。"},
    {0x00000097, "BOUND_IMAGE_UNSUPPORTED", "不支持绑定映像", "不支持绑定的驱动程序映像。"},
    {0x00000098, "END_OF_NT_EVALUATION_PERIOD", "NT评估期结束", "Windows评估期已结束。需要激活系统。"},
    {0x00000099, "INVALID_REGION_OR_SEGMENT", "无效的区域或段", "内存区域或段无效。"},
    {0x0000009A, "SYSTEM_LICENSE_VIOLATION", "系统许可证违规", "Windows许可证违规。"},
    {0x0000009B, "UDFS_FILE_SYSTEM", "UDFS文件系统错误", "UDF文件系统发生错误。"},
    {0x0000009C, "MACHINE_CHECK_EXCEPTION", "机器检查异常", "CPU检测到硬件错误。可能是CPU、内存或主板问题。"},
    {0x0000009E, "USER_MODE_HEALTH_MONITOR", "用户模式健康监视器", "用户模式进程健康监视器检测到问题。"},
    {0x0000009F, "DRIVER_POWER_STATE_FAILURE", "驱动程序电源状态失败", "驱动程序电源状态转换失败。建议更新驱动。"},
    {0x000000A0, "INTERNAL_POWER_ERROR", "内部电源错误", "系统电源管理发生错误。"},
    {0x000000A1, "PCI_BUS_DRIVER_INTERNAL", "PCI总线驱动程序内部错误", "PCI总线驱动程序发生错误。"},
    {0x000000A2, "MEMORY_IMAGE_CORRUPT", "内存映像损坏", "系统内存映像损坏。"},
    {0x000000A3, "ACPI_DRIVER_INTERNAL", "ACPI驱动程序内部错误", "ACPI驱动程序发生错误。建议更新BIOS。"},
    {0x000000A4, "CNSS_FILE_SYSTEM_FILTER", "CNSS文件系统过滤器错误", "CNSS文件系统过滤器发生错误。"},
    {0x000000A5, "ACPI_BIOS_ERROR", "ACPI BIOS错误", "ACPI BIOS配置错误。建议更新BIOS。"},
    {0x000000A7, "BAD_EXHANDLE", "扩展句柄错误", "扩展句柄无效。"},
    {0x000000AB, "SESSION_HAS_VALID_POOL_ON_EXIT", "退出时会话有有效内存池", "会话退出时内存池未释放。"},
    {0x000000AC, "HAL_MEMORY_ALLOCATION", "HAL内存分配错误", "硬件抽象层内存分配失败。"},
    {0x000000AD, "VIDEO_DRIVER_DEBUG_REPORT_REQUEST", "视频驱动程序调试报告请求", "显卡驱动请求调试报告。"},
    {0x000000B4, "VIDEO_DRIVER_INIT_FAILURE", "视频驱动初始化失败", "显卡驱动初始化失败。建议更新显卡驱动。"},
    {0x000000B8, "ATTEMPTED_SWITCH_FROM_DPC", "尝试从DPC切换", "从延迟过程调用(DPC)进行无效切换。"},
    {0x000000B9, "CHIPSET_DETECTED_ERROR", "芯片组检测到错误", "主板芯片组检测到错误。"},
    {0x000000BA, "SESSION_HAS_VALID_VIEWS_ON_EXIT", "退出时会话有有效视图", "会话退出时视图未释放。"},
    {0x000000BB, "NETWORK_BOOT_INITIALIZATION_FAILED", "网络引导初始化失败", "网络引导初始化失败。"},
    {0x000000BC, "NETWORK_BOOT_DUPLICATE_ADDRESS", "网络引导地址重复", "网络引导时IP地址冲突。"},
    {0x000000BE, "ATTEMPTED_WRITE_TO_READONLY_MEMORY", "尝试写入只读内存", "程序试图写入只读内存区域。"},
    {0x000000BF, "MUTEX_ALREADY_OWNED", "互斥锁已被占用", "互斥锁已被其他线程占用。"},
    {0x000000C1, "SPECIAL_POOL_DETECTED_MEMORY_CORRUPTION", "特殊内存池检测到内存损坏", "驱动程序验证器检测到内存损坏。"},
    {0x000000C2, "BAD_POOL_CALLER", "内存池调用者错误", "驱动程序调用内存池API时参数错误。"},
    {0x000000C4, "DRIVER_VERIFIER_DETECTED_VIOLATION", "驱动程序验证器检测到违规", "驱动程序验证器检测到驱动程序违规。建议禁用驱动验证器或更新驱动。"},
    {0x000000C5, "DRIVER_CORRUPTED_EXPOOL", "驱动程序损坏执行内存池", "驱动程序损坏了系统内存池。"},
    {0x000000C6, "DRIVER_CAUGHT_MODIFYING_FREED_POOL", "驱动程序修改已释放的内存池", "驱动程序访问已释放的内存。"},
    {0x000000C7, "TIMER_OR_DPC_INVALID", "定时器或DPC无效", "定时器或延迟过程调用无效。"},
    {0x000000C8, "IRQL_UNEXPECTED_VALUE", "IRQL意外值", "中断请求级别异常。"},
    {0x000000C9, "DRIVER_VERIFIER_IOMANAGER_VIOLATION", "驱动程序验证器IO管理器违规", "驱动程序验证器检测到IO管理器违规。"},
    {0x000000CA, "PNP_DETECTED_FATAL_ERROR", "即插即用检测到致命错误", "即插即用检测到致命错误。建议检查设备驱动。"},
    {0x000000CB, "DRIVER_LEFT_LOCKED_PAGES_IN_PROCESS", "驱动程序在进程中遗留锁定页面", "驱动程序退出时未释放锁定的内存页面。"},
    {0x000000CC, "PAGE_FAULT_IN_FREED_SPECIAL_POOL", "已释放特殊内存池中的页面错误", "访问已释放的特殊内存池。"},
    {0x000000CD, "PAGE_FAULT_BEYOND_END_OF_ALLOCATION", "超出分配范围的页面错误", "访问超出分配范围的内存。"},
    {0x000000CE, "DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS", "驱动程序卸载时未取消挂起操作", "驱动程序卸载时仍有未完成的操作。"},
    {0x000000CF, "TERMINAL_SERVER_DRIVER_MADE_INCORRECT_MEMORY_REFERENCE", "终端服务器驱动程序内存引用错误", "终端服务驱动内存引用错误。"},
    {0x000000D0, "DRIVER_CORRUPTED_MMPOOL", "驱动程序损坏了MM内存池", "驱动程序损坏系统内存池。"},
    {0x000000D1, "DRIVER_IRQL_NOT_LESS_OR_EQUAL", "驱动程序IRQL不低于或等于", "驱动程序在IRQL过高时访问低IRQL内存。建议更新驱动。"},
    {0x000000D2, "BUGCODE_ID_DRIVER", "BUGCODE_ID驱动程序错误", "ID驱动程序发生错误。"},
    {0x000000D3, "DRIVER_PORTION_MUST_BE_NONPAGED", "驱动程序部分必须非分页", "驱动程序必须位于非分页内存中。"},
    {0x000000D4, "SYSTEM_SCAN_AT_RAISED_IRQL_CAUGHT_IMPROPER_DRIVER_UNLOAD", "提升IRQL时系统扫描捕获不当驱动卸载", "提升IRQL时系统扫描捕获不当驱动卸载。"},
    {0x000000D5, "DRIVER_PAGE_FAULT_IN_FREED_SPECIAL_POOL", "驱动程序在已释放特殊内存池中页面错误", "驱动程序访问已释放的特殊内存池。"},
    {0x000000D6, "DRIVER_PAGE_FAULT_BEYOND_END_OF_ALLOCATION", "驱动程序超出分配范围的页面错误", "驱动程序访问超出分配范围的内存。"},
    {0x000000D7, "DRIVER_UNMAPPING_INVALID_VIEW", "驱动程序取消映射无效视图", "驱动程序取消映射无效视图。"},
    {0x000000D8, "DRIVER_USED_EXCESSIVE_PTES", "驱动程序使用了过多页表项", "驱动程序使用了过多系统页表项。"},
    {0x000000D9, "LOCKED_PAGES_TRACKER_CORRUPTION", "锁定页面跟踪器损坏", "锁定页面跟踪器数据损坏。"},
    {0x000000DA, "SYSTEM_PTE_MISUSE", "系统页表项误用", "系统页表项使用不当。"},
    {0x000000DB, "DRIVER_CORRUPTED_SYSPTES", "驱动程序损坏了系统页表项", "驱动程序损坏系统页表项。"},
    {0x000000DC, "DRIVER_INVALID_STACK_ACCESS", "驱动程序无效堆栈访问", "驱动程序访问无效堆栈。"},
    {0x000000DE, "POOL_CORRUPTION_IN_FILE_AREA", "文件区域内存池损坏", "文件区域内存池损坏。"},
    {0x000000DF, "IMPERSONATING_WORKER_THREAD", "模拟工作线程", "工作线程模拟错误。"},
    {0x000000E0, "ACPI_BIOS_FATAL_ERROR", "ACPI BIOS致命错误", "ACPI BIOS发生致命错误。建议更新BIOS。"},
    {0x000000E1, "WORKER_THREAD_RETURNED_AT_BAD_IRQL", "工作线程在错误IRQL返回", "工作线程在错误的IRQL返回。"},
    {0x000000E2, "MANUALLY_INITIATED_CRASH", "手动启动的崩溃", "用户手动触发了系统崩溃(蓝屏)。"},
    {0x000000E3, "RESOURCE_NOT_OWNED", "资源未被拥有", "线程尝试释放不拥有的资源。"},
    {0x000000E4, "WORKER_INVALID", "工作线程无效", "工作线程无效。"},
    {0x000000E6, "DRIVER_VERIFIER_DMA_VIOLATION", "驱动程序验证器DMA违规", "驱动程序验证器检测到DMA违规。"},
    {0x000000E7, "INVALID_FLOATING_POINT_STATE", "无效的浮点状态", "浮点单元状态无效。"},
    {0x000000E8, "INVALID_CANCEL_OF_FILE_OPEN", "无效的文件打开取消", "取消文件打开操作失败。"},
    {0x000000E9, "ACTIVE_EX_WORKER_THREAD_TERMINATION", "活动EX工作线程终止", "活动工作线程异常终止。"},
    {0x000000EA, "THREAD_STUCK_IN_DEVICE_DRIVER", "线程卡在设备驱动程序中", "设备驱动程序中的线程卡住。建议更新显卡驱动或磁盘驱动。"},
    {0x000000EB, "DIRTY_MAPPED_PAGES_CONGESTION", "脏映射页面拥塞", "脏映射页面过多。"},
    {0x000000EC, "SESSION_HAS_VALID_SPECIAL_POOL_ON_EXIT", "退出时会话有有效特殊内存池", "会话退出时特殊内存池未释放。"},
    {0x000000ED, "UNMOUNTABLE_BOOT_VOLUME", "无法挂载的启动卷", "无法挂载启动分区。建议检查硬盘连接和文件系统。"},
    {0x000000EF, "CRITICAL_PROCESS_DIED", "关键进程终止", "关键系统进程终止。"},
    {0x000000F1, "SCSI_VERIFIER_DETECTED_VIOLATION", "SCSI验证器检测到违规", "SCSI验证器检测到驱动违规。"},
    {0x000000F3, "DISORDERLY_SHUTDOWN", "无序关机", "系统无序关机。可能是电源问题或软件冲突。"},
    {0x000000F4, "CRITICAL_OBJECT_TERMINATION", "关键对象终止", "关键系统对象被终止。"},
    {0x000000F5, "FLTMGR_FILE_SYSTEM", "文件系统过滤管理器错误", "文件系统过滤管理器发生错误。"},
    {0x000000F6, "PCI_VERIFIER_DETECTED_VIOLATION", "PCI验证器检测到违规", "PCI验证器检测到违规。"},
    {0x000000F7, "DRIVER_OVERRAN_STACK_BUFFER", "驱动程序堆栈缓冲区溢出", "驱动程序堆栈缓冲区溢出。"},
    {0x000000F8, "RAMDISK_BOOT_INITIALIZATION_FAILED", "RAMDISK引导初始化失败", "RAMDISK引导初始化失败。"},
    {0x000000F9, "DRIVER_RETURNED_STATUS_REPARSE_FOR_VOLUME_OPEN", "驱动程序返回卷打开的重解析状态", "驱动程序返回重解析状态。"},
    {0x000000FA, "HTTP_DRIVER_CORRUPTED", "HTTP驱动程序损坏", "HTTP驱动程序损坏。"},
    {0x000000FC, "ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY", "尝试执行不可执行内存", "尝试执行不可执行的内存区域（可能是内存损坏或恶意软件）。"},
    {0x000000FD, "DIRTY_NOWRITE_PAGES_CONGESTION", "脏不可写页面拥塞", "脏不可写页面拥塞。"},
    {0x000000FE, "BUGCODE_USB_DRIVER", "USB驱动程序错误码", "USB驱动程序发生错误。建议更新USB驱动。"},
    {0x000000FF, "RESERVE_QUEUE_OVERFLOW", "保留队列溢出", "保留队列溢出。"},
    {0x00000100, "LOADER_BLOCK_MISMATCH", "加载器块不匹配", "系统加载器块不匹配。"},
    {0x00000101, "CLOCK_WATCHDOG_TIMEOUT", "时钟看门狗超时", "CPU核心卡住或时钟问题。可能是硬件问题。"},
    {0x00000103, "MUP_FILE_SYSTEM", "MUP文件系统错误", "MUP(多UNC提供程序)文件系统错误。"},
    {0x00000104, "AGP_INVALID_ACCESS", "AGP无效访问", "AGP图形端口访问无效。"},
    {0x00000105, "AGP_GART_CORRUPTION", "AGP GART损坏", "AGP图形地址转换表损坏。"},
    {0x00000106, "AGP_ILLEGALLY_REPROGRAMMED", "AGP被非法重新编程", "AGP被非法重新编程。"},
    {0x00000108, "THIRD_PARTY_FILE_SYSTEM_FAILURE", "第三方文件系统失败", "第三方文件系统发生错误。"},
    {0x00000109, "CRITICAL_STRUCTURE_CORRUPTION", "关键结构损坏", "系统关键数据结构损坏。"},
    {0x0000010A, "APP_TAGGING_INITIALIZATION_FAILED", "应用程序标记初始化失败", "应用程序标记初始化失败。"},
    {0x0000010C, "FSRTL_EXTRA_CREATE_PARAMETER_VIOLATION", "FSRTL额外创建参数违规", "文件系统运行时库参数错误。"},
    {0x0000010D, "WDF_VIOLATION", "Windows驱动程序框架违规", "Windows驱动程序框架检测到违规。"},
    {0x0000010E, "VIDEO_MEMORY_MANAGEMENT_INTERNAL", "视频内存管理内部错误", "显卡内存管理发生内部错误。"},
    {0x0000010F, "RESOURCE_MANAGER_EXCEPTION_NOT_HANDLED", "资源管理器异常未处理", "资源管理器异常未处理。"},
    {0x00000111, "RECURSIVE_NMI", "递归NMI", "发生递归NMI中断。可能是硬件问题。"},
    {0x00000112, "MSRPC_STATE_VIOLATION", "MSRPC状态违规", "Microsoft RPC状态违规。"},
    {0x00000113, "VIDEO_DXGKRNL_FATAL_ERROR", "视频DXGKRNL致命错误", "显卡内核模式驱动发生致命错误。建议更新显卡驱动。"},
    {0x00000114, "VIDEO_SHADOW_DRIVER_FATAL_ERROR", "视频影子驱动程序致命错误", "显卡影子驱动发生致命错误。"},
    {0x00000115, "AGP_INTERNAL", "AGP内部错误", "AGP内部错误。"},
    {0x00000116, "VIDEO_TDR_ERROR", "视频超时检测与恢复错误", "显卡超时检测和恢复失败。建议更新显卡驱动。"},
    {0x00000117, "VIDEO_TDR_TIMEOUT_DETECTED", "检测到视频超时", "显卡操作超时。建议更新显卡驱动或降低画质设置。"},
    {0x00000119, "VIDEO_SCHEDULER_INTERNAL_ERROR", "视频调度程序内部错误", "显卡调度程序内部错误。"},
    {0x0000011A, "EM_INITIALIZATION_FAILURE", "EM初始化失败", "EM(执行管理器)初始化失败。"},
    {0x0000011B, "DRIVER_RETURNED_HOLDING_CANCEL_LOCK", "驱动程序返回时持有取消锁", "驱动程序返回时持有取消锁。"},
    {0x0000011C, "ATTEMPTED_WRITE_TO_CM_PROTECTED_STORAGE", "尝试写入CM保护存储", "尝试写入受保护的配置管理器存储。"},
    {0x0000011D, "EVENT_TRACING_FATAL_ERROR", "事件跟踪致命错误", "Windows事件跟踪发生致命错误。"},
    {0x00000121, "DRIVER_VIOLATION", "驱动程序违规", "驱动程序发生违规。"},
    {0x00000122, "WHEA_INTERNAL_ERROR", "Windows硬件错误体系结构内部错误", "Windows硬件错误架构内部错误。"},
    {0x00000124, "WHEA_UNCORRECTABLE_ERROR", "Windows硬件错误体系结构不可纠正错误", "硬件检测到不可纠正的错误。可能是CPU、内存或主板问题。"},
    {0x00000127, "PAGE_NOT_ZERO", "页面非零", "内存页面应该为零但不是。"},
    {0x0000012B, "FAULTY_HARDWARE_CORRUPTED_PAGE", "故障硬件损坏页面", "硬件故障导致内存页面损坏。"},
    {0x0000012C, "EXFAT_FILE_SYSTEM", "EXFAT文件系统错误", "exFAT文件系统发生错误。"},
    {0x00000133, "DPC_WATCHDOG_VIOLATION", "DPC看门狗违规", "延迟过程调用(DPC)执行时间过长。建议更新驱动。"},
    {0x00000138, "GPIO_CONTROLLER_DRIVER_ERROR", "GPIO控制器驱动程序错误", "GPIO控制器驱动发生错误。"},
    {0x00000139, "KERNEL_SECURITY_CHECK_FAILURE", "内核安全检查失败", "内核安全检查失败。可能是内存损坏。"},
    {0x00000144, "BUGCODE_USB3_DRIVER", "USB3驱动程序错误码", "USB3.0驱动程序发生错误。建议更新USB驱动。"},
    {0x0000014B, "SOC_SUBSYSTEM_FAILURE", "片上系统子系统失败", "SOC子系统发生失败。"},
    {0x00000154, "UNEXPECTED_STORE_EXCEPTION", "意外存储异常", "存储控制器发生意外异常。"},
    {0x00000159, "HAL_ILLEGAL_IOMMU_PAGE_FAULT", "HAL非法IOMMU页面错误", "HAL IOMMU页面错误。"},
    {0x0000015A, "SDBUS_INTERNAL_ERROR", "SD总线内部错误", "SD卡总线发生内部错误。"},
    {0x0000015B, "WORKER_THREAD_RETURNED_WITH_SYSTEM_PAGE_PRIORITY_ACTIVE", "工作线程返回时系统页面优先级活动", "工作线程返回时系统页面优先级仍活动。"},
    {0x00000161, "LIVE_SYSTEM_DUMP", "活动系统转储", "系统正在执行活动转储。"},
    {0x00000184, "HYPERVISOR_ERROR", "虚拟机监控程序错误", "虚拟机监控程序发生错误。"},
    {0x00000189, "BAD_OBJECT_HEADER", "对象头错误", "对象头损坏。"},
    {0x0000018A, "SECURE_KERNEL_ERROR", "安全内核错误", "安全内核发生错误。"},
    {0x0000019C, "DIRECT_WRITE_STALL_BUGCHECK", "直接写入停滞错误检查", "直接写入操作停滞。"},
    {0x000001C8, "MANUALLY_INITIATED_POWER_BUTTON_HOLD", "手动启动的电源按钮长按", "用户长按电源按钮导致的崩溃。"},
    {0x000001CF, "HARDWARE_WATCHDOG_TIMEOUT", "硬件看门狗超时", "硬件看门狗超时。可能是硬件问题。"},
    {0x1000007E, "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED_M", "系统线程异常未处理_M", "系统线程发生未处理的异常。"},
    {0x1000007F, "UNEXPECTED_KERNEL_MODE_TRAP_M", "意外的内核模式陷阱_M", "内核遇到意外陷阱。"},
    {0x1000008E, "KERNEL_MODE_EXCEPTION_NOT_HANDLED_M", "内核模式异常未处理_M", "内核模式发生未处理的异常。"},
    {0x100000EA, "THREAD_STUCK_IN_DEVICE_DRIVER_M", "线程卡在设备驱动程序中_M", "设备驱动程序中的线程卡住。"},
    {0xC0000218, "STATUS_CANNOT_LOAD_REGISTRY_FILE", "无法加载注册表文件", "系统无法加载注册表文件。建议运行系统修复。"},
    {0xC000021A, "STATUS_SYSTEM_PROCESS_TERMINATED", "系统进程已终止", "关键系统进程已终止。"},
    {0xC0000221, "STATUS_IMAGE_CHECKSUM_MISMATCH", "映像校验和不匹配", "系统文件校验和不匹配。建议运行SFC扫描。"},
    {0xDEADDEAD, "MANUALLY_INITIATED_CRASH1", "手动启动的崩溃1", "用户手动触发了系统崩溃(蓝屏)。"},
    {0, NULL, NULL, NULL}
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

const char* gnwinfo_bsod_get_code_diagnosis(UINT32 code)
{
    for (int i = 0; g_bsod_codes[i].name != NULL; i++) {
        if (g_bsod_codes[i].code == code && g_bsod_codes[i].diagnosis != NULL) {
            return g_bsod_codes[i].diagnosis;
        }
    }
    return "";
}

void gnwinfo_bsod_init(void)
{
    InitializeCriticalSection(&g_bsod_lock);

    int dump_enabled = gnwinfo_bsod_get_dump_enabled();

    if (!dump_enabled) {
        if (IsRunAsAdmin()) {
            gnwinfo_bsod_set_dump_type(3);
            gnwinfo_bsod_set_dump_enabled(1);
        }
        else {
            SHELLEXECUTEINFOA sei = { 0 };
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
        }
        else {
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
        }
        else {
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
            }
            else {
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
                            }
                            else if (debug_file) {
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
