#include <windows.h>
#include <stdio.h>

int main()
{
    HKEY hKey;
    LONG result;
    WCHAR path[MAX_PATH];
    DWORD size;

    printf("正在测试注册表操作...\n");

    // 获取当前可执行文件路径
    GetModuleFileNameW(NULL, path, MAX_PATH);
    printf("当前路径: %ls\n", path);

    // 打开/创建注册表键
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &hKey, NULL);

    if (result != ERROR_SUCCESS)
    {
        printf("RegCreateKeyExW 失败，错误代码: %ld\n", result);
        return 1;
    }
    printf("RegCreateKeyExW 成功\n");

    // 设置注册表值
    size = (DWORD)(wcslen(path) + 1) * sizeof(WCHAR);
    result = RegSetValueExW(hKey, L"gnwinfo_test", 0, REG_SZ, (const BYTE*)path, size);

    if (result != ERROR_SUCCESS)
    {
        printf("RegSetValueExW 失败，错误代码: %ld\n", result);
        RegCloseKey(hKey);
        return 1;
    }
    printf("RegSetValueExW 成功，已添加 gnwinfo_test 项\n");

    RegCloseKey(hKey);
    printf("\n测试完成！请用以下命令检查:\n");
    printf("Get-ItemProperty \"HKCU:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run\" -Name gnwinfo_test\n");
    getchar();
    return 0;
}
