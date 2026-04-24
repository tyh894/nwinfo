

#include "gnwinfo.h"
#include "gettext.h"

#include <pathcch.h>
#include <windowsx.h>
#include <dbt.h>
#include <shellapi.h>
#include <taskschd.h>
#include <oleauto.h>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#define AUTOSTART_TASK_NAME L"gnwinfo_autostart"

LPCSTR NWL_Ucs2ToUtf8(LPCWSTR src);
LPCWSTR NWL_Utf8ToUcs2(LPCSTR src);

unsigned int g_init_width = 600;
unsigned int g_init_height = 800;
unsigned int g_init_alpha = 255;
unsigned int g_smart_interval = 600;
GdipFont* g_font = NULL;
int g_font_size = 12;
double g_dpi_factor = 1.0;
nk_bool g_dpi_scaling = 1;
nk_bool g_bginfo = 0;
nk_bool g_debug = 0;
nk_bool g_startup_mode = 0;

nk_bool g_tray_created = 0;
nk_bool g_autostart = 0;
nk_bool g_need_save_hw_config = 0;
nk_bool g_window_was_hidden = nk_false;
nk_bool g_first_window_show = nk_true;
nk_bool g_hw_has_diff = nk_false;
NOTIFYICONDATAW g_nid;

static UINT m_dpi = USER_DEFAULT_SCREEN_DPI;

static BSTR alloc_bstr(LPCWSTR str)
{
	return SysAllocString(str);
}

static void init_variant(VARIANT* var)
{
	VariantInit(var);
}

static void set_variant_empty(VARIANT* var)
{
	VariantClear(var);
	V_VT(var) = VT_EMPTY;
}

nk_bool gnwinfo_get_autostart(void)
{
	nk_bool found = nk_false;
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		NWL_Debug("AUTOSTART", "Get: CoInitializeEx failed: 0x%08X", hr);
		return nk_false;
	}

	ITaskService* pService = NULL;
	ITaskFolder* pRootFolder = NULL;
	IRegisteredTask* pTask = NULL;
	BSTR bstrRoot = NULL;

	hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void**)&pService);
	if (SUCCEEDED(hr))
	{
		VARIANT varEmpty;
		init_variant(&varEmpty);
		
		hr = pService->lpVtbl->Connect(pService, varEmpty, varEmpty, varEmpty, varEmpty);
		if (SUCCEEDED(hr))
		{
			bstrRoot = alloc_bstr(L"\\");
			hr = pService->lpVtbl->GetFolder(pService, bstrRoot, &pRootFolder);
			if (SUCCEEDED(hr))
			{
				BSTR bstrTaskName = alloc_bstr(AUTOSTART_TASK_NAME);
				hr = pRootFolder->lpVtbl->GetTask(pRootFolder, bstrTaskName, &pTask);
				SysFreeString(bstrTaskName);
				
				if (SUCCEEDED(hr))
				{
					found = nk_true;
					pTask->lpVtbl->Release(pTask);
					NWL_Debug("AUTOSTART", "Get: Found task in Task Scheduler");
				}
				pRootFolder->lpVtbl->Release(pRootFolder);
			}
			SysFreeString(bstrRoot);
		}
		pService->lpVtbl->Release(pService);
	}

	CoUninitialize();
	NWL_Debug("AUTOSTART", "Get: Returning %d", found);
	return found;
}

void gnwinfo_set_autostart_internal(nk_bool enable, nk_bool show_message)
{
	WCHAR path[MAX_PATH];

	NWL_Debug("AUTOSTART", "Set: enable=%d", enable);

	if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0)
	{
		NWL_Debug("AUTOSTART", "Set: Failed to get module path");
		return;
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		NWL_Debug("AUTOSTART", "Set: CoInitializeEx failed: 0x%08X", hr);
		if (show_message)
			MessageBoxW(NULL, L"????? COM ???", L"???????????", MB_OK | MB_ICONERROR);
		return;
	}

	ITaskService* pService = NULL;
	ITaskFolder* pRootFolder = NULL;
	IRegisteredTask* pTask = NULL;

	hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void**)&pService);
	if (FAILED(hr))
	{
		NWL_Debug("AUTOSTART", "Set: CoCreateInstance failed: 0x%08X", hr);
		CoUninitialize();
		if (show_message)
			MessageBoxW(NULL, L"?????????????????", L"???????????", MB_OK | MB_ICONERROR);
		return;
	}

	VARIANT varEmpty;
	init_variant(&varEmpty);
	
	hr = pService->lpVtbl->Connect(pService, varEmpty, varEmpty, varEmpty, varEmpty);
	if (FAILED(hr))
	{
		NWL_Debug("AUTOSTART", "Set: Connect failed: 0x%08X", hr);
		pService->lpVtbl->Release(pService);
		CoUninitialize();
		if (show_message)
			MessageBoxW(NULL, L"?????????????????", L"???????????", MB_OK | MB_ICONERROR);
		return;
	}

	BSTR bstrRoot = alloc_bstr(L"\\");
	hr = pService->lpVtbl->GetFolder(pService, bstrRoot, &pRootFolder);
	SysFreeString(bstrRoot);
	
	if (FAILED(hr))
	{
		NWL_Debug("AUTOSTART", "Set: GetFolder failed: 0x%08X", hr);
		pService->lpVtbl->Release(pService);
		CoUninitialize();
		if (show_message)
			MessageBoxW(NULL, L"???????????????", L"???????????", MB_OK | MB_ICONERROR);
		return;
	}

	if (enable)
	{
		ITaskDefinition* pTaskDef = NULL;
		IRegistrationInfo* pRegInfo = NULL;
		IPrincipal* pPrincipal = NULL;
		ITaskSettings* pSettings = NULL;
		IExecAction* pExecAction = NULL;
		IAction* pAction = NULL;
		IActionCollection* pActionColl = NULL;
		ITrigger* pTrigger = NULL;
		ITriggerCollection* pTriggerColl = NULL;

		hr = pService->lpVtbl->NewTask(pService, 0, &pTaskDef);
		if (FAILED(hr))
		{
			NWL_Debug("AUTOSTART", "Set: NewTask failed: 0x%08X", hr);
			goto create_fail;
		}

		hr = pTaskDef->lpVtbl->get_RegistrationInfo(pTaskDef, &pRegInfo);
		if (SUCCEEDED(hr))
		{
			BSTR bstrAuthor = alloc_bstr(L"gnwinfo");
			BSTR bstrDesc = alloc_bstr(L"?????");
			pRegInfo->lpVtbl->put_Author(pRegInfo, bstrAuthor);
			pRegInfo->lpVtbl->put_Description(pRegInfo, bstrDesc);
			SysFreeString(bstrAuthor);
			SysFreeString(bstrDesc);
			pRegInfo->lpVtbl->Release(pRegInfo);
		}

		hr = pTaskDef->lpVtbl->get_Principal(pTaskDef, &pPrincipal);
		if (SUCCEEDED(hr))
		{
			pPrincipal->lpVtbl->put_LogonType(pPrincipal, TASK_LOGON_INTERACTIVE_TOKEN);
			pPrincipal->lpVtbl->put_RunLevel(pPrincipal, TASK_RUNLEVEL_HIGHEST);
			pPrincipal->lpVtbl->Release(pPrincipal);
		}

		hr = pTaskDef->lpVtbl->get_Settings(pTaskDef, &pSettings);
		if (SUCCEEDED(hr))
		{
			BSTR bstrTimeLimit = alloc_bstr(L"PT0S");
			pSettings->lpVtbl->put_StartWhenAvailable(pSettings, VARIANT_TRUE);
			pSettings->lpVtbl->put_DisallowStartIfOnBatteries(pSettings, VARIANT_FALSE);
			pSettings->lpVtbl->put_StopIfGoingOnBatteries(pSettings, VARIANT_FALSE);
			pSettings->lpVtbl->put_AllowHardTerminate(pSettings, VARIANT_TRUE);
			pSettings->lpVtbl->put_ExecutionTimeLimit(pSettings, bstrTimeLimit);
			SysFreeString(bstrTimeLimit);
			pSettings->lpVtbl->Release(pSettings);
		}

		hr = pTaskDef->lpVtbl->get_Triggers(pTaskDef, &pTriggerColl);
		if (SUCCEEDED(hr))
		{
			hr = pTriggerColl->lpVtbl->Create(pTriggerColl, TASK_TRIGGER_LOGON, &pTrigger);
			if (SUCCEEDED(hr))
			{
				BSTR bstrTriggerId = alloc_bstr(L"LogonTrigger");
				pTrigger->lpVtbl->put_Id(pTrigger, bstrTriggerId);
				SysFreeString(bstrTriggerId);
				pTrigger->lpVtbl->Release(pTrigger);
			}
			pTriggerColl->lpVtbl->Release(pTriggerColl);
		}

		hr = pTaskDef->lpVtbl->get_Actions(pTaskDef, &pActionColl);
		if (SUCCEEDED(hr))
		{
			hr = pActionColl->lpVtbl->Create(pActionColl, TASK_ACTION_EXEC, &pAction);
			if (SUCCEEDED(hr))
			{
				hr = pAction->lpVtbl->QueryInterface(pAction, &IID_IExecAction, (void**)&pExecAction);
				if (SUCCEEDED(hr))
				{
					BSTR bstrPath = alloc_bstr(path);
					pExecAction->lpVtbl->put_Path(pExecAction, bstrPath);
					SysFreeString(bstrPath);
					BSTR bstrArgs = alloc_bstr(L"/startup");
					pExecAction->lpVtbl->put_Arguments(pExecAction, bstrArgs);
					SysFreeString(bstrArgs);
					pExecAction->lpVtbl->Release(pExecAction);
				}
				pAction->lpVtbl->Release(pAction);
			}
			pActionColl->lpVtbl->Release(pActionColl);
		}

		BSTR bstrTaskName = alloc_bstr(AUTOSTART_TASK_NAME);
		VARIANT varUser;
		init_variant(&varUser);
		hr = pRootFolder->lpVtbl->RegisterTaskDefinition(
			pRootFolder,
			bstrTaskName,
			pTaskDef,
			TASK_CREATE_OR_UPDATE,
			varEmpty,
			varEmpty,
			TASK_LOGON_INTERACTIVE_TOKEN,
			varUser,
			&pTask);
		SysFreeString(bstrTaskName);

		pTaskDef->lpVtbl->Release(pTaskDef);

		if (SUCCEEDED(hr))
		{
			NWL_Debug("AUTOSTART", "Set: Task created successfully");
			if (pTask)
				pTask->lpVtbl->Release(pTask);
		}
		else
		{
			NWL_Debug("AUTOSTART", "Set: RegisterTaskDefinition failed: 0x%08X", hr);
		}

		goto cleanup;
create_fail:
		pRootFolder->lpVtbl->Release(pRootFolder);
		pService->lpVtbl->Release(pService);
		CoUninitialize();
		if (show_message)
			MessageBoxW(NULL, L"?????????????????", L"??????????", MB_OK | MB_ICONERROR);
		return;
	}
	else
	{
		BSTR bstrTaskName = alloc_bstr(AUTOSTART_TASK_NAME);
		hr = pRootFolder->lpVtbl->DeleteTask(pRootFolder, bstrTaskName, 0);
		SysFreeString(bstrTaskName);
		
		if (SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		{
			NWL_Debug("AUTOSTART", "Set: Task deleted or not found");
		}
		else
		{
			NWL_Debug("AUTOSTART", "Set: DeleteTask failed: 0x%08X", hr);
		}
	}

cleanup:
	pRootFolder->lpVtbl->Release(pRootFolder);
	pService->lpVtbl->Release(pService);
	CoUninitialize();

	if (show_message)
	{
		if (enable)
		{
			MessageBoxW(NULL, L"???????????????\n\n???????????????????\n????????????????????????????", L"???????????", MB_OK | MB_ICONINFORMATION);
		}
		else
		{
			MessageBoxW(NULL, L"????????????????", L"???????????", MB_OK | MB_ICONINFORMATION);
		}
	}
}

void gnwinfo_set_autostart(nk_bool enable)
{
	gnwinfo_set_autostart_internal(enable, nk_true);
}

void gnwinfo_save_hw_config(void)
{
	WCHAR hw_path[MAX_PATH];
	WCHAR time_str[64];
	time_t now;
	struct tm tm_info;

	// printf("DEBUG: gnwinfo_save_hw_config called\n");

	PNODE old_root = NWLC->NwRoot;
	int old_format = NWLC->NwFormat;
	UINT old_codepage = NWLC->CodePage;
	NWLC->NwRoot = NWL_NodeAlloc("NWinfo", 0);
	NWLC->NwFormat = FORMAT_JSON;
	NWLC->CodePage = CP_UTF8;

	if (!NWLC->NwRoot)
	{
		NWLC->NwRoot = old_root;
		NWLC->NwFormat = old_format;
		NWLC->CodePage = old_codepage;
		return;
	}

	NW_System(TRUE);
	NW_Uefi(TRUE);
	NW_Cpuid(TRUE);
	NW_Smbios(TRUE);
	__try
	{
		NW_Mainboard(TRUE);
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
	}
	NW_Disk(TRUE);
	NW_Edid(TRUE);
	NW_Pci(TRUE);
	__try
	{
		NW_Spd(TRUE);
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
	}

	PNODE display_settings = NWL_NodeAppendNew(NWLC->NwRoot, "DisplaySettings", 0);
	if (display_settings)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%ldx%ld", g_ctx.cur_display.Width, g_ctx.cur_display.Height);
		NWL_NodeAttrSet(display_settings, "Resolution", buf, 0);
		snprintf(buf, sizeof(buf), "%u", g_ctx.cur_display.Dpi);
		NWL_NodeAttrSet(display_settings, "DPI", buf, 0);
		snprintf(buf, sizeof(buf), "%u", g_ctx.cur_display.Scale);
		NWL_NodeAttrSet(display_settings, "Scale", buf, 0);
	}

	time(&now);
	localtime_s(&tm_info, &now);
	char timestamp_buf[64];
	strftime(timestamp_buf, sizeof(timestamp_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
	PNODE config_info = NWL_NodeAppendNew(NWLC->NwRoot, "ConfigInfo", 0);
	if (config_info)
	{
		NWL_NodeAttrSet(config_info, "Timestamp", timestamp_buf, 0);
	}

	if (GetEnvironmentVariableW(L"USERPROFILE", hw_path, MAX_PATH) > 0)
	{
		wcscat_s(hw_path, MAX_PATH, L"\\herosys_data");
	}
	else
	{
		GetModuleFileNameW(NULL, hw_path, MAX_PATH);
		WCHAR* last_slash = wcsrchr(hw_path, L'\\');
		if (last_slash)
			*(last_slash + 1) = L'\0';
		wcscat_s(hw_path, MAX_PATH, L"herosys_data");
	}
	CreateDirectoryW(hw_path, NULL);
	wcscat_s(hw_path, MAX_PATH, L"\\");

	time(&now);
	localtime_s(&tm_info, &now);
	wcsftime(time_str, ARRAYSIZE(time_str), L"hw_config_%Y%m%d_%H%M%S.json", &tm_info);
	wcscat_s(hw_path, MAX_PATH, time_str);

	FILE* fp = NULL;
	if (_wfopen_s(&fp, hw_path, L"w") == 0 && fp)
	{
		NW_Export(NWLC->NwRoot, fp);
		fclose(fp);
		printf("DEBUG: JSON file saved: %S\n", hw_path);
		// printf("DEBUG: JSON file saved: %S\n", hw_path);
		fflush(stdout);
	}
	else
	{
		// printf("DEBUG: Failed to save JSON file: %S\n", hw_path);
		fflush(stdout);
	}

	NWL_NodeFree(NWLC->NwRoot, 1);
	NWLC->NwRoot = old_root;
	NWLC->NwFormat = old_format;
	NWLC->CodePage = old_codepage;
}

static void create_tray_icon(HWND wnd)
{
	memset(&g_nid, 0, sizeof(NOTIFYICONDATAW));
	g_nid.cbSize = sizeof(NOTIFYICONDATAW);
	g_nid.hWnd = wnd;
	g_nid.uID = ID_TRAY_ICON;
	g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	g_nid.uCallbackMessage = WM_TRAYMESSAGE;
	g_nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON1));
	wcscpy_s(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"\x5de5\x63a7\x673a\x54e8\x5175");
	Shell_NotifyIconW(NIM_ADD, &g_nid);
	g_tray_created = 1;
}

static void remove_tray_icon(void)
{
	if (g_tray_created)
	{
		Shell_NotifyIconW(NIM_DELETE, &g_nid);
		g_tray_created = 0;
	}
}

static void show_tray_menu(HWND wnd, POINT pt)
{
	HMENU hMenu = CreatePopupMenu();
	AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, NWL_Utf8ToUcs2(N_(N__DISPLAYMAIN)));
	
	AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, NWL_Utf8ToUcs2(N_(N__DISPLAYQUIT)));

	SetForegroundWindow(wnd);
	TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, wnd, NULL);
	PostMessageW(wnd, WM_NULL, 0, 0);
	DestroyMenu(hMenu);
}


#define REGION_MASK_LEFT    (1 << 0)
#define REGION_MASK_RIGHT   (1 << 1)
#define REGION_MASK_TOP     (1 << 2)
#define REGION_MASK_BOTTOM  (1 << 3)

static void
set_dpi_scaling(HWND wnd)
{
	WCHAR font_name[64];
	GetPrivateProfileStringW(L"Window", L"Font", L"-", font_name, 64, g_ini_path);
	if (wcscmp(font_name, L"-") == 0)
		wcscpy_s(font_name, 64, NWL_Utf8ToUcs2(N_(N__FONT_)));
	if (g_bginfo)
		g_dpi_scaling = 0;
	else
		g_dpi_scaling = strtol(gnwinfo_get_ini_value(L"Window", L"DpiScaling", L"1"), NULL, 10);
	if (g_font)
	{
		nk_gdipfont_del(g_font);
		g_font = NULL;
	}
	if (g_dpi_scaling)
	{
		UINT dpi = GetDpiForWindow(wnd);
		double new_dpi_factor = (double)dpi / USER_DEFAULT_SCREEN_DPI;
		double ratio = new_dpi_factor / g_dpi_factor;
		
		g_dpi_factor = new_dpi_factor;
		m_dpi = dpi;
		
		RECT rect = { 0 };
		GetWindowRect(wnd, &rect);
		int new_width = (int)((rect.right - rect.left) * ratio);
		int new_height = (int)((rect.bottom - rect.top) * ratio);
		
		static int base_font_size = 0;
		if (base_font_size == 0)
			base_font_size = strtol(gnwinfo_get_ini_value(L"Window", L"FontSize", L"16"), NULL, 10);
		g_font_size = (int)(base_font_size * g_dpi_factor);
		
		SetWindowPos(wnd, NULL, 0, 0, new_width, new_height,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		nk_gdip_resize(new_width, new_height);
	}
	g_font = nk_gdip_load_font(font_name, g_font_size);
	nk_gdip_set_font(g_font);
}

static LRESULT CALLBACK
window_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
	case WM_CREATE:
		if (g_startup_mode)
		{
			SetTimer(wnd, IDT_TIMER_TRAY_DELAY, 5000, NULL);
		}
		else
		{
			create_tray_icon(wnd);
		}
		break;
	case WM_DESTROY:
		remove_tray_icon();
		PostQuitMessage(0);
		break;
	case WM_CLOSE:
		ShowWindow(wnd, SW_HIDE);
		return 0;
	case WM_COMMAND:
	{
		switch (LOWORD(wparam))
		{
		case ID_TRAY_SHOW:
		{
			struct nk_window* win;
			const char* title = u8"���ػ��ڱ�";
			for (win = g_ctx.nk->begin; win != NULL; win = win->next)
			{
				if (strcmp(win->name_string, title) == 0)
				{
					win->flags &= ~NK_WINDOW_HIDDEN;
					break;
				}
			}
			memset(&g_ctx.nk->input, 0, sizeof(g_ctx.nk->input));
			ShowWindow(wnd, SW_RESTORE);
			SetForegroundWindow(wnd);
		}
		break;
		case ID_TRAY_EXIT:
			PostMessageW(wnd, WM_DESTROY, 0, 0);
			break;
		}
	}
	break;
	case WM_TRAYMESSAGE:
	{
		switch (lparam)
		{
		case WM_RBUTTONUP:
		{
			POINT pt;
			GetCursorPos(&pt);
			show_tray_menu(wnd, pt);
		}
		break;
		case WM_LBUTTONDBLCLK:
		{
			struct nk_window* win;
			const char* title = u8"���ػ��ڱ�";
			for (win = g_ctx.nk->begin; win != NULL; win = win->next)
			{
				if (strcmp(win->name_string, title) == 0)
				{
					win->flags &= ~NK_WINDOW_HIDDEN;
					break;
				}
			}
			memset(&g_ctx.nk->input, 0, sizeof(g_ctx.nk->input));
			ShowWindow(wnd, SW_RESTORE);
			SetForegroundWindow(wnd);
		}
		break;
		}
	}
	break;
	case WM_SHOWMAIN:
	{
		struct nk_window* win;
		const char* title = u8"���ػ��ڱ�";
		for (win = g_ctx.nk->begin; win != NULL; win = win->next)
		{
			if (strcmp(win->name_string, title) == 0)
			{
				win->flags &= ~NK_WINDOW_HIDDEN;
				break;
			}
		}
		memset(&g_ctx.nk->input, 0, sizeof(g_ctx.nk->input));
		ShowWindow(wnd, SW_RESTORE);
		SetForegroundWindow(wnd);
		g_window_was_hidden = nk_false;
	}
	break;
	case WM_TIMER:
		if (wparam == IDT_TIMER_TRAY_DELAY)
		{
			if (!g_tray_created)
			{
				create_tray_icon(wnd);
			}
			KillTimer(wnd, IDT_TIMER_TRAY_DELAY);
		}
		else
		{
			gnwinfo_ctx_update(wparam);
		}
		break;
	case WM_DEVICECHANGE:
	{
		switch (wparam)
		{
		case DBT_DEVNODES_CHANGED:
			// TODO: check if this is needed
			break;
		case DBT_DEVICEARRIVAL:
		case DBT_DEVICEREMOVECOMPLETE:
			gnwinfo_ctx_update(IDT_TIMER_DISK);
			break;
		}
	}
		break;
	case WM_DPICHANGED:
	{
		UINT dpi = HIWORD(wparam);
		RECT* rect = (RECT*)lparam;
		double new_dpi_factor = (double)dpi / USER_DEFAULT_SCREEN_DPI;
		double ratio = new_dpi_factor / g_dpi_factor;
		
		g_dpi_factor = new_dpi_factor;
		m_dpi = dpi;
		
		int new_width = rect->right - rect->left;
		int new_height = rect->bottom - rect->top;
		
		SetWindowPos(wnd, NULL, rect->left, rect->top, new_width, new_height,
			SWP_NOZORDER | SWP_NOACTIVATE);
		
		nk_gdip_resize(new_width, new_height);
		
		WCHAR font_name[64];
		GetPrivateProfileStringW(L"Window", L"Font", L"-", font_name, 64, g_ini_path);
		if (wcscmp(font_name, L"-") == 0)
			wcscpy_s(font_name, 64, NWL_Utf8ToUcs2(N_(N__FONT_)));
		if (g_font)
		{
			nk_gdipfont_del(g_font);
			g_font = NULL;
		}
		static int base_font_size = 0;
		if (base_font_size == 0)
			base_font_size = strtol(gnwinfo_get_ini_value(L"Window", L"FontSize", L"16"), NULL, 10);
		g_font_size = (int)(base_font_size * g_dpi_factor);
		g_font = nk_gdip_load_font(font_name, g_font_size);
		nk_gdip_set_font(g_font);
	}
	break;
	case WM_POWERBROADCAST:
		gnwinfo_ctx_update(IDT_TIMER_POWER);
		break;
	case WM_DISPLAYCHANGE:
		gnwinfo_ctx_update(IDT_TIMER_DISPLAY);
		if (g_bginfo)
		{
			int x = 0;
			RECT desktop = { 0, 0, 1024, 768 };
			GetWindowRect(GetDesktopWindow(), &desktop);
			if (desktop.right > (LONG)g_init_width)
				x = desktop.right - (LONG)g_init_width;
			SetWindowPos(wnd, HWND_BOTTOM, x, 0, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
		}
		break;
	case WM_MOUSEMOVE:
		if (g_bginfo && !g_ctx.mouse)
		{
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_HOVER | TME_LEAVE;
			tme.hwndTrack = wnd;
			tme.dwHoverTime = 100;
			TrackMouseEvent(&tme);
			g_ctx.mouse = TRUE;
		}
		break;
	case WM_MOUSEHOVER:
		if (g_bginfo)
		{
			g_ctx.mouse = FALSE;
#ifdef GNWINFO_TRANSPARENT
			SetLayeredWindowAttributes(wnd, 0, (BYTE)g_init_alpha, LWA_ALPHA);
#endif
			SetForegroundWindow(wnd);
		}
		break;
	case WM_MOUSELEAVE:
		if (g_bginfo)
		{
			g_ctx.mouse = FALSE;
#ifdef GNWINFO_TRANSPARENT
			SetLayeredWindowAttributes(wnd, RGB(g_color_back.r, g_color_back.g, g_color_back.b), (BYTE)g_init_alpha, LWA_COLORKEY | LWA_ALPHA);
#endif
			SetWindowPos(wnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		break;
	case WM_NCHITTEST:
		if (!g_bginfo)
		{
			RECT rect = { 0 };
			LONG x = GET_X_LPARAM(lparam);
			LONG y = GET_Y_LPARAM(lparam);
			GetWindowRect(wnd, &rect);
			if (y <= (LONG)(rect.top + g_ctx.gui_title) &&
				x <= (LONG)(rect.right - 3 * g_ctx.gui_title))
				return HTCAPTION;
		}
		break;
	case WM_SIZE:
		g_ctx.gui_height = HIWORD(lparam);
		g_ctx.gui_width = LOWORD(lparam);
		break;
	}
	if (nk_gdip_handle_event(wnd, msg, wparam, lparam))
		return 0;
	return DefWindowProcW(wnd, msg, wparam, lparam);
}

static void
get_ini_color(LPCWSTR key, struct nk_color* color)
{
	WCHAR fallback[7];
	UINT32 hex;
	swprintf(fallback, 7, L"%02X%02X%02X", color->r, color->g, color->b);
	hex = strtoul(gnwinfo_get_ini_value(L"Color", key, fallback), NULL, 16);
	color->r = (hex >> 16) & 0xFF;
	color->g = (hex >> 8) & 0xFF;
	color->b = hex & 0xFF;
}

static void
parse_cmdline(LPWSTR cmdline)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(cmdline, &argc);
	if (!argv)
		return;
	for (int i = 0; i < argc; i++)
	{
		if (_wcsicmp(argv[i], L"/debug") == 0)
		{
			if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
			{
				AllocConsole();
			}
			FILE* fp = NULL;
			freopen_s(&fp, "CONOUT$", "w", stdout);
			freopen_s(&fp, "CONOUT$", "w", stderr);
			setvbuf(stdout, NULL, _IONBF, 0);
			setvbuf(stderr, NULL, _IONBF, 0);
			g_debug = 1;
		}
		else if (_wcsicmp(argv[i], L"/startup") == 0)
		{
			g_startup_mode = 1;
		}
	}
	LocalFree(argv);
}

int APIENTRY
wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow)
{
	struct nk_context* ctx;
	const char* str;
	int x_pos, y_pos;
	WNDCLASSW wc;
	DWORD style = WS_POPUP | WS_VISIBLE;
	DWORD exstyle = WS_EX_LAYERED;
	HWND wnd;
	int running = 1;
	int needs_refresh = 1;
	DWORD layered_flag = LWA_ALPHA;
	HANDLE hMutex;
//2026.5.1  
	SYSTEMTIME expireDate = { 0 };
	expireDate.wYear = 2026;
	expireDate.wMonth = 5;
	expireDate.wDay = 1;
	SYSTEMTIME currentTime = { 0 };
	GetLocalTime(&currentTime);
	FILETIME ftExpire, ftCurrent;
	SystemTimeToFileTime(&expireDate, &ftExpire);
	SystemTimeToFileTime(&currentTime, &ftCurrent);
	if (CompareFileTime(&ftCurrent, &ftExpire) > 0)
	{
		/*MessageBoxW(NULL, L"\x8f6f\x4ef6\x5df2\x8fc7\x671f\x2c\x65e0\x6cd5\x8fd0\x884c", 
			L"\x5de5\x63a7\x673a\x54e8\x5175", MB_OK | MB_ICONERROR);*/
		return 0;
	}
//time  end
	parse_cmdline(lpCmdLine);

	hMutex = CreateMutexW(NULL, TRUE, L"NWinfo{e25f6e37-d51b-4950-8949-510dfc86d913}");
	if (GetLastError() == ERROR_ALREADY_EXISTS || !hMutex)
	{
		HWND existingWnd = FindWindowW(L"NwinfoWindowClass", NULL);
		if (existingWnd)
		{
			ShowWindow(existingWnd, SW_RESTORE);
			SetForegroundWindow(existingWnd);
			PostMessageW(existingWnd, WM_SHOWMAIN, 0, 0);
		}
		if (hMutex)
			CloseHandle(hMutex);
		return 0;
	}

	GetModuleFileNameW(NULL, g_ini_path, MAX_PATH);
	PathCchRemoveFileSpec(g_ini_path, MAX_PATH);
	PathCchAppend(g_ini_path, MAX_PATH, L"gnwinfo.ini");
	x_pos = strtol(gnwinfo_get_ini_value(L"Window", L"X", L"100"), NULL, 10);
	y_pos = strtol(gnwinfo_get_ini_value(L"Window", L"Y", L"10"), NULL, 10);
	{
		int screen_width = GetSystemMetrics(SM_CXSCREEN);
		int screen_height = GetSystemMetrics(SM_CYSCREEN);
		g_init_width = (unsigned int)(screen_width * 0.52f);
		g_init_height = (unsigned int)(screen_height * 0.65f);
		if (g_init_width < 600) g_init_width = 600;
		if (g_init_height < 500) g_init_height = 500;
		if (g_init_width > (unsigned int)(screen_width - 100)) g_init_width = screen_width - 100;
		if (g_init_height > (unsigned int)(screen_height - 100)) g_init_height = screen_height - 100;
	}
	g_init_alpha = strtoul(gnwinfo_get_ini_value(L"Window", L"Alpha", L"255"), NULL, 10);
	g_smart_interval = strtoul(gnwinfo_get_ini_value(L"Window", L"SmartInterval", L"600"), NULL, 10);
	if (g_smart_interval < 60) g_smart_interval = 60;
	if (g_smart_interval > 86400) g_smart_interval = 86400;
	g_font_size = strtol(gnwinfo_get_ini_value(L"Window", L"FontSize", L"16"), NULL, 10);
	str = gnwinfo_get_ini_value(L"Window", L"AutoStart", L"-1");
	if (strcmp(str, "-1") == 0)
	{
		g_autostart = nk_false;
		g_need_save_hw_config = nk_true;
		__try
		{
			gnwinfo_set_autostart_internal(nk_true, nk_false);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			NWL_Debug("AUTOSTART", "Set: Exception in initialization");
		}
		gnwinfo_set_ini_value(L"Window", L"AutoStart", L"%d", 1);
	}
	else
	{
		int autostart_val = strtol(str, NULL, 10);
		g_autostart = (autostart_val == 0) ? nk_true : nk_false;
		__try
		{
			gnwinfo_set_autostart_internal(autostart_val != 0, nk_false);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			NWL_Debug("AUTOSTART", "Set: Exception in initialization");
		}
	}
	str = gnwinfo_get_ini_value(L"Window", L"BGInfo", L"0");
	if (str[0] != '0')
	{
		RECT desktop = {0, 0, 1024, 768};
		g_bginfo = 1;
		exstyle |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
		GetWindowRect(GetDesktopWindow(), &desktop);
		x_pos = desktop.right > (LONG)g_init_width ? (desktop.right - (LONG)g_init_width) : 0;
		y_pos = 0;
	}
	get_ini_color(L"Background", &g_color_back);
	get_ini_color(L"Highlight", &g_color_text_l);
	get_ini_color(L"Default", &g_color_text_d);
	get_ini_color(L"StateGood", &g_color_good);
	get_ini_color(L"StateWarn", &g_color_warning);
	get_ini_color(L"StateError", &g_color_error);
	get_ini_color(L"StateUnknown", &g_color_unknown);

	/* Win32 */
	memset(&wc, 0, sizeof(wc));
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = window_proc;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.lpszClassName = L"NwinfoWindowClass";
	RegisterClassW(&wc);

	unsigned int startup_width = (unsigned int)(500 * g_dpi_factor);
	unsigned int startup_height = (unsigned int)(600 * g_dpi_factor);

	wnd = CreateWindowExW(exstyle, wc.lpszClassName, L"\x5de5\x63a7\x673a\x54e8\x5175", style,
		x_pos, y_pos, startup_width, startup_height, NULL, NULL, wc.hInstance, NULL);

	if (g_bginfo)
	{
		SetWindowPos(wnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#ifdef GNWINFO_TRANSPARENT
		layered_flag |= LWA_COLORKEY;
#endif
	}

	SetLayeredWindowAttributes(wnd, RGB(g_color_back.r, g_color_back.g, g_color_back.b), (BYTE)g_init_alpha, layered_flag);

	if (g_startup_mode)
	{
		ShowWindow(wnd, SW_HIDE);
		g_window_was_hidden = nk_true;
	}

	/* GUI */
	ctx = nk_gdip_init(wnd, startup_width, startup_height);
	set_dpi_scaling(wnd);

	(void)CoInitializeEx(0, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
	gnwinfo_set_style(ctx);
	gnwinfo_ctx_init(hInstance, wnd, ctx, (float)(startup_width), (float)(startup_height));

	while (running)
	{
		/* Input */
		MSG msg;
		nk_input_begin(ctx);
		if (needs_refresh == 0)
		{
			if (GetMessageW(&msg, NULL, 0, 0) <= 0)
				running = 0;
			else
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
			needs_refresh = 1;
		}
		else
			needs_refresh = 0;
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				running = 0;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
			needs_refresh = 1;
		}
		nk_input_end(ctx);

		/* GUI */
		AcquireSRWLockExclusive(&g_ctx.lock);
		
		static int last_interface = -2;
		int current_interface = gnwinfo_get_display_interface();
		if (current_interface != last_interface)
		{
			last_interface = current_interface;
			if (current_interface == -1)
			{
				int startup_width = (int)(500 * g_dpi_factor);
				int startup_height = (int)(600 * g_dpi_factor);
				SetWindowPos(wnd, NULL, 0, 0, g_init_width, g_init_height, SWP_NOMOVE | SWP_NOZORDER);
				nk_gdip_resize(g_init_width, g_init_height);
			}
			else if (current_interface == 0 || current_interface == 1 || current_interface == 2)
			{
				SetWindowPos(wnd, NULL, 0, 0, (int)g_init_width, (int)g_init_height, SWP_NOMOVE | SWP_NOZORDER);
				nk_gdip_resize((int)g_init_width, (int)g_init_height);
			}
		}
		
		if (g_ctx.window_flag & GUI_WINDOW_SETTINGS)
			gnwinfo_set_style(ctx);
		gnwinfo_draw_main_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_cpuid_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_about_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_smart_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_settings_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_customize_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_pci_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_dmi_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_display_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_mm_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		gnwinfo_draw_hostname_window(ctx, g_ctx.gui_width, g_ctx.gui_height);
		ReleaseSRWLockExclusive(&g_ctx.lock);
		if (g_ctx.exit_pending)
			gnwinfo_ctx_exit();

		/* Draw */
		nk_gdip_render(g_ctx.gui_aa, g_color_back);
	}

	CoUninitialize();
	nk_gdipfont_del(g_font);
	nk_gdip_shutdown();
	UnregisterClassW(wc.lpszClassName, wc.hInstance);
	gnwinfo_ctx_exit();
	if (hMutex)
		CloseHandle(hMutex);
	return 0;
}
