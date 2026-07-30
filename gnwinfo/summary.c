// SPDX-License-Identifier: Unlicense

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gnwinfo.h"
#include "gettext.h"
#include "utils.h"

static CHAR m_buf[MAX_PATH];
static int g_group_index = 0;

static struct nk_color g_color_separator = { 0xC0, 0xC0, 0xC0, 0xFF };

static double get_fan_speed_from_sensors(const char* fan_name_prefix)
{
	PNODE parent = NWL_NodeAlloc("temp_sensors", 0);
	if (!parent)
		return -1.0;

	NWL_InitSensors(NWL_SENSOR_HWINFO);
	PNODE sensors = NWL_GetSensors(parent);
	if (!sensors)
	{
		NWL_NodeFree(parent, 1);
		return -1.0;
	}

	double fan_speed = -1.0;
	int sensor_count = NWL_NodeChildCount(sensors);
	for (int i = 0; i < sensor_count; i++)
	{
		PNODE sensor = NWL_NodeEnumChild(sensors, i);
		if (!sensor)
			continue;

		int attr_count = NWL_NodeAttrCount(sensor);
		for (int j = 0; j < attr_count; j++)
		{
			PNODE_ATT att = NWL_NodeAttrEnum(sensor, j);
			if (!att || !att->key || !att->value)
				continue;

			if (strstr(att->key, fan_name_prefix) && strstr(att->key, "Fan"))
			{
				fan_speed = atof(att->value);
				if (fan_speed > 0)
					break;
			}
		}
		if (fan_speed > 0)
			break;
	}

	NWL_NodeFree(parent, 1);
	return fan_speed;
}

static int g_mem_test_running = 0;
static int g_mem_test_result = -1;
static DWORD64 g_mem_test_size = 0;
static HANDLE g_mem_test_thread = NULL;
static char g_mem_test_time[128] = { 0 };
static int g_mem_test_loaded = 0;
static int g_cpu_test_running = 0;
static int g_cpu_test_result = -1;
static HANDLE g_cpu_test_thread = NULL;
static char g_cpu_test_time[128] = { 0 };
static char g_cpu_test_date[128] = { 0 };
static int g_cpu_test_loaded = 0;

static int g_optimize_running = 0;
static int g_optimize_progress = 0;
static HANDLE g_optimize_thread = NULL;

#define BLOCK_SIZE (64 * 1024 * 1024)
#define TEST_PATTERNS 4

static void load_mem_test_result(void)
{
	FILE* fp;
	char path[MAX_PATH];
	char backup_dir[MAX_PATH];
	char line[256];
	
	if (GetEnvironmentVariableA("USERPROFILE", backup_dir, MAX_PATH) > 0) {
		sprintf_s(path, MAX_PATH, "%s\\herosys_data\\backup\\mem_test_result.txt", backup_dir);
	} else {
		return;
	}
	
	fopen_s(&fp, path, "r");
	if (fp)
	{
		if (fgets(line, sizeof(line), fp))
		{
			if (sscanf_s(line, "%d %lld %s", &g_mem_test_result, &g_mem_test_size, g_mem_test_time, (unsigned)_countof(g_mem_test_time)) == 3)
			{
			}
		}
		fclose(fp);
	}
}

static void save_mem_test_result(int result, DWORD64 size)
{
	FILE* fp;
	char path[MAX_PATH];
	char backup_dir[MAX_PATH];
	
	if (GetEnvironmentVariableA("USERPROFILE", backup_dir, MAX_PATH) > 0) {
		sprintf_s(path, MAX_PATH, "%s\\herosys_data\\backup\\mem_test_result.txt", backup_dir);
	} else {
		return;
	}
	
	fopen_s(&fp, path, "w");
	if (fp)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		sprintf_s(g_mem_test_time, sizeof(g_mem_test_time), "%04d-%02d-%02d_%02d:%02d:%02d", 
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		fprintf(fp, "%d %lld %s\n", result, size, g_mem_test_time);
		fclose(fp);
	}
}

static int test_block(void* ptr, SIZE_T size)
{
	BYTE* p = (BYTE*)ptr;
	BYTE pattern;

	for (int pat = 0; pat < TEST_PATTERNS; pat++)
	{
		switch (pat)
		{
			case 0: pattern = 0x00; break;
			case 1: pattern = 0xFF; break;
			case 2: pattern = 0xAA; break;
			case 3: pattern = 0x55; break;
			default: pattern = 0x00;
		}

		memset(p, pattern, size);

		for (SIZE_T i = 0; i < size; i++)
		{
			if (p[i] != pattern)
			{
				return 0;
			}
		}
	}
	return 1;
}

static DWORD WINAPI memory_test_thread(LPVOID param)
{
	MEMORYSTATUSEX memInfo;
	memInfo.dwLength = sizeof(memInfo);
	GlobalMemoryStatusEx(&memInfo);

	SIZE_T totalMem = (SIZE_T)(memInfo.ullTotalPhys);
	SIZE_T reserveMem = 512 * 1024 * 1024;
	SIZE_T testMem = totalMem - reserveMem;
	SIZE_T blocks = testMem / BLOCK_SIZE;

	if (blocks < 1)
		blocks = 1;

	void** bufs = (void**)malloc(blocks * sizeof(void*));
	if (!bufs)
	{
		g_mem_test_result = -1;
		g_mem_test_running = 0;
		return 0;
	}

	SIZE_T allocated_blocks = 0;
	for (SIZE_T i = 0; i < blocks; i++)
	{
		bufs[i] = VirtualAlloc(NULL, BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!bufs[i])
		{
			break;
		}
		allocated_blocks++;
	}

	g_mem_test_size = blocks * BLOCK_SIZE;

	DWORD errors = 0;

	for (int pass = 1; pass <= 3; pass++)
	{
		for (SIZE_T i = 0; i < allocated_blocks; i++)
		{
			if (!test_block(bufs[i], BLOCK_SIZE))
			{
				errors++;
			}
		}
	}

	for (SIZE_T i = 0; i < allocated_blocks; i++)
		VirtualFree(bufs[i], 0, MEM_RELEASE);

	free(bufs);

	g_mem_test_result = (int)errors;
	g_mem_test_size = allocated_blocks * BLOCK_SIZE;
	save_mem_test_result((int)errors, g_mem_test_size);
	g_mem_test_running = 0;
	return 0;
}

static void start_memory_test(void)
{
	if (g_mem_test_running)
		return;

	g_mem_test_running = 1;
	g_mem_test_result = -1;
	g_mem_test_thread = CreateThread(NULL, 0, memory_test_thread, NULL, 0, NULL);
	if (g_mem_test_thread)
		CloseHandle(g_mem_test_thread);
}

static void start_cpu_test(void);
static void save_cpu_test_result(int result, UINT64 prime_count, UINT64 total_tests);
static void load_cpu_test_result(void);

static BOOL test_prime(UINT64 n)
{
	if (n < 2) return FALSE;
	if (n == 2) return TRUE;
	if (n % 2 == 0) return FALSE;
	UINT64 sqrt_n = (UINT64)sqrt((double)n);
	for (UINT64 i = 3; i <= sqrt_n; i += 2) {
		if (n % i == 0) return FALSE;
	}
	return TRUE;
}

static DWORD WINAPI cpu_test_thread(LPVOID param)
{
	UINT64 errors = 0;
	UINT64 total_tests = 0;
	UINT64 prime_count = 0;
	
	SYSTEMTIME start_time;
	GetLocalTime(&start_time);
	
	for (int pass = 0; pass < 3; pass++) {
		for (UINT64 n = 2; n < 100000; n++) {
			if (test_prime(n)) {
				prime_count++;
			}
			total_tests++;
		}
		
		volatile double sum = 0.0;
		for (int i = 0; i < 10000000; i++) {
			sum += sin((double)i) * cos((double)i);
		}
		
		UINT64 matrix[8][8];
		for (int i = 0; i < 8; i++) {
			for (int j = 0; j < 8; j++) {
				matrix[i][j] = (i * 8 + j + pass) * 7 % 1000;
			}
		}
		
		UINT64 result[8][8] = {0};
		for (int i = 0; i < 8; i++) {
			for (int j = 0; j < 8; j++) {
				for (int k = 0; k < 8; k++) {
					result[i][j] += matrix[i][k] * matrix[k][j];
				}
			}
		}
	}
	
	SYSTEMTIME end_time;
	GetLocalTime(&end_time);
	
	WORD start_ms = start_time.wHour * 3600000 + start_time.wMinute * 60000 + 
					 start_time.wSecond * 1000 + start_time.wMilliseconds;
	WORD end_ms = end_time.wHour * 3600000 + end_time.wMinute * 60000 + 
				  end_time.wSecond * 1000 + end_time.wMilliseconds;
	int duration = (end_ms - start_ms) / 1000;
	
	snprintf(g_cpu_test_time, sizeof(g_cpu_test_time), "%02d:%02d:%02d", 
			 duration / 3600, (duration % 3600) / 60, duration % 60);
	
	snprintf(g_cpu_test_date, sizeof(g_cpu_test_date), "%04d-%02d-%02d %02d:%02d:%02d",
		end_time.wYear, end_time.wMonth, end_time.wDay,
		end_time.wHour, end_time.wMinute, end_time.wSecond);
	
	g_cpu_test_result = (int)(errors > 0 ? 1 : 0);
	save_cpu_test_result(g_cpu_test_result, prime_count, total_tests);
	g_cpu_test_running = 0;
	return 0;
}

static void start_cpu_test(void)
{
	if (g_cpu_test_running)
		return;

	g_cpu_test_running = 1;
	g_cpu_test_result = -1;
	g_cpu_test_thread = CreateThread(NULL, 0, cpu_test_thread, NULL, 0, NULL);
	if (g_cpu_test_thread)
		CloseHandle(g_cpu_test_thread);
}

static void save_cpu_test_result(int result, UINT64 prime_count, UINT64 total_tests)
{
	FILE* fp;
	char path[MAX_PATH];
	char backup_dir[MAX_PATH];
	
	if (GetEnvironmentVariableA("USERPROFILE", backup_dir, MAX_PATH) > 0) {
		sprintf_s(path, MAX_PATH, "%s\\herosys_data\\backup\\cpu_test_result.txt", backup_dir);
	} else {
		return;
	}
	
	char dir_path[MAX_PATH];
	sprintf_s(dir_path, MAX_PATH, "%s\\herosys_data\\backup", backup_dir);
	CreateDirectoryA(dir_path, NULL);
	
	fopen_s(&fp, path, "w");
	if (fp)
	{
		fprintf(fp, "result=%d\n", result);
		fprintf(fp, "prime_count=%llu\n", prime_count);
		fprintf(fp, "total_tests=%llu\n", total_tests);
		fprintf(fp, "duration=%s\n", g_cpu_test_time);
		fprintf(fp, "test_time=%s\n", g_cpu_test_date);
		fclose(fp);
	}
}

static void load_cpu_test_result(void)
{
	FILE* fp;
	char path[MAX_PATH];
	char backup_dir[MAX_PATH];
	char line[256];
	
	if (GetEnvironmentVariableA("USERPROFILE", backup_dir, MAX_PATH) > 0) {
		sprintf_s(path, MAX_PATH, "%s\\herosys_data\\backup\\cpu_test_result.txt", backup_dir);
	} else {
		return;
	}
	
	fopen_s(&fp, path, "r");
	if (fp)
	{
		while (fgets(line, sizeof(line), fp))
		{
			if (strncmp(line, "result=", 7) == 0)
			{
				g_cpu_test_result = atoi(line + 7);
				g_cpu_test_loaded = 1;
			}
			else if (strncmp(line, "duration=", 9) == 0)
			{
				strncpy_s(g_cpu_test_time, sizeof(g_cpu_test_time), line + 9, _TRUNCATE);
				char* p = strchr(g_cpu_test_time, '\n');
				if (p) *p = '\0';
			}
			else if (strncmp(line, "test_time=", 10) == 0)
			{
				strncpy_s(g_cpu_test_date, sizeof(g_cpu_test_date), line + 10, _TRUNCATE);
				char* p = strchr(g_cpu_test_date, '\n');
				if (p) *p = '\0';
			}
		}
		fclose(fp);
	}
}

static DWORD WINAPI optimize_thread(LPVOID param)
{
	char* script_args = (char*)param;
	
	char exe_path[MAX_PATH];
	char full_script_path[MAX_PATH];
	char cmd_line[MAX_PATH * 2];
	char backup_dir[MAX_PATH];
	
	GetModuleFileNameA(NULL, exe_path, MAX_PATH);
	char* last_backslash = strrchr(exe_path, '\\');
	if (last_backslash) *last_backslash = '\0';
	
	if (GetEnvironmentVariableA("USERPROFILE", backup_dir, MAX_PATH) > 0) {
		strcat_s(backup_dir, MAX_PATH, "\\herosys_data\\backup");
	} else {
		sprintf_s(backup_dir, MAX_PATH, "%s\\herosys_data\\backup", exe_path);
	}
	
	char script_name[MAX_PATH] = {0};
	char* args = "";
	
	if (strchr(script_args, ' ')) {
		char* space = strchr(script_args, ' ');
		size_t name_len = space - script_args;
		strncpy_s(script_name, MAX_PATH, script_args, name_len);
		script_name[name_len] = '\0';
		args = space + 1;
	} else {
		strncpy_s(script_name, MAX_PATH, script_args, _TRUNCATE);
	}
	
	STARTUPINFOA si = {0};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {0};
	
	sprintf_s(full_script_path, MAX_PATH, "%s\\ICP-Optimizer\\%s", exe_path, script_name);
	sprintf_s(cmd_line, MAX_PATH * 2, 
		"powershell.exe -ExecutionPolicy Bypass -NoProfile -File \"%s\" %s -BackupDir \"%s\"", 
		full_script_path, args, backup_dir);
	
	g_optimize_progress = 0;
	CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &si, &pi);
	
	while (1) {
		DWORD result = WaitForSingleObject(pi.hProcess, 500);
		if (result == WAIT_OBJECT_0) {
			g_optimize_progress = 100;
			break;
		}
		if (g_optimize_progress < 95) {
			g_optimize_progress += 5;
		}
	}
	
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	
	g_optimize_running = 0;
	free(script_args);
	return 0;
}

VOID
run_powershell_script(LPCSTR script_name_with_args)
{
	if (g_optimize_running)
		return;
	
	g_optimize_running = 1;
	g_optimize_progress = 0;
	g_ctx.window_flag |= GUI_WINDOW_OPTIMIZE;
	
	char* args_copy = _strdup(script_name_with_args);
	g_optimize_thread = CreateThread(NULL, 0, optimize_thread, args_copy, 0, NULL);
	if (g_optimize_thread)
		CloseHandle(g_optimize_thread);
}

VOID
gnwinfo_draw_optimize_window(struct nk_context* ctx, float width, float height)
{
	if (!(g_ctx.window_flag & GUI_WINDOW_OPTIMIZE))
		return;
	
	if (!nk_begin_ex(ctx, u8"优化",
		nk_rect(width / 4.0f, height / 3.0f, 400, 200),
		NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE,
		GET_PNG(IDR_PNG_INFO), GET_PNG(IDR_PNG_CLOSE)))
	{
		g_ctx.window_flag &= ~GUI_WINDOW_OPTIMIZE;
		goto out;
	}
	
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_spacer(ctx);
	
	if (g_optimize_progress >= 100) {
		nk_l(ctx, u8"优化完成", NK_TEXT_CENTERED);
	} else {
		nk_l(ctx, u8"系统优化中...", NK_TEXT_CENTERED);
	}
	
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_spacer(ctx);
	
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_lhcf(ctx, NK_TEXT_CENTERED, g_color_good, u8"%d%%", g_optimize_progress);
	
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_spacer(ctx);
	
	{
		nk_size size = (nk_size)g_optimize_progress;
		if (size > 100) size = 100;
		ctx->style.progress.cursor_normal = nk_style_item_color(g_color_good);
		ctx->style.progress.cursor_hover = nk_style_item_color(g_color_good);
		ctx->style.progress.cursor_active = nk_style_item_color(g_color_good);
		nk_progress(ctx, &size, 100, 0);
	}
	
out:
	nk_end(ctx);
}

static void
draw_group_separator(struct nk_context* ctx)
{
	struct nk_window* win = ctx->current;
	if (!win) return;
	
	nk_layout_row(ctx, NK_DYNAMIC, 4, 1, (float[1]) { 1.0f });
	struct nk_rect bounds;
	if (!nk_widget(&bounds, ctx)) return;
	
	float y = bounds.y + bounds.h / 2;
	nk_stroke_line(&win->buffer, bounds.x, y, bounds.x + bounds.w, y, 1, g_color_separator);
}

static void
draw_group_background(struct nk_context* ctx, int group_idx)
{
	g_group_index = group_idx;
}

static const char* translate_health_status(const char* health)
{
	static char result[64];
	if (health == NULL || health[0] == '\0') {
		return u8"未知";
	}
	
	const char* cn_status = u8"未知";
	if (strstr(health, "Good") != NULL) {
		cn_status = u8"良好";
	} else if (strstr(health, "Caution") != NULL) {
		cn_status = u8"警告";
	} else if (strstr(health, "Bad") != NULL) {
		cn_status = u8"不良";
	}
	
	int percent = -1;
	if (sscanf_s(health, "%d%%", &percent) == 1 && percent >= 0 && percent <= 100) {
		_snprintf_s(result, sizeof(result), _TRUNCATE, "%d%% %s", percent, cn_status);
	} else {
		strcpy_s(result, sizeof(result), cn_status);
	}
	
	return result;
}

static const char* get_optimization_status()
{
	static char result[64] = {0};
	char history_path[MAX_PATH];
	char user_profile[MAX_PATH];
	FILE* fp = NULL;
	char buffer[1024];
	char* level_start = NULL;
	
	if (GetEnvironmentVariableA("USERPROFILE", user_profile, MAX_PATH) > 0) {
		_snprintf_s(history_path, MAX_PATH, _TRUNCATE, "%s\\herosys_data\\backup\\history.json", user_profile);
	} else {
		return u8"未优化";
	}
	
	if (fopen_s(&fp, history_path, "r") != 0) {
		return u8"未优化";
	}
	
	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		level_start = strstr(buffer, "\"Level\"");
		if (level_start != NULL) {
			level_start = strchr(level_start, ':');
			if (level_start != NULL) {
				level_start++;
				while (*level_start == ' ' || *level_start == '"') level_start++;
				
				result[0] = '\0';
				if (strncmp(level_start, "basic", 5) == 0) {
					strcpy_s(result, sizeof(result), u8"基础优化");
				} else if (strncmp(level_start, "deep", 4) == 0) {
					strcpy_s(result, sizeof(result), u8"深度优化");
				} else if (strncmp(level_start, "full", 4) == 0) {
					strcpy_s(result, sizeof(result), u8"完全优化");
				} else if (strncmp(level_start, "custom", 6) == 0 || strncmp(level_start, "menu", 4) == 0) {
					strcpy_s(result, sizeof(result), u8"自定义优化");
				} else {
					strcpy_s(result, sizeof(result), u8"已优化");
				}
			}
		}
	}
	fclose(fp);
	
	if (result[0] == '\0') {
		return u8"未优化";
	}
	return result;
}

static const char* translate_smart_header(const char* cell)
{
	if (strcmp(cell, "Time") == 0) return u8"时间";
	if (strcmp(cell, "Health") == 0) return u8"健康状态";
	
	if (strlen(cell) >= 2) {
		unsigned int id;
		if (sscanf_s(cell, "%02u", &id) == 1) {
			switch ((BYTE)id) {
				case 0x01: return u8"01 关键警告";
				case 0x02: return u8"02 温度";
				case 0x03: return u8"03 可用备用空间";
				case 0x04: return u8"04 可用备用空间阈值";
				case 0x05: return u8"05 使用百分比";
				case 0x06: return u8"06 读取错误率";
				case 0x07: return u8"07 写入错误率";
				case 0x08: return u8"08 读取重试次数";
				case 0x09: return u8"09 写入重试次数";
				case 0x0A: return u8"10 旋转重试计数";
				case 0x0B: return u8"11 重新校准重试计数";
				case 0x0C: return u8"12 通电周期计数";
				case 0x0D: return u8"13 不安全关机次数";
				case 0x0E: return u8"14 介质错误";
				case 0x0F: return u8"15 错误数量";
				case 0x10: return u8"16 数据完整性错误";
				case 0xAA: return u8"170 可用预留空间";
				case 0xAB: return u8"171 程序失败计数";
				case 0xAC: return u8"172 擦除失败计数";
				case 0xAD: return u8"173 平均擦除次数";
				case 0xAE: return u8"174 意外断电计数";
				case 0xAF: return u8"175 断电保护计数";
				case 0xB0: return u8"176 擦除错误计数";
				case 0xB1: return u8"177 磨损均衡计数";
				case 0xB2: return u8"178 充电计时器";
				case 0xB3: return u8"179 充电状态";
				case 0xB4: return u8"180 保留块数";
				case 0xB5: return u8"181 程序失败计数";
				case 0xB6: return u8"182 擦除失败计数";
				case 0xB7: return u8"183 SATA降级计数";
				case 0xB8: return u8"184 端对端错误";
				case 0xB9: return u8"185 校准重试计数";
				case 0xBA: return u8"186 磁头飞行时间";
				case 0xBB: return u8"187 不可纠正错误";
				case 0xBC: return u8"188 命令超时";
				case 0xBD: return u8"189 高优先级写入";
				case 0xBE: return u8"190 温度";
				case 0xBF: return u8"191 重力感应错误";
				case 0xC0: return u8"192 断电保护计数";
				case 0xC1: return u8"193 磁头加载周期";
				case 0xC2: return u8"194 温度";
				case 0xC3: return u8"195 ECC校验错误";
				case 0xC4: return u8"196 重新映射扇区计数";
				case 0xC5: return u8"197 待映射扇区计数";
				case 0xC6: return u8"198 脱机无法校正扇区";
				case 0xC7: return u8"199 CRC错误计数";
				case 0xC8: return u8"200 写入错误计数";
				case 0xC9: return u8"201 软读取错误率";
				case 0xCA: return u8"202 数据地址标记错误";
				case 0xCB: return u8"203 运行错误校准";
				case 0xCC: return u8"204 软读取错误率";
				case 0xCD: return u8"205 报告不可纠正错误";
				case 0xCE: return u8"206 温度";
				case 0xCF: return u8"207 传输错误率";
				case 0xE1: return u8"225 总写入量";
				case 0xE2: return u8"226 总读取量";
				case 0xE7: return u8"231 温度/寿命";
				case 0xE8: return u8"232 端对端错误";
				case 0xE9: return u8"233 NAND写入量";
				case 0xEA: return u8"234 NAND读取量";
				case 0xF0: return u8"240 磁头飞行时间";
				case 0xF1: return u8"241 总写入量";
				case 0xF2: return u8"242 总读取量";
			}
		}
	}
	return cell;
}

static inline nk_bool
quick_access_button(struct nk_context* ctx, struct nk_image img, const char* str)
{
	if (g_ctx.main_flag & MAIN_NO_QUICK)
		return nk_button_image_hover(ctx, img, str);
	nk_spacer(ctx);
	return nk_false;
}

static VOID
draw_os(struct nk_context* ctx)
{
	LPCSTR saved_os = gnwinfo_hw_compare_get_string("System", "OS");
	LPCSTR saved_arch = gnwinfo_hw_compare_get_string("System", "Processor Architecture");
	LPCSTR saved_edition = gnwinfo_hw_compare_get_string("System", "Edition");
	LPCSTR saved_build = gnwinfo_hw_compare_get_string("System", "Build Number");
	LPCSTR saved_timestamp = gnwinfo_hw_compare_get_string("ConfigInfo", "Timestamp");
	
	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2, g_ctx.gui_ratio });
	nk_lhsc(ctx, u8"配置信息", NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
	
	if (gnwinfo_hw_compare_available() && saved_timestamp && saved_timestamp[0] != '\0')
	{
		char saved_label[128];
		_snprintf_s(saved_label, sizeof(saved_label), _TRUNCATE, u8"上次配置 - %s", saved_timestamp);
		nk_lhc(ctx, saved_label, NK_TEXT_LEFT, g_color_text_d);
	}
	else
	{
		nk_lhc(ctx, u8"上次配置 - 无", NK_TEXT_LEFT, g_color_text_d);
	}
	nk_lhc(ctx, u8"当前配置", NK_TEXT_LEFT, g_color_text_l);
	nk_spacing(ctx, 1);
	
	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_OS), N_(N__OS), NK_TEXT_LEFT, g_color_text_d);
	
	int len = snprintf(m_buf, MAX_PATH, "%s %s",
		NWL_NodeAttrGet(g_ctx.system, "OS"),
		NWL_NodeAttrGet(g_ctx.system, "Processor Architecture"));
	if (g_ctx.main_flag & MAIN_OS_EDITIONID)
	{
		LPCSTR edition = NWL_NodeAttrGet(g_ctx.system, "Edition");
		if (edition[0] != '-' && len >= 0 && len < MAX_PATH)
			len += snprintf(m_buf + len, MAX_PATH - len, " %s", edition);
	}
	if ((g_ctx.main_flag & MAIN_OS_BUILD) && len >= 0 && len < MAX_PATH)
		snprintf(m_buf + len, MAX_PATH - len, " (%s)", NWL_NodeAttrGet(g_ctx.system, "Build Number"));
	
	if (gnwinfo_hw_compare_available())
	{
		char saved_buf[MAX_PATH] = {0};
		if (saved_os && saved_os[0] != '\0' && saved_os[0] != '-')
		{
			strcpy_s(saved_buf, MAX_PATH, saved_os);
			if (saved_arch && saved_arch[0] != '\0' && saved_arch[0] != '-')
				snprintf(saved_buf + strlen(saved_buf), MAX_PATH - strlen(saved_buf), " %s", saved_arch);
			if (g_ctx.main_flag & MAIN_OS_EDITIONID)
			{
				if (saved_edition && saved_edition[0] != '\0' && saved_edition[0] != '-')
					snprintf(saved_buf + strlen(saved_buf), MAX_PATH - strlen(saved_buf), " %s", saved_edition);
			}
			if ((g_ctx.main_flag & MAIN_OS_BUILD) && saved_build && saved_build[0] != '\0' && saved_build[0] != '-')
				snprintf(saved_buf + strlen(saved_buf), MAX_PATH - strlen(saved_buf), " (%s)", saved_build);
		}
		
		if (gnwinfo_hw_compare_is_different(m_buf, saved_buf))
			nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
		else
			nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_text_d);
	}
	else
	{
		nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_d);
	}
	
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
	
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_INFO), NULL))
		ShellExecuteW(GetDesktopWindow(), NULL,
			L"::{26EE0668-A00A-44D7-9371-BEB064C98683}\\5\\::{BB06C0E4-D293-4F75-8A90-CB05B6477EEE}",
			NULL, NULL, SW_NORMAL);

	if (g_ctx.main_flag & MAIN_OS_DETAIL)
	{
		nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2, g_ctx.gui_ratio });
		nk_lhsc(ctx, N_(N__LOGIN), NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
		
		LPCSTR saved_username = gnwinfo_hw_compare_get_string("System", "Username");
		LPCSTR saved_hostname = gnwinfo_hw_compare_get_string("System", "DNS Hostname");
		nk_bool saved_safe_mode = gnwinfo_hw_compare_get_bool("System", "Safe Mode");
		nk_bool saved_bitlocker = gnwinfo_hw_compare_get_bool("System", "BitLocker Boot");
		nk_bool saved_vhd = gnwinfo_hw_compare_get_bool("System", "VHD Boot");
		nk_bool saved_fast_startup = gnwinfo_hw_compare_get_bool("System", "Fast Startup");
		
		char current_login[MAX_PATH] = {0};
		snprintf(current_login, MAX_PATH, "%s@%s%s%s%s%s",
			NWL_NodeAttrGet(g_ctx.system, "Username"),
			g_ctx.sys_hostname,
			strcmp(NWL_NodeAttrGet(g_ctx.system, "Safe Mode"), NA_BOOL_TRUE) == 0 ? " SafeMode" : "",
			strcmp(NWL_NodeAttrGet(g_ctx.system, "BitLocker Boot"), NA_BOOL_TRUE) == 0 ? " BitLocker" : "",
			strcmp(NWL_NodeAttrGet(g_ctx.system, "VHD Boot"), NA_BOOL_TRUE) == 0 ? " VHD" : "",
			strcmp(NWL_NodeAttrGet(g_ctx.system, "Fast Startup"), NA_BOOL_TRUE) == 0 ? " FastStartup" : "");
		
		if (gnwinfo_hw_compare_available() && saved_username && saved_hostname)
		{
			char saved_login_buf[MAX_PATH] = {0};
			snprintf(saved_login_buf, MAX_PATH, "%s@%s%s%s%s%s",
				saved_username,
				saved_hostname,
				saved_safe_mode ? " SafeMode" : "",
				saved_bitlocker ? " BitLocker" : "",
				saved_vhd ? " VHD" : "",
				saved_fast_startup ? " FastStartup" : "");
			
			if (gnwinfo_hw_compare_is_different(current_login, saved_login_buf))
				nk_lhc(ctx, saved_login_buf, NK_TEXT_LEFT, g_color_warning);
			else
				nk_lhc(ctx, saved_login_buf, NK_TEXT_LEFT, g_color_text_d);
		}
		else
		{
			nk_lhc(ctx, current_login, NK_TEXT_LEFT, g_color_text_d);
		}
		
		nk_lhcf(ctx, NK_TEXT_LEFT,
			g_color_text_l,
			"%s@%s%s%s%s%s",
			NWL_NodeAttrGet(g_ctx.system, "Username"),
			g_ctx.sys_hostname,
			strcmp(NWL_NodeAttrGet(g_ctx.system, "Safe Mode"), NA_BOOL_TRUE) == 0 ? " SafeMode" : "",
			strcmp(NWL_NodeAttrGet(g_ctx.system, "BitLocker Boot"), NA_BOOL_TRUE) == 0 ? " BitLocker" : "",
			strcmp(NWL_NodeAttrGet(g_ctx.system, "VHD Boot"), NA_BOOL_TRUE) == 0 ? " VHD" : "",
			strcmp(NWL_NodeAttrGet(g_ctx.system, "Fast Startup"), NA_BOOL_TRUE) == 0 ? " FastStartup" : "");
		
		// if (quick_access_button(ctx, GET_PNG(IDR_PNG_EDIT), N_(N__HOSTNAME)))
		// 	gnwinfo_init_hostname_window(ctx);
	}

	if (g_ctx.main_flag & MAIN_OS_UPTIME)
	{
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
		nk_lhsc(ctx, N_(N__UPTIME), NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
		nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
		nk_lhc(ctx, g_ctx.sys_uptime, NK_TEXT_LEFT, g_color_text_l);
	}

	if (g_ctx.system)
	{
		LPCSTR activation_status = NWL_NodeAttrGet(g_ctx.system, "Activation Status");
		LPCSTR activation_method = NWL_NodeAttrGet(g_ctx.system, "Activation Method");
		if (activation_status && activation_status[0] != '\0' && activation_status[0] != '-')
		{
			nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
			nk_lhsc(ctx, u8"激活状态", NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
			
			LPCSTR saved_status = gnwinfo_hw_compare_get_string("System", "Activation Status");
			if (gnwinfo_hw_compare_available() && saved_status)
			{
				if (gnwinfo_hw_compare_is_different(activation_status, saved_status))
					nk_lhc(ctx, saved_status, NK_TEXT_LEFT, g_color_warning);
				else
					nk_lhc(ctx, saved_status, NK_TEXT_LEFT, g_color_text_d);
			}
			else
			{
				nk_lhc(ctx, activation_status, NK_TEXT_LEFT, g_color_text_d);
			}
			
			nk_lhc(ctx, activation_status, NK_TEXT_LEFT, g_color_text_l);

			if (activation_method && activation_method[0] != '\0' && activation_method[0] != '-')
			{
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
				nk_lhsc(ctx, u8"激活方式", NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
				
				LPCSTR saved_method = gnwinfo_hw_compare_get_string("System", "Activation Method");
				if (gnwinfo_hw_compare_available() && saved_method)
				{
					if (gnwinfo_hw_compare_is_different(activation_method, saved_method))
						nk_lhc(ctx, saved_method, NK_TEXT_LEFT, g_color_warning);
					else
						nk_lhc(ctx, saved_method, NK_TEXT_LEFT, g_color_text_d);
				}
				else
				{
					nk_lhc(ctx, activation_method, NK_TEXT_LEFT, g_color_text_d);
				}
				
				nk_lhc(ctx, activation_method, NK_TEXT_LEFT, g_color_text_l);
			}

			LPCSTR kms_server = NWL_NodeAttrGet(g_ctx.system, "KMS Server");
			if (kms_server && kms_server[0] != '\0' && kms_server[0] != '-')
			{
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.3f, 0.35f, 0.35f });
				nk_lhsc(ctx, u8"KMS服务器", NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
				
				LPCSTR saved_kms = gnwinfo_hw_compare_get_string("System", "KMS Server");
				if (gnwinfo_hw_compare_available() && saved_kms)
				{
					if (gnwinfo_hw_compare_is_different(kms_server, saved_kms))
						nk_lhc(ctx, saved_kms, NK_TEXT_LEFT, g_color_warning);
					else
						nk_lhc(ctx, saved_kms, NK_TEXT_LEFT, g_color_text_d);
				}
				else
				{
					nk_lhc(ctx, kms_server, NK_TEXT_LEFT, g_color_text_d);
				}
				
				nk_lhc(ctx, kms_server, NK_TEXT_LEFT, g_color_text_l);
			}
		}
	}
}

static VOID
draw_bios(struct nk_context* ctx)
{
	LPCSTR tpm = g_ctx.system ? NWL_NodeAttrGet(g_ctx.system, "TPM") : "-";
	LPCSTR sb = g_ctx.uefi ? NWL_NodeAttrGet(g_ctx.uefi, "Secure Boot") : "-";

	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_FIRMWARE), N_(N__BIOS), NK_TEXT_LEFT, g_color_text_d);

	int len = snprintf(m_buf, MAX_PATH, "%s", g_ctx.system ? NWL_NodeAttrGet(g_ctx.system, "Firmware") : "Unknown");
	if (sb[0] == 'E' && len >= 0 && len < MAX_PATH)
		len += snprintf(m_buf + len, MAX_PATH - len, " %s", N_(N__SB));
	else if (sb[0] == 'D' && len >= 0 && len < MAX_PATH)
		len += snprintf(m_buf + len, MAX_PATH - len, " %s", N_(N__SB_OFF));

	if (tpm[0] == 'v' && len >= 0 && len < MAX_PATH)
		snprintf(m_buf + len, MAX_PATH - len, " TPM%s", tpm);

	char saved_firmware_buf[MAX_PATH] = {0};
	LPCSTR saved_firmware = gnwinfo_hw_compare_get_string("System", "Firmware");
	LPCSTR saved_sb = gnwinfo_hw_compare_get_string("UEFI", "Secure Boot");
	LPCSTR saved_tpm = gnwinfo_hw_compare_get_string("System", "TPM");
	
	if (gnwinfo_hw_compare_available() && saved_firmware)
	{
		int saved_len = snprintf(saved_firmware_buf, MAX_PATH, "%s", saved_firmware);
		if (saved_sb && saved_sb[0] == 'E' && saved_len >= 0 && saved_len < MAX_PATH)
			saved_len += snprintf(saved_firmware_buf + saved_len, MAX_PATH - saved_len, " %s", N_(N__SB));
		else if (saved_sb && saved_sb[0] == 'D' && saved_len >= 0 && saved_len < MAX_PATH)
			saved_len += snprintf(saved_firmware_buf + saved_len, MAX_PATH - saved_len, " %s", N_(N__SB_OFF));
		if (saved_tpm && saved_tpm[0] == 'v' && saved_len >= 0 && saved_len < MAX_PATH)
			snprintf(saved_firmware_buf + saved_len, MAX_PATH - saved_len, " TPM%s", saved_tpm);
		
		if (gnwinfo_hw_compare_is_different(m_buf, saved_firmware_buf))
			nk_lhc(ctx, saved_firmware_buf, NK_TEXT_LEFT, g_color_warning);
		else
			nk_lhc(ctx, saved_firmware_buf, NK_TEXT_LEFT, g_color_text_d);
	}
	else
	{
		nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_d);
	}

	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);

	// if (quick_access_button(ctx, GET_PNG(IDR_PNG_DMI), "SMBIOS"))
	// 	g_ctx.window_flag |= GUI_WINDOW_DMI;

	if (g_ctx.main_flag & MAIN_B_VENDOR)
	{
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
		nk_lhsc(ctx, N_(N__VENDOR), NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
		
		LPCSTR current_vendor = gnwinfo_get_smbios_attr("0", "Vendor", NULL, NULL);
		LPCSTR saved_vendor = gnwinfo_hw_compare_get_smbios_attr(0, "Vendor");
		if (gnwinfo_hw_compare_available() && saved_vendor)
		{
			if (gnwinfo_hw_compare_is_different(current_vendor, saved_vendor))
				nk_lhc(ctx, saved_vendor, NK_TEXT_LEFT, g_color_warning);
			else
				nk_lhc(ctx, saved_vendor, NK_TEXT_LEFT, g_color_text_d);
		}
		else
		{
			nk_lhc(ctx, current_vendor, NK_TEXT_LEFT, g_color_text_d);
		}
		nk_lhc(ctx, current_vendor, NK_TEXT_LEFT, g_color_text_l);
	}
	if (g_ctx.main_flag & MAIN_B_VERSION)
	{
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) {0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
		nk_lhsc(ctx, N_(N__VERSION), NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
		
		char current_ver[MAX_PATH] = {0};
		snprintf(current_ver, MAX_PATH, "%s %s",
			gnwinfo_get_smbios_attr("0", "Version", NULL, NULL),
			gnwinfo_get_smbios_attr("0", "Release Date", NULL, NULL));
		
		LPCSTR saved_ver = gnwinfo_hw_compare_get_smbios_attr(0, "Version");
		LPCSTR saved_date = gnwinfo_hw_compare_get_smbios_attr(0, "Release Date");
		char saved_ver_buf[MAX_PATH] = {0};
		if (saved_ver)
		{
			if (saved_date)
				snprintf(saved_ver_buf, MAX_PATH, "%s %s", saved_ver, saved_date);
			else
				snprintf(saved_ver_buf, MAX_PATH, "%s", saved_ver);
		}
		
		if (gnwinfo_hw_compare_available() && saved_ver)
		{
			if (gnwinfo_hw_compare_is_different(current_ver, saved_ver_buf))
				nk_lhc(ctx, saved_ver_buf, NK_TEXT_LEFT, g_color_warning);
			else
				nk_lhc(ctx, saved_ver_buf, NK_TEXT_LEFT, g_color_text_d);
		}
		else
		{
			nk_lhc(ctx, current_ver, NK_TEXT_LEFT, g_color_text_d);
		}
		nk_lhc(ctx, current_ver, NK_TEXT_LEFT, g_color_text_l);
	}
}

static VOID
draw_computer(struct nk_context* ctx)
{
	struct nk_color color = g_color_unknown;
	BOOL has_battery = TRUE;
	LPCSTR time = "";
	LPCSTR bat = NWL_NodeAttrGet(g_ctx.battery, "Battery Status");
	LPCSTR ac = "";

	nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 1.0f - g_ctx.gui_ratio, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_PC), N_(N__PC), NK_TEXT_LEFT, g_color_text_d);
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_PCI), "PCI"))
		g_ctx.window_flag |= GUI_WINDOW_PCI;

	nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2});
	nk_lhsc(ctx, gnwinfo_get_smbios_attr("1", "Manufacturer", NULL, NULL), NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);

	char current_pc[MAX_PATH] = {0};
	snprintf(current_pc, MAX_PATH, "%s %s %s",
		gnwinfo_get_smbios_attr("1", "Product Name", NULL, NULL),
		gnwinfo_get_smbios_attr("3", "Type", NULL, NULL),
		gnwinfo_get_smbios_attr("1", "Serial Number", NULL, NULL));

	LPCSTR saved_product = gnwinfo_hw_compare_get_smbios_attr(1, "Product Name");
	LPCSTR saved_type = gnwinfo_hw_compare_get_smbios_attr(3, "Type");
	LPCSTR saved_serial = gnwinfo_hw_compare_get_smbios_attr(1, "Serial Number");

	if (gnwinfo_hw_compare_available() && saved_product)
	{
		char saved_pc[MAX_PATH] = {0};
		snprintf(saved_pc, MAX_PATH, "%s %s %s",
			saved_product ? saved_product : "",
			saved_type ? saved_type : "",
			saved_serial ? saved_serial : "");

		if (gnwinfo_hw_compare_is_different(current_pc, saved_pc))
			nk_lhc(ctx, saved_pc, NK_TEXT_LEFT, g_color_warning);
		else
			nk_lhc(ctx, saved_pc, NK_TEXT_LEFT, g_color_text_d);
	}
	else
	{
		nk_lhc(ctx, current_pc, NK_TEXT_LEFT, g_color_text_d);
	}

	nk_lhc(ctx, current_pc, NK_TEXT_LEFT, g_color_text_l);

	if (g_ctx.board)
	{
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
		nk_lhsc(ctx, NWL_NodeAttrGet(g_ctx.board, "Manufacturer"), NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);

		char current_board[MAX_PATH] = {0};
		snprintf(current_board, MAX_PATH, "%s %s",
			NWL_NodeAttrGet(g_ctx.board, "Board Name"),
			NWL_NodeAttrGet(g_ctx.board, "Serial Number"));

		LPCSTR saved_board_name = gnwinfo_hw_compare_get_smbios_attr(2, "Product Name");
		LPCSTR saved_board_serial = gnwinfo_hw_compare_get_smbios_attr(2, "Serial Number");

		if (gnwinfo_hw_compare_available() && saved_board_name)
		{
			char saved_board_buf[MAX_PATH] = {0};
			snprintf(saved_board_buf, MAX_PATH, "%s %s",
				saved_board_name ? saved_board_name : "",
				saved_board_serial ? saved_board_serial : "");

			if (gnwinfo_hw_compare_is_different(current_board, saved_board_buf))
				nk_lhc(ctx, saved_board_buf, NK_TEXT_LEFT, g_color_warning);
			else
				nk_lhc(ctx, saved_board_buf, NK_TEXT_LEFT, g_color_text_d);
		}
		else
		{
			nk_lhc(ctx, current_board, NK_TEXT_LEFT, g_color_text_d);
		}

		nk_lhc(ctx, current_board, NK_TEXT_LEFT, g_color_text_l);
	}

	if (strcmp(bat, "Charging") == 0)
	{
		color = g_color_good;
		time = NWL_NodeAttrGet(g_ctx.battery, "Battery Life Full");
	}
	else if (strcmp(bat, "Not Charging") == 0)
	{
		color = g_color_warning;
		time = NWL_NodeAttrGet(g_ctx.battery, "Battery Life Remaining");
	}
	else
		has_battery = FALSE;

	if (strcmp(time, "UNKNOWN") == 0)
		time = "";

	if (strcmp(NWL_NodeAttrGet(g_ctx.battery, "AC Power"), "Online") == 0)
		ac = u8"AC ";

	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2, g_ctx.gui_ratio  });
	nk_lhsc(ctx, N_(N__POWER_STAT), NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
	nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
	int len = snprintf(m_buf, MAX_PATH, "%s %s",
		ac, NWL_NodeAttrGet(g_ctx.battery, "Active Power Scheme Name"));
	if (has_battery && len >= 0 && len < MAX_PATH)
		snprintf(m_buf + len, MAX_PATH - len, " %s %s",
			NWL_NodeAttrGet(g_ctx.battery, "Battery Life Percentage"),
			time);
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_BATTERY), N_(N__POWER_STAT)))
		ShellExecuteW(GetDesktopWindow(), NULL,
			L"shell:::{025A5937-A6BE-4686-A844-36FE4BEC8B6D}",
			NULL, NULL, SW_NORMAL);
}

static VOID
draw_processor(struct nk_context* ctx)
{
	nk_layout_row(ctx, NK_DYNAMIC, 0, 5, (float[5]) {  0.2f, 0.4f - g_ctx.gui_ratio/2, (0.4f - g_ctx.gui_ratio/2)/2,(0.4f - g_ctx.gui_ratio/2)/2, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_CPU), N_(N__CPU), NK_TEXT_LEFT, g_color_text_d);
	nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
	nk_lhcf(ctx, NK_TEXT_LEFT, gnwinfo_get_color(g_ctx.cpu_usage, 70.0, 90.0),
		"%.2f%% %lu MHz",
		g_ctx.cpu_usage,
		g_ctx.cpu_freq);
	gnwinfo_draw_percent_prog(ctx, g_ctx.cpu_usage);
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_CPUID), "CPUID"))
		g_ctx.window_flag |= GUI_WINDOW_CPUID;

	for (INT i = 0; i < g_ctx.cpu_count; i++)
	{
		PNODE cpu = NWL_NodeEnumChild(g_ctx.cpuid, i);
		LPCSTR brand = NWL_NodeAttrGet(cpu, "Brand");
		if (cpu == NULL)
			break;

		char cpu_node_name[32] = {0};
		snprintf(cpu_node_name, sizeof(cpu_node_name), "CPU%d-G", i);

		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2});
		nk_lhsc(ctx, cpu->name, NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);

		LPCSTR saved_brand = gnwinfo_hw_compare_get_nested_string("CPUID", cpu_node_name, "Brand");
		if (gnwinfo_hw_compare_available() && saved_brand)
		{
			if (gnwinfo_hw_compare_is_different(brand, saved_brand))
				nk_lhc(ctx, saved_brand, NK_TEXT_LEFT, g_color_warning);
			else
				nk_lhc(ctx, saved_brand, NK_TEXT_LEFT, g_color_text_d);
		}
		else
		{
			nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
		}

		nk_lhc(ctx, brand, NK_TEXT_LEFT, g_color_text_l);

		if (!(g_ctx.main_flag & MAIN_CPU_DETAIL))
			continue;

		nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - 0.05f,0.05f });
		nk_spacer(ctx);

		int len = snprintf(m_buf, MAX_PATH, "%s %s", NWL_NodeAttrGet(cpu, "Cores"), N_(N__CORES));
		if (len >= 0 && len < MAX_PATH)
			len += snprintf(m_buf + len, MAX_PATH - len, " %s %s",
				NWL_NodeAttrGet(cpu, "Logical CPUs"),
				N_(N__THREADS));

		LPCSTR saved_cores = gnwinfo_hw_compare_get_nested_string("CPUID", cpu_node_name, "Cores");
		LPCSTR saved_logical = gnwinfo_hw_compare_get_nested_string("CPUID", cpu_node_name, "Logical CPUs");
		if (gnwinfo_hw_compare_available() && saved_cores && saved_logical)
		{
			char saved_spec[MAX_PATH] = {0};
			snprintf(saved_spec, MAX_PATH, "%s %s %s %s",
				saved_cores, N_(N__CORES), saved_logical, N_(N__THREADS));
			if (gnwinfo_hw_compare_is_different(m_buf, saved_spec))
				nk_lhc(ctx, saved_spec, NK_TEXT_LEFT, g_color_warning);
			else
				nk_lhc(ctx, saved_spec, NK_TEXT_LEFT, g_color_text_d);
		}
		else
		{
			nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
		}

		nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
		if (g_ctx.cpu_info[i].MsrTemp > 0)
			nk_lhcf(ctx, NK_TEXT_LEFT,
				gnwinfo_get_color((double)g_ctx.cpu_info[i].MsrTemp, 65.0, 85.0),
				u8"%d"TEMP_CELSIUS_SYMBOL, g_ctx.cpu_info[i].MsrTemp);
		else
			nk_spacer(ctx);

		if (!(g_ctx.main_flag & MAIN_CPU_CACHE))
			continue;
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
		nk_spacer(ctx);

		PNODE cache = NWL_NodeGetChild(cpu, "Cache");
		LPCSTR l1 = NWL_NodeAttrGet(cache, "L1 Cache Size");
		LPCSTR l2 = NWL_NodeAttrGet(cache, "L2 Cache Size");
		LPCSTR l3 = NWL_NodeAttrGet(cache, "L3 Cache Size");
		LPCSTR l4 = NWL_NodeAttrGet(cache, "L4 Cache Size");

		len = snprintf(m_buf, MAX_PATH, "L1 %s", l1);
		if (l2[0] != '-' && len >= 0 && len < MAX_PATH)
			len += snprintf(m_buf + len, MAX_PATH - len, " L2 %s", l2);
		if (l3[0] != '-' && len >= 0 && len < MAX_PATH)
			len += snprintf(m_buf + len, MAX_PATH - len, " L3 %s", l3);
		if (l4[0] != '-' && len >= 0 && len < MAX_PATH)
			snprintf(m_buf + len, MAX_PATH - len, " L4 %s", l4);

		LPCSTR saved_l1 = gnwinfo_hw_compare_get_deep_nested_string("CPUID", cpu_node_name, "Cache", "L1 Cache Size");
		LPCSTR saved_l2 = gnwinfo_hw_compare_get_deep_nested_string("CPUID", cpu_node_name, "Cache", "L2 Cache Size");
		LPCSTR saved_l3 = gnwinfo_hw_compare_get_deep_nested_string("CPUID", cpu_node_name, "Cache", "L3 Cache Size");
		if (gnwinfo_hw_compare_available() && saved_l1)
		{
			char saved_cache[MAX_PATH] = {0};
			snprintf(saved_cache, MAX_PATH, "L1 %s", saved_l1);
			if (saved_l2 && strcmp(saved_l2, "-") != 0)
				snprintf(saved_cache + strlen(saved_cache), MAX_PATH - strlen(saved_cache), " L2 %s", saved_l2);
			if (saved_l3 && strcmp(saved_l3, "-") != 0)
				snprintf(saved_cache + strlen(saved_cache), MAX_PATH - strlen(saved_cache), " L3 %s", saved_l3);

			if (gnwinfo_hw_compare_is_different(m_buf, saved_cache))
				nk_lhc(ctx, saved_cache, NK_TEXT_LEFT, g_color_warning);
			else
				nk_lhc(ctx, saved_cache, NK_TEXT_LEFT, g_color_text_d);
		}
		else
		{
			nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
		}

		nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
	}
	
	// if (!g_cpu_test_loaded)
	// {
	// 	load_cpu_test_result();
	// 	g_cpu_test_loaded = 1;
	// }
	
	// nk_layout_row(ctx, NK_DYNAMIC, 35, 2, (float[2]) { 0.7f, 0.3f });
	// if (g_cpu_test_running)
	// {
	// 	nk_lhc(ctx, u8"测试中...", NK_TEXT_LEFT, g_color_warning);
	// }
	// else if (g_cpu_test_result == 0)
	// {
	// 	nk_lhcf(ctx, NK_TEXT_LEFT, g_color_good, u8"通过 (%s)", g_cpu_test_date);
	// }
	// else if (g_cpu_test_result > 0)
	// {
	// 	nk_lhcf(ctx, NK_TEXT_LEFT, g_color_warning, u8"错误 (%s)", g_cpu_test_date);
	// }
	// else
	// {
	// 	nk_lhc(ctx, u8"未测试", NK_TEXT_LEFT, g_color_text_l);
	// }

	// if (nk_button_label(ctx, u8"测试CPU"))
	// {
	// 	start_cpu_test();
	// }
}
static VOID
draw_processor_simple(struct nk_context* ctx)
{
	PNODE cpu = NWL_NodeEnumChild(g_ctx.cpuid, 0);
	LPCSTR cores = cpu ? NWL_NodeAttrGet(cpu, "Cores") : "-";
	LPCSTR threads = cpu ? NWL_NodeAttrGet(cpu, "Logical CPUs") : "-";
	
	nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.4f, 0.4f, 0.2f });
	nk_image_label(ctx, GET_PNG(IDR_PNG_CPU), u8"CPU 使用率", NK_TEXT_LEFT, g_color_text_d);
	nk_lhcf(ctx, NK_TEXT_LEFT, gnwinfo_get_color(g_ctx.cpu_usage, 70.0, 90.0),
		"%.1f%%", g_ctx.cpu_usage);
	nk_lhc(ctx, u8"● 正常", NK_TEXT_LEFT, g_color_good);
	
	nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f });
	gnwinfo_draw_percent_prog(ctx, g_ctx.cpu_usage);
	
	nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.5f, 0.5f });
	snprintf(m_buf, MAX_PATH, u8"核心数：%s 核 / %s 线程", cores ? cores : "-", threads ? threads : "-");
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
	snprintf(m_buf, MAX_PATH, u8"频率：%lu MHz", g_ctx.cpu_freq);
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
	
	if (!g_cpu_test_loaded)
	{
		load_cpu_test_result();
		g_cpu_test_loaded = 1;
	}
	
	nk_layout_row(ctx, NK_DYNAMIC, 35, 2, (float[2]) { 0.7f, 0.3f });
	if (g_cpu_test_running)
	{
		nk_lhc(ctx, u8"测试中...", NK_TEXT_LEFT, g_color_warning);
	}
	else if (g_cpu_test_result == 0)
	{
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_good, u8"通过 (%s)", g_cpu_test_date);
	}
	else if (g_cpu_test_result > 0)
	{
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_warning, u8"错误 (%s)", g_cpu_test_date);
	}
	else
	{
		nk_lhc(ctx, u8"未测试", NK_TEXT_LEFT, g_color_text_l);
	}

	if (nk_button_label(ctx, u8"测试CPU"))
	{
		start_cpu_test();
	}
}

static VOID
draw_mem_capacity(struct nk_context* ctx)
{
	nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio / 2, 0.4f - g_ctx.gui_ratio / 2 });
	nk_lhsc(ctx, N_(N__MAX_CAPACITY), NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);

	LPCSTR id = "16";
	LPCSTR capacity = gnwinfo_get_smbios_attr(id, "Max Capacity", NULL, NULL);
	LPCSTR saved_capacity = gnwinfo_hw_compare_get_smbios_attr(16, "Max Capacity");
	LPCSTR saved_slots = gnwinfo_hw_compare_get_smbios_attr(16, "Number of Slots");

	if (capacity[0] == '-')
	{
		id = "5";
		capacity = gnwinfo_get_smbios_attr(id, "Max Memory Module Size (MB)", NULL, NULL);
		saved_capacity = gnwinfo_hw_compare_get_smbios_attr(17, "Device Size");
		saved_slots = NULL;
	}

	if (gnwinfo_hw_compare_available() && saved_capacity)
	{
		char saved_buf[MAX_PATH] = {0};
		if (saved_slots)
		{
			snprintf(saved_buf, MAX_PATH, "%s %s %s%s",
				saved_slots, N_(N__SLOTS), saved_capacity, id[0] == '5' ? " MB" : "");
		}
		else
		{
			snprintf(saved_buf, MAX_PATH, "%s%s", saved_capacity, id[0] == '5' ? " MB" : "");
		}

		snprintf(m_buf, MAX_PATH, "%s %s %s%s",
			gnwinfo_get_smbios_attr(id, "Number of Slots", NULL, NULL),
			N_(N__SLOTS),
			capacity,
			id[0] == '5' ? " MB" : "");

		if (gnwinfo_hw_compare_is_different(m_buf, saved_buf))
			nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
		else
			nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_text_d);
	}
	else
	{
		nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
	}

	snprintf(m_buf, MAX_PATH, "%s %s %s%s",
		gnwinfo_get_smbios_attr(id, "Number of Slots", NULL, NULL),
		N_(N__SLOTS),
		capacity,
		id[0] == '5' ? " MB" : "");
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
}

static VOID
draw_mem_dmi(struct nk_context* ctx)
{
	nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2});
	INT count = NWL_NodeChildCount(g_ctx.smbios);

	int current_device_count = 0;
	for (INT i = 0; i < count; i++)
	{
		PNODE tab = NWL_NodeEnumChild(g_ctx.smbios, i);
		LPCSTR attr = NWL_NodeAttrGet(tab, "Table Type");
		if (strcmp(attr, "17") != 0)
			continue;
		LPCSTR ddr = NWL_NodeAttrGet(tab, "Device Type");
		if (ddr[0] == '-')
			continue;
		current_device_count++;
	}

	int saved_device_count = gnwinfo_hw_compare_get_smbios_count(17);

	int device_index = 0;
	for (INT i = 0; i < count; i++)
	{
		PNODE tab = NWL_NodeEnumChild(g_ctx.smbios, i);
		LPCSTR attr = NWL_NodeAttrGet(tab, "Table Type");
		if (strcmp(attr, "17") != 0)
			continue;
		LPCSTR ddr = NWL_NodeAttrGet(tab, "Device Type");
		if (ddr[0] == '-')
			continue;

		LPCSTR current_serial = NWL_NodeAttrGet(tab, "Serial Number");
		LPCSTR saved_ddr = gnwinfo_hw_compare_get_smbios_attr_by_serial(17, current_serial, "Device Type");
		LPCSTR saved_speed = gnwinfo_hw_compare_get_smbios_attr_by_serial(17, current_serial, "Speed (MT/s)");
		LPCSTR saved_size = gnwinfo_hw_compare_get_smbios_attr_by_serial(17, current_serial, "Device Size");
		LPCSTR saved_manuf = gnwinfo_hw_compare_get_smbios_attr_by_serial(17, current_serial, "Manufacturer");
		LPCSTR saved_serial = gnwinfo_hw_compare_get_smbios_attr_by_serial(17, current_serial, "Serial Number");

		nk_bool is_new = gnwinfo_hw_compare_available() && !gnwinfo_hw_compare_smbios_serial_exists(17, current_serial);

		if (is_new)
		{
			if(g_hw_has_diff == nk_false)
			g_hw_has_diff = nk_true;
			nk_lhc(ctx, u8"新增", NK_TEXT_LEFT, g_color_warning);
			nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_warning);
		}
		else
		{
			nk_lhsc(ctx, NWL_NodeAttrGet(tab, "Bank Locator"), NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);

			if (gnwinfo_hw_compare_available() && saved_ddr)
			{
				char saved_buf[MAX_PATH] = {0};
				snprintf(saved_buf, MAX_PATH, "%s-%s %s %s %s",
					saved_ddr,
					saved_speed ? saved_speed : NWL_NodeAttrGet(tab, "Speed (MT/s)"),
					saved_size,
					saved_manuf,
					saved_serial);

				char current_buf[MAX_PATH] = {0};
				snprintf(current_buf, MAX_PATH, "%s-%s %s %s %s",
					ddr,
					NWL_NodeAttrGet(tab, "Speed (MT/s)"),
					NWL_NodeAttrGet(tab, "Device Size"),
					NWL_NodeAttrGet(tab, "Manufacturer"),
					NWL_NodeAttrGet(tab, "Serial Number"));

				if (gnwinfo_hw_compare_is_different(current_buf, saved_buf))
					nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
				else
					nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_text_d);
			}
			else
			{
				nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
			}
		}

		if (is_new)
		{
			nk_lhcf(ctx, NK_TEXT_LEFT, g_color_warning,
				"%s-%s %s %s %s",
				ddr,
				NWL_NodeAttrGet(tab, "Speed (MT/s)"),
				NWL_NodeAttrGet(tab, "Device Size"),
				NWL_NodeAttrGet(tab, "Manufacturer"),
				NWL_NodeAttrGet(tab, "Serial Number"));
		}
		else
		{
			nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l,
				"%s-%s %s %s %s",
				ddr,
				NWL_NodeAttrGet(tab, "Speed (MT/s)"),
				NWL_NodeAttrGet(tab, "Device Size"),
				NWL_NodeAttrGet(tab, "Manufacturer"),
				NWL_NodeAttrGet(tab, "Serial Number"));
		}
		
		device_index++;
	}

	if (gnwinfo_hw_compare_available())
	{
		for (int i = 0; i < saved_device_count; i++)
		{
			LPCSTR saved_serial = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Serial Number");
			if (!saved_serial || saved_serial[0] == '\0' || saved_serial[0] == '-')
				continue;

			nk_bool found_in_current = nk_false;
			for (INT j = 0; j < count && !found_in_current; j++)
			{
				PNODE tab = NWL_NodeEnumChild(g_ctx.smbios, j);
				LPCSTR attr = NWL_NodeAttrGet(tab, "Table Type");
				if (strcmp(attr, "17") != 0)
					continue;
				LPCSTR ddr = NWL_NodeAttrGet(tab, "Device Type");
				if (ddr[0] == '-')
					continue;
				LPCSTR current_serial = NWL_NodeAttrGet(tab, "Serial Number");
				if (current_serial && strcmp(current_serial, saved_serial) == 0)
				{
					found_in_current = nk_true;
				}
			}

			if (found_in_current)
				continue;

			LPCSTR saved_ddr = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Device Type");
			LPCSTR saved_speed = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Speed (MT/s)");
			LPCSTR saved_size = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Device Size");
			LPCSTR saved_manuf = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Manufacturer");

			nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2});
			nk_lhc(ctx, u8"已移除", NK_TEXT_LEFT, g_color_warning);

			char saved_buf[MAX_PATH] = {0};
			snprintf(saved_buf, MAX_PATH, "%s-%s %s %s %s",
				saved_ddr,
				saved_speed ? saved_speed : "-",
				saved_size,
				saved_manuf,
				saved_serial);

			nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
			nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_warning);
		}
	}
}

static VOID
draw_mem_spd(struct nk_context* ctx)
{
	INT count = NWL_NodeChildCount(g_ctx.spd);
	if (count <= 0)
	{
		draw_mem_dmi(ctx);
		return;
	}
	for (INT i = 0; i < count; i++)
	{
		PNODE tab = NWL_NodeEnumChild(g_ctx.spd, i);
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2});
		nk_lhscf(ctx, NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true, "BANK %s", NWL_NodeAttrGet(tab, "ID"));
		nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l,
			"%s-%s %s %s %s",
			NWL_NodeAttrGet(tab, "Memory Type"),
			NWL_NodeAttrGet(tab, "Speed (MHz)"),
			NWL_NodeAttrGet(tab, "Capacity"),
			NWL_NodeAttrGet(tab, "Manufacturer"),
			NWL_NodeAttrGet(tab, "Serial Number"));
		nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2, g_ctx.gui_ratio });
		nk_spacer(ctx);
		nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l,
			"%s CL%s-%s-%s-%s",
			NWL_NodeAttrGet(tab, "Module Type"),
			NWL_NodeAttrGet(tab, "tCL"),
			NWL_NodeAttrGet(tab, "tRCD"),
			NWL_NodeAttrGet(tab, "tRP"),
			NWL_NodeAttrGet(tab, "tRAS"));
		double temp = g_ctx.mem_sensors.Sensor[i].Temp;
		if (temp > 0.0)
			nk_lhcf(ctx, NK_TEXT_LEFT, gnwinfo_get_color(temp, 55.0, 85.0), u8"%.1f"TEMP_CELSIUS_SYMBOL, temp);
		else
			nk_spacer(ctx);
	}
}

static VOID
draw_memory(struct nk_context* ctx)
{
	nk_layout_row(ctx, NK_DYNAMIC, 0, 5, (float[5]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, (0.4f - g_ctx.gui_ratio/2)/2 , (0.4f - g_ctx.gui_ratio / 2) / 2, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_MEMORY), N_(N__MEMORY), NK_TEXT_LEFT, g_color_text_d);
	nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
	nk_lhcf(ctx, NK_TEXT_LEFT,
		gnwinfo_get_color((double)g_ctx.mem_status.PhysUsage, 70.0, 90.0),
		"%lu%% %s / %s",
		g_ctx.mem_status.PhysUsage, g_ctx.mem_status.StrPhysAvail, g_ctx.mem_status.StrPhysTotal);
	gnwinfo_draw_percent_prog(ctx, (double)g_ctx.mem_status.PhysUsage);
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_ROCKET), N_(N__CLEAN_MEMORY)))
		gnwinfo_init_mm_window(ctx);

	if (g_ctx.main_flag & MAIN_MEM_DETAIL)
	{
		draw_mem_capacity(ctx);
		if (g_ctx.spd)
			draw_mem_spd(ctx);
		else
			draw_mem_dmi(ctx);
	}
}
static VOID
draw_memory_simple(struct nk_context* ctx)
{
	if (!g_mem_test_loaded)
	{
		load_mem_test_result();
		g_mem_test_loaded = 1;
	}
	
	nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.4f, 0.4f, 0.2f });
	nk_image_label(ctx, GET_PNG(IDR_PNG_MEMORY), u8"内存使用率", NK_TEXT_LEFT, g_color_text_d);
	nk_lhcf(ctx, NK_TEXT_LEFT,
		gnwinfo_get_color((double)g_ctx.mem_status.PhysUsage, 70.0, 90.0),
		"%lu%%", g_ctx.mem_status.PhysUsage);
	nk_lhc(ctx, u8"● 正常", NK_TEXT_LEFT, g_color_good);
		
	nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f });
	gnwinfo_draw_percent_prog(ctx, (double)g_ctx.mem_status.PhysUsage);
	
	nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.5f, 0.5f });
	snprintf(m_buf, MAX_PATH, u8"剩余：%s", g_ctx.mem_status.StrPhysAvail);
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
	snprintf(m_buf, MAX_PATH, u8"总量：%s", g_ctx.mem_status.StrPhysTotal);
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);

	nk_layout_row(ctx, NK_DYNAMIC, 35, 2, (float[2]) { 0.7f, 0.3f });
	if (g_mem_test_running)
	{
		nk_lhc(ctx, u8"测试中...", NK_TEXT_LEFT, g_color_warning);
	}
	else if (g_mem_test_result == 0)
	{
		snprintf(m_buf, MAX_PATH, u8"通过 (%lldMB) (%s)", g_mem_test_size / (1024 * 1024), g_mem_test_time);
		for (char* p = m_buf; *p; p++) { if (*p == '_') *p = ' '; }
		nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_good);
	}
	else if (g_mem_test_result > 0)
	{
		snprintf(m_buf, MAX_PATH, u8"错误: %d (%lldMB) (%s)", g_mem_test_result, g_mem_test_size / (1024 * 1024), g_mem_test_time);
		for (char* p = m_buf; *p; p++) { if (*p == '_') *p = ' '; }
		nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_warning);
	}
	else
	{
		nk_lhc(ctx, u8"未测试", NK_TEXT_LEFT, g_color_text_l);
	}

	if (nk_button_label(ctx, u8"测试内存"))
	{
		start_memory_test();
	}
}
static VOID
draw_display(struct nk_context* ctx)
{
	INT i;

	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_DISPLAY), N_(N__DISPLAY), NK_TEXT_LEFT, g_color_text_d);

	LPCSTR saved_res = gnwinfo_hw_compare_get_string("DisplaySettings", "Resolution");
	LPCSTR saved_dpi = gnwinfo_hw_compare_get_string("DisplaySettings", "DPI");
	LPCSTR saved_scale = gnwinfo_hw_compare_get_string("DisplaySettings", "Scale");

	nk_bool is_different = nk_false;
	if (gnwinfo_hw_compare_available() && saved_res)
	{
		char current_res[64] = {0};
		snprintf(current_res, sizeof(current_res), "%ldx%ld", g_ctx.cur_display.Width, g_ctx.cur_display.Height);

		char saved_buf[128] = {0};
		if (saved_dpi && saved_scale)
			snprintf(saved_buf, sizeof(saved_buf), "%s %sDPI (%s%%)", saved_res, saved_dpi, saved_scale);
		else
			snprintf(saved_buf, sizeof(saved_buf), "%s", saved_res);

		is_different = gnwinfo_hw_compare_is_different(current_res, saved_res);

		if (saved_dpi)
		{
			char current_dpi[16] = {0};
			snprintf(current_dpi, sizeof(current_dpi), "%u", g_ctx.cur_display.Dpi);
			if (gnwinfo_hw_compare_is_different(current_dpi, saved_dpi))
				is_different = nk_true;
		}

		if (saved_scale)
		{
			char current_scale[16] = {0};
			snprintf(current_scale, sizeof(current_scale), "%u", g_ctx.cur_display.Scale);
			if (gnwinfo_hw_compare_is_different(current_scale, saved_scale))
				is_different = nk_true;
		}

		if (is_different)
			nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
		else
			nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_text_d);
	}
	else
	{
		nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
	}

	if (is_different)
	{
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_warning,
			"%ldx%ld %u DPI (%u%%)",
			g_ctx.cur_display.Width, g_ctx.cur_display.Height, g_ctx.cur_display.Dpi, g_ctx.cur_display.Scale);
	}
	else
	{
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l,
			"%ldx%ld %u DPI (%u%%)",
			g_ctx.cur_display.Width, g_ctx.cur_display.Height, g_ctx.cur_display.Dpi, g_ctx.cur_display.Scale);
	}

	if (quick_access_button(ctx, GET_PNG(IDR_PNG_MONITOR), N_(N__DISPLAY)))
		g_ctx.window_flag |= GUI_WINDOW_DISPLAY;

	if (g_ctx.lib.NwGpu)
	{
		if (g_ctx.lib.NwGpu->DeviceCount > 0)
		{
			for (i = 0; i < (INT)g_ctx.lib.NwGpu->DeviceCount; i++)
			{
				NWLIB_GPU_DEV* gpu = &g_ctx.lib.NwGpu->Device[i];
				CHAR name[32];
				snprintf(name, sizeof(name), "GPU%d", i);
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2});

				nk_lhsc(ctx, name, NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
				nk_lhc(ctx, gpu->Name, NK_TEXT_LEFT, g_color_text_d);
				nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l, "%s (%u%%)",
					NWL_GetHumanSize(gpu->TotalMemory, NWLC->NwUnits, 1024), (unsigned)gpu->MemoryPercent);

				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
				nk_spacer(ctx);
				nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l,
					u8"%.1f%% %.1fW %.1fMHz %.1fV %lluRPM",
					gpu->UsagePercent, gpu->Power, gpu->Frequency, gpu->Voltage, gpu->FanSpeed);
				nk_lhcf(ctx, NK_TEXT_LEFT,
					gnwinfo_get_color(gpu->Temperature, 50.0, 85.0),
					u8"%.1f"TEMP_CELSIUS_SYMBOL, gpu->Temperature);
			}
		}
		else
		{
			INT count = NWL_NodeChildCount(g_ctx.lib.NwGpu->PciList);
			for (i = 0; i < count; i++)
			{
				PNODE gpu = NWL_NodeEnumChild(g_ctx.lib.NwGpu->PciList, i);
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
				nk_lhsc(ctx, NWL_NodeAttrGet(gpu, "Vendor"), NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);
				nk_lhc(ctx, NWL_NodeAttrGet(gpu, "Device"), NK_TEXT_LEFT, g_color_text_d);
				nk_lhc(ctx, NWL_NodeAttrGet(gpu, "Device"), NK_TEXT_LEFT, g_color_text_l);
			}
		}
	}

	INT count = NWL_NodeChildCount(g_ctx.edid);
	int saved_display_count = gnwinfo_hw_compare_get_array_size("Display", NULL);
	int current_display_count = 0;
	for (i = 0; i < count; i++)
	{
		PNODE mon = NWL_NodeEnumChild(g_ctx.edid, i);
		LPCSTR id = NWL_NodeAttrGet(mon, "ID");
		if (id[0] == '-')
			continue;
		current_display_count++;
	}

	int display_index = 0;
	for (i = 0; i < count; i++)
	{
		PNODE mon = NWL_NodeEnumChild(g_ctx.edid, i);
		LPCSTR id = NWL_NodeAttrGet(mon, "ID");
		if (id[0] == '-')
			continue;

		LPCSTR saved_id = gnwinfo_hw_compare_get_array_item("Display", NULL, display_index, "ID");
		LPCSTR saved_manuf = gnwinfo_hw_compare_get_array_item("Display", NULL, display_index, "Manufacturer");
		LPCSTR saved_res = gnwinfo_hw_compare_get_array_item("Display", NULL, display_index, "Max Resolution");
		LPCSTR saved_refresh = gnwinfo_hw_compare_get_display_item("Display", NULL, display_index, "Max Refresh Rate (Hz)");
		LPCSTR saved_diag = gnwinfo_hw_compare_get_display_item("Display", NULL, display_index, "Diagonal (in)");
		LPCSTR saved_name = gnwinfo_hw_compare_get_array_item("Display", NULL, display_index, "Display Name");

		LPCSTR cur_res = NWL_NodeAttrGet(mon, "Max Resolution");
		LPCSTR cur_refresh = NWL_NodeAttrGet(mon, "Max Refresh Rate (Hz)");
		LPCSTR cur_diag = NWL_NodeAttrGet(mon, "Diagonal (in)");
		LPCSTR cur_name = NWL_NodeAttrGet(mon, "Display Name");

		nk_bool is_new = gnwinfo_hw_compare_available() && !saved_id;

		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });

		if (is_new)
		{
			if(g_hw_has_diff == nk_false)
			g_hw_has_diff = nk_true;
			nk_lhc(ctx, u8"新增", NK_TEXT_LEFT, g_color_warning);
			nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_warning);
		}
		else
		{
			nk_lhsc(ctx, NWL_NodeAttrGet(mon, "Manufacturer"), NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);

			if (gnwinfo_hw_compare_available() && saved_id)
		{
			char saved_buf[MAX_PATH] = {0};
			if (saved_name && saved_name[0] != '\0')
				snprintf(saved_buf, MAX_PATH, "%s %s@%sHz %s\" %s",
					saved_id,
					saved_res ? saved_res : "-",
					saved_refresh ? saved_refresh : "-",
					saved_diag ? saved_diag : "-",
					saved_name);
			else
				snprintf(saved_buf, MAX_PATH, "%s %s@%sHz %s\"",
					saved_id,
					saved_res ? saved_res : "-",
					saved_refresh ? saved_refresh : "-",
					saved_diag ? saved_diag : "-");

			char current_buf[MAX_PATH] = {0};
			if (cur_name && cur_name[0] != '\0')
				snprintf(current_buf, MAX_PATH, "%s %s@%sHz %s\" %s",
					id,
					cur_res ? cur_res : "-",
					cur_refresh ? cur_refresh : "-",
					cur_diag ? cur_diag : "-",
					cur_name);
			else
				snprintf(current_buf, MAX_PATH, "%s %s@%sHz %s\"",
					id,
					cur_res ? cur_res : "-",
					cur_refresh ? cur_refresh : "-",
					cur_diag ? cur_diag : "-");

			if (gnwinfo_hw_compare_is_different(current_buf, saved_buf))
				nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
			else
				nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_text_d);
		}
			else
			{
				nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
			}
		}

		if (is_new)
		{
			LPCSTR cur_res = NWL_NodeAttrGet(mon, "Max Resolution");
			LPCSTR cur_refresh = NWL_NodeAttrGet(mon, "Max Refresh Rate (Hz)");
			LPCSTR cur_diag = NWL_NodeAttrGet(mon, "Diagonal (in)");
			LPCSTR cur_name = NWL_NodeAttrGet(mon, "Display Name");

			if (cur_name && cur_name[0] != '\0')
				nk_lhcf(ctx, NK_TEXT_LEFT, g_color_warning,
					"%s %s@%sHz %s\" %s",
					id,
					cur_res ? cur_res : "-",
					cur_refresh ? cur_refresh : "-",
					cur_diag ? cur_diag : "-",
					cur_name);
			else
				nk_lhcf(ctx, NK_TEXT_LEFT, g_color_warning,
					"%s %s@%sHz %s\"",
					id,
					cur_res ? cur_res : "-",
					cur_refresh ? cur_refresh : "-",
					cur_diag ? cur_diag : "-");
		}
		else
		{
			LPCSTR cur_res = NWL_NodeAttrGet(mon, "Max Resolution");
			LPCSTR cur_refresh = NWL_NodeAttrGet(mon, "Max Refresh Rate (Hz)");
			LPCSTR cur_diag = NWL_NodeAttrGet(mon, "Diagonal (in)");
			LPCSTR cur_name = NWL_NodeAttrGet(mon, "Display Name");

			if (cur_name && cur_name[0] != '\0')
				nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l,
					"%s %s@%sHz %s\" %s",
					id,
					cur_res ? cur_res : "-",
					cur_refresh ? cur_refresh : "-",
					cur_diag ? cur_diag : "-",
					cur_name);
			else
				nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l,
					"%s %s@%sHz %s\"",
					id,
					cur_res ? cur_res : "-",
					cur_refresh ? cur_refresh : "-",
					cur_diag ? cur_diag : "-");
		}

		display_index++;
	}

	if (gnwinfo_hw_compare_available() && saved_display_count > current_display_count)
	{
		for (int i = current_display_count; i < saved_display_count; i++)
		{
			LPCSTR saved_id = gnwinfo_hw_compare_get_array_item("Display", NULL, i, "ID");
			LPCSTR saved_manuf = gnwinfo_hw_compare_get_array_item("Display", NULL, i, "Manufacturer");
			LPCSTR saved_res = gnwinfo_hw_compare_get_array_item("Display", NULL, i, "Max Resolution");
			LPCSTR saved_refresh = gnwinfo_hw_compare_get_display_item("Display", NULL, i, "Max Refresh Rate (Hz)");
			LPCSTR saved_diag = gnwinfo_hw_compare_get_display_item("Display", NULL, i, "Diagonal (in)");
			LPCSTR saved_name = gnwinfo_hw_compare_get_array_item("Display", NULL, i, "Display Name");

			if (saved_id)
			{
				if(g_hw_has_diff == nk_false)
				g_hw_has_diff = nk_true;
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
				nk_lhc(ctx, u8"已移除", NK_TEXT_LEFT, g_color_warning);

				char saved_buf[MAX_PATH] = {0};
				if (saved_name && saved_name[0] != '\0')
					snprintf(saved_buf, MAX_PATH, "%s %s@%sHz %s\" %s",
						saved_id,
						saved_res ? saved_res : "-",
						saved_refresh ? saved_refresh : "-",
						saved_diag ? saved_diag : "-",
						saved_name);
				else
					snprintf(saved_buf, MAX_PATH, "%s %s@%sHz %s\"",
						saved_id,
						saved_res ? saved_res : "-",
						saved_refresh ? saved_refresh : "-",
						saved_diag ? saved_diag : "-");

				nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
				nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_warning);
			}
		}
	}
}

static LPCSTR
get_drive_letter(PNODE volume)
{
	PNODE vol_path_name = NWL_NodeGetChild(volume, "Volume Path Names");
	if (!vol_path_name)
		goto fail;
	INT count = NWL_NodeChildCount(vol_path_name);
	for (INT i = 0; i < count; i++)
	{
		PNODE mnt = NWL_NodeEnumChild(vol_path_name, i);
		LPCSTR attr = NWL_NodeAttrGet(mnt, "Drive Letter");
		if (attr[0] != '-')
			return attr;
	}
fail:
	return NULL;
}

static VOID
open_folder(LPCSTR drive_letter, LPCSTR volume_guid)
{
	LPCWSTR path = NULL;
	if (drive_letter)
		path = NWL_Utf8ToUcs2(drive_letter);
	else
		path = NWL_Utf8ToUcs2(volume_guid);
	ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_NORMAL);
}

static VOID
draw_volume(struct nk_context* ctx, PNODE disk, BOOL cdrom)
{
	PNODE vol = NWL_NodeGetChild(disk, "Volumes");
	if (!vol)
		return;
	nk_layout_row(ctx, NK_DYNAMIC, 0, 5, (float[5]) { 0.12f, 0.18f, 0.4f, 0.3f - g_ctx.gui_ratio, g_ctx.gui_ratio });
	INT count = NWL_NodeChildCount(vol);
	for (INT i = 0; i < count; i++)
	{
		struct nk_image img = GET_PNG(IDR_PNG_DIR);
		PNODE tab = NWL_NodeEnumChild(vol, i);
		LPCSTR path = NWL_NodeAttrGet(tab, "Path");
		LPCSTR drive = get_drive_letter(tab);
		LPCSTR volume_guid = NWL_NodeAttrGet(tab, "Volume GUID");
		double percent = strtod(NWL_NodeAttrGet(tab, "Usage"), NULL);
		if (strcmp(path, g_ctx.sys_disk) == 0)
			img = GET_PNG(IDR_PNG_OS);
		if (cdrom)
			img = GET_PNG(IDR_PNG_CD);
		nk_spacer(ctx);
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_d, "[%s]",
			drive ? drive : NWL_NodeAttrGet(tab, "Partition Flag"));
		nk_lhcf(ctx, NK_TEXT_LEFT,
			g_color_text_l,
			"%s %s %s",
			NWL_NodeAttrGet(tab, "Total Space"),
			NWL_NodeAttrGet(tab, "Filesystem"),
			NWL_NodeAttrGet(tab, "Label"));
		if (g_ctx.main_flag & MAIN_VOLUME_PROG)
			gnwinfo_draw_percent_prog(ctx, percent);
		else
			nk_lhcf(ctx, NK_TEXT_LEFT,
				gnwinfo_get_color(percent, 70.0, 90.0),
				"%.0f%% %s: %s",
				percent,
				N_(N__FREE),
				NWL_NodeAttrGet(tab, "Free Space"));
		if (quick_access_button(ctx, img, volume_guid))
			open_folder(drive, volume_guid);
	}
}

static VOID
draw_volume_compact(struct nk_context* ctx, PNODE disk)
{
	INT i;
	INT count;
	CHAR buf[] = "A";
	PNODE vol = NWL_NodeGetChild(disk, "Volumes");
	if (!vol)
		return;
	for (i = 0, count = 0; ; i++)
	{
		PNODE node = NWL_NodeEnumChild(vol, i);
		if (!node)
			break;
		LPCSTR drive = get_drive_letter(node);
		if (drive)
			count++;
	}
	nk_layout_row_begin(ctx, NK_STATIC, 0, count + 1);
	nk_layout_row_push(ctx, 0.3f * g_ctx.gui_width);
	nk_spacer(ctx);
	count = NWL_NodeChildCount(vol);
	for (i = 0; i < count; i++)
	{
		PNODE tab = NWL_NodeEnumChild(vol, i);
		LPCSTR drive = get_drive_letter(tab);
		if (!drive)
			continue;
		buf[0] = drive[0];
		nk_layout_row_push(ctx, g_ctx.gui_ratio * g_ctx.gui_width);
		if (nk_button_label(ctx, buf))
			open_folder(drive, NULL);
	}
	nk_layout_row_end(ctx);
}

static VOID
draw_net_drive(struct nk_context* ctx)
{
	INT count = NWL_NodeChildCount(g_ctx.smb);
	for (INT i = 0; i < count; i++)
	{
		PNODE nd = NWL_NodeEnumChild(g_ctx.smb, i);
		if (!nd || strcmp(nd->name, "Drive") != 0)
			continue;
		LPCSTR local = NWL_NodeAttrGet(nd, "Local Name");
		LPCSTR remote = NWL_NodeAttrGet(nd, "Remote Name");
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.3f, 0.7f - g_ctx.gui_ratio, g_ctx.gui_ratio });
		nk_lhsc(ctx, N_(N__NETWORK_DRIVES), NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l, "[%s] %s", local, remote);
		if (quick_access_button(ctx, GET_PNG(IDR_PNG_DIR), NULL))
			open_folder(NULL, remote);
	}
}

static VOID
draw_net_drive_compact(struct nk_context* ctx)
{
	INT i;
	INT count = 0;
	CHAR buf[] = "A";
	for (i = 0; ; i++)
	{
		PNODE node = NWL_NodeEnumChild(g_ctx.smb, i);
		if (!node)
			break;
		if (strcmp(node->name, "Drive") != 0)
			continue;
		count++;
	}
	if (count < 1)
		return;
	nk_layout_row_begin(ctx, NK_STATIC, 0, count + 1);
	nk_layout_row_push(ctx, 0.3f * g_ctx.gui_width);
	nk_lhsc(ctx, N_(N__NETWORK_DRIVES), NK_TEXT_LEFT, g_color_text_d, nk_false, nk_true);
	count = NWL_NodeChildCount(g_ctx.smb);
	for (i = 0; i < count; i++)
	{
		PNODE tab = NWL_NodeEnumChild(g_ctx.smb, i);
		if (strcmp(tab->name, "Drive") != 0)
			continue;
		LPCSTR drive = NWL_NodeAttrGet(tab, "Local Name");
		buf[0] = drive[0];
		nk_layout_row_push(ctx, g_ctx.gui_ratio * g_ctx.gui_width);
		if (nk_button_label(ctx, buf))
			open_folder(drive, NULL);
	}
	nk_layout_row_end(ctx);
}

static VOID
draw_storage(struct nk_context* ctx)
{
	nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 1.0f - g_ctx.gui_ratio, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_DISK), N_(N__STORAGE), NK_TEXT_LEFT, g_color_text_d);
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_SMART), "S.M.A.R.T."))
		g_ctx.window_flag |= GUI_WINDOW_SMART;

	INT count = NWL_NodeChildCount(g_ctx.disk);
	INT saved_disk_count = gnwinfo_hw_compare_get_array_size("Disks", NULL);

	for (INT i = 0; i < count; i++)
	{
		BOOL cdrom;
		LPCSTR prefix = "HD";
		LPCSTR path, id = "-";
		LPCSTR ssd = "";
		PNODE disk = NWL_NodeEnumChild(g_ctx.disk, i);
		if (!disk)
			continue;
		path = NWL_NodeAttrGet(disk, "Path");
		if (strncmp(path, "\\\\.\\CdRom", 9) == 0)
		{
			cdrom = TRUE;
			prefix = "CD";
			id = &path[9];
		}
		else if (strncmp(path, "\\\\.\\PhysicalDrive", 17) == 0)
		{
			cdrom = FALSE;
			id = &path[17];
			if (strcmp(NWL_NodeAttrGet(disk, "SSD"), NA_BOOL_TRUE) == 0)
				ssd = " SSD";
			if (strcmp(NWL_NodeAttrGet(disk, "Removable"), NA_BOOL_TRUE) == 0)
				prefix = "RM";
		}
		else
		{
			cdrom = FALSE;
			prefix = "HD";
		}

		LPCSTR saved_size = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Size");
		LPCSTR saved_part = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Partition Table");
		LPCSTR saved_prod = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Product ID");
		nk_bool is_new = gnwinfo_hw_compare_available() && !saved_size;

		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
		snprintf(m_buf, MAX_PATH, "%s%s %s%s",
			prefix,
			id,
			NWL_NodeAttrGet(disk, "Type"),
			ssd);
		
		if (is_new)
		{
			if(g_hw_has_diff == nk_false)
			g_hw_has_diff = nk_true;
			nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_warning);
			nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_warning);
		}
		else
		{
			nk_lhsc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);

			if (gnwinfo_hw_compare_available() && saved_size)
			{
				char saved_buf[MAX_PATH] = {0};
				snprintf(saved_buf, MAX_PATH, "%s %s %s",
					saved_size,
					saved_part ? saved_part : "-",
					saved_prod ? saved_prod : "-");

				char current_buf[MAX_PATH] = {0};
				snprintf(current_buf, MAX_PATH, "%s %s %s",
					NWL_NodeAttrGet(disk, "Size"),
					NWL_NodeAttrGet(disk, "Partition Table"),
					NWL_NodeAttrGet(disk, "Product ID"));

				if (gnwinfo_hw_compare_is_different(current_buf, saved_buf))
					nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
				else
					nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_text_d);
			}
			else
			{
				nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
			}
		}

		nk_lhcf(ctx, NK_TEXT_LEFT,
			g_color_text_l,
			"%s %s %s",
			NWL_NodeAttrGet(disk, "Size"),
			NWL_NodeAttrGet(disk, "Partition Table"),
			NWL_NodeAttrGet(disk, "Product ID"));

		LPCSTR health = NWL_NodeAttrGet(disk, "Health Status");
		if ((g_ctx.main_flag & MAIN_DISK_SMART) && strcmp(health, "-") != 0)
		{
			nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
			nk_spacer(ctx);

			//draw_volume(ctx, disk, cdrom);	

			LPCSTR life = strchr(health, '(');
			GETTEXT_STR_ID whealth = N__UNKNOWN;
			struct nk_color color = g_color_unknown;
			LPCSTR temp = NWL_NodeAttrGet(disk, "Temperature (C)");
			if (strncmp(health, "Good", 4) == 0)
			{
				color = g_color_good;
				whealth = N__GOOD;
			}
			else if (strncmp(health, "Caution", 7) == 0)
			{
				color = g_color_warning;
				whealth = N__CAUTION;
			}
			else if (strncmp(health, "Bad", 3) == 0)
			{
				color = g_color_error;
				whealth = N__BAD;
			}

			LPCSTR saved_health = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Health Status");

			if (gnwinfo_hw_compare_available() && saved_health)
			{
				GETTEXT_STR_ID saved_whealth = N__UNKNOWN;
				if (strncmp(saved_health, "Good", 4) == 0)
					saved_whealth = N__GOOD;
				else if (strncmp(saved_health, "Caution", 7) == 0)
					saved_whealth = N__CAUTION;
				else if (strncmp(saved_health, "Bad", 3) == 0)
					saved_whealth = N__BAD;

				LPCSTR saved_life = strchr(saved_health, '(');
				char saved_health_buf[MAX_PATH] = {0};
				if (saved_life && saved_life[0] != '\0')
				{
					snprintf(saved_health_buf, MAX_PATH, "%s%s", N_(saved_whealth), saved_life);
				}
				else
				{
					snprintf(saved_health_buf, MAX_PATH, "%s", N_(saved_whealth));
				}

				if (gnwinfo_hw_compare_is_different(health, saved_health))
					nk_lhc(ctx, saved_health_buf, NK_TEXT_LEFT, g_color_warning);
				else
					nk_lhc(ctx, saved_health_buf, NK_TEXT_LEFT, g_color_text_d);
			}
			else
			{
				nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_text_d);
			}

			char current_health_buf[MAX_PATH] = {0};
			if (life && life[0] != '\0')
			{
				snprintf(current_health_buf, MAX_PATH, "%s%s", N_(whealth), life);
			}
			else
			{
				snprintf(current_health_buf, MAX_PATH, "%s", N_(whealth));
			}

			nk_lhcf(ctx, NK_TEXT_LEFT, color,
				u8"%s %s"TEMP_CELSIUS_SYMBOL, current_health_buf,
				temp[0] == '-' ? "-" : temp);
		}
	}

	if (gnwinfo_hw_compare_available() && saved_disk_count > count)
	{
		for (int i = count; i < saved_disk_count; i++)
		{
			LPCSTR saved_path = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Path");
			if (!saved_path)
				continue;

			LPCSTR prefix = "HD";
			LPCSTR id = "-";
			if (strncmp(saved_path, "\\\\.\\CdRom", 9) == 0)
			{
				prefix = "CD";
				id = &saved_path[9];
			}
			else if (strncmp(saved_path, "\\\\.\\PhysicalDrive", 17) == 0)
			{
				id = &saved_path[17];
			}

			LPCSTR saved_type = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Type");
			LPCSTR saved_ssd = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "SSD");
			LPCSTR saved_size = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Size");
			LPCSTR saved_part = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Partition Table");
			LPCSTR saved_prod = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Product ID");

			nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
			if(g_hw_has_diff == nk_false)
			g_hw_has_diff = nk_true;
			nk_lhc(ctx, u8"已移除", NK_TEXT_LEFT, g_color_warning);

			char saved_buf[MAX_PATH] = {0};
			snprintf(saved_buf, MAX_PATH, "%s%s %s%s %s %s %s",
				prefix,
				id,
				saved_type ? saved_type : "-",
				saved_ssd && strcmp(saved_ssd, "false") != 0 ? " SSD" : "",
				saved_size ? saved_size : "-",
				saved_part ? saved_part : "-",
				saved_prod ? saved_prod : "-");

			nk_lhc(ctx, saved_buf, NK_TEXT_LEFT, g_color_warning);
			nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_warning);

			LPCSTR saved_health = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Health Status");
			LPCSTR saved_temp = gnwinfo_hw_compare_get_array_item("Disks", NULL, i, "Temperature (C)");

			if (saved_health && saved_health[0] != '\0' && strcmp(saved_health, "-") != 0)
			{
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, (1.0f-0.2f-g_ctx.gui_ratio)/2, (1.0f-0.2f-g_ctx.gui_ratio)/2 });
				nk_spacer(ctx);

				GETTEXT_STR_ID saved_whealth = N__UNKNOWN;
				if (strncmp(saved_health, "Good", 4) == 0)
					saved_whealth = N__GOOD;
				else if (strncmp(saved_health, "Caution", 7) == 0)
					saved_whealth = N__CAUTION;
				else if (strncmp(saved_health, "Bad", 3) == 0)
					saved_whealth = N__BAD;

				LPCSTR saved_life = strchr(saved_health, '(');
				char saved_health_buf[MAX_PATH] = {0};
				if (saved_life && saved_life[0] != '\0')
				{
					snprintf(saved_health_buf, MAX_PATH, "%s%s", N_(saved_whealth), saved_life);
				}
				else
				{
					snprintf(saved_health_buf, MAX_PATH, "%s", N_(saved_whealth));
				}

				nk_lhc(ctx, saved_health_buf, NK_TEXT_LEFT, g_color_warning);
				nk_lhc(ctx, "-", NK_TEXT_LEFT, g_color_warning);
			}
		}
	}
	if (g_ctx.main_flag & MAIN_DISK_COMPACT)
		draw_net_drive(ctx);
	else
		draw_net_drive_compact(ctx);
}
static VOID
draw_storage_simple(struct nk_context* ctx)
{
	nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f});
	nk_image_label(ctx, GET_PNG(IDR_PNG_DISK), N_(N__STORAGE), NK_TEXT_LEFT, g_color_text_d);
	// if (quick_access_button(ctx, GET_PNG(IDR_PNG_SMART), "S.M.A.R.T."))
	// 	g_ctx.window_flag |= GUI_WINDOW_SMART;

	INT count = NWL_NodeChildCount(g_ctx.disk);

	for (INT i = 0; i < count; i++)
	{
		BOOL cdrom;
		LPCSTR prefix = "HD";
		LPCSTR path, id = "-";
		LPCSTR ssd = "";
		PNODE disk = NWL_NodeEnumChild(g_ctx.disk, i);
		if (!disk)
			continue;
		path = NWL_NodeAttrGet(disk, "Path");
		if (strncmp(path, "\\\\.\\CdRom", 9) == 0)
		{
			cdrom = TRUE;
			prefix = "CD";
			id = &path[9];
		}
		else if (strncmp(path, "\\\\.\\PhysicalDrive", 17) == 0)
		{
			cdrom = FALSE;
			id = &path[17];
			if (strcmp(NWL_NodeAttrGet(disk, "SSD"), NA_BOOL_TRUE) == 0)
				ssd = " SSD";
			if (strcmp(NWL_NodeAttrGet(disk, "Removable"), NA_BOOL_TRUE) == 0)
				prefix = "RM";
		}
		else
		{
			cdrom = FALSE;
			prefix = "HD";
		}

		nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.3f, 0.2f,0.3f, 0.2f });
		snprintf(m_buf, MAX_PATH, "%s%s %s%s",
			prefix,
			id,
			NWL_NodeAttrGet(disk, "Type"),
			ssd);
		nk_lhsc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);
		nk_lhcf(ctx, NK_TEXT_LEFT,
			g_color_text_l,
			"%s",
			NWL_NodeAttrGet(disk, "Size"));
		LPCSTR health = NWL_NodeAttrGet(disk, "Health Status");
		if ((g_ctx.main_flag & MAIN_DISK_SMART) && strcmp(health, "-") != 0)
		{
			// draw_volume(ctx, disk, cdrom);	
			LPCSTR life = strchr(health, '(');
			GETTEXT_STR_ID whealth = N__UNKNOWN;
			struct nk_color color = g_color_unknown;
			LPCSTR temp = NWL_NodeAttrGet(disk, "Temperature (C)");
			if (strncmp(health, "Good", 4) == 0)
			{
				color = g_color_good;
				whealth = N__GOOD;
			}
			else if (strncmp(health, "Caution", 7) == 0)
			{
				color = g_color_warning;
				whealth = N__CAUTION;
			}
			else if (strncmp(health, "Bad", 3) == 0)
			{
				color = g_color_error;
				whealth = N__BAD;
			}

			char current_health_buf[MAX_PATH] = {0};
			if (life && life[0] != '\0')
			{
				snprintf(current_health_buf, MAX_PATH, "%s%s", N_(whealth), life);
			}
			else
			{
				snprintf(current_health_buf, MAX_PATH, "%s", N_(whealth));
			}

			nk_lhcf(ctx, NK_TEXT_LEFT, color,
				u8"%s %s"TEMP_CELSIUS_SYMBOL, current_health_buf,
				temp[0] == '-' ? "-" : temp);
		}
		else
		{
			nk_spacing(ctx, 1);
		}
		nk_lhc(ctx, u8"● 正常", NK_TEXT_LEFT, g_color_good);

		

		nk_layout_row(ctx, NK_DYNAMIC, 35, 3, (float[3]) { 0.05f, 0.65f,0.3f });
		nk_spacer(ctx);
		nk_spacer(ctx);

		// nk_lhcf(ctx, NK_TEXT_LEFT,
		// 	g_color_text_l,
		// 	"%s %s %s",
		// 	NWL_NodeAttrGet(disk, "Size"),
		// 	NWL_NodeAttrGet(disk, "Partition Table"),
		// 	NWL_NodeAttrGet(disk, "Product ID"));
		if (ssd[0] == '\0')
		{
			// nk_layout_row(ctx, NK_DYNAMIC, 30, 1, (float[1]) { 1.0f });
			if (nk_button_label(ctx, u8"磁道电机性能检测"))
			{
			}
		}
	}

	if (g_ctx.main_flag & MAIN_DISK_COMPACT)
		draw_net_drive(ctx);
	else
		draw_net_drive_compact(ctx);
}

static LPCSTR
get_first_ipv4(PNODE node)
{
	PNODE unicasts = NWL_NodeGetChild(node, "Unicasts");
	if (!unicasts)
		return "";
	INT count = NWL_NodeChildCount(unicasts);
	for (INT i = 0; i < count; i++)
	{
		PNODE ip = NWL_NodeEnumChild(unicasts, i);
		LPCSTR addr = NWL_NodeAttrGet(ip, "IPv4");
		if (strcmp(addr, "-") != 0)
			return addr;
	}
	return "";
}

static VOID
draw_network(struct nk_context* ctx)
{
	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.64f, 0.18f - g_ctx.gui_ratio, 0.18f, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_NETWORK), N_(N__NETWORK), NK_TEXT_LEFT, g_color_text_d);
	nk_lhcf(ctx, NK_TEXT_LEFT, g_color_warning, u8"\u2191 %s", g_ctx.net_traffic.StrSend);
	nk_lhcf(ctx, NK_TEXT_LEFT, g_color_unknown, u8"\u2193 %s", g_ctx.net_traffic.StrRecv);
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_EDIT), NULL))
		ShellExecuteW(NULL, NULL, L"::{7007ACC7-3202-11D1-AAD2-00805FC1270E}", NULL, NULL, SW_NORMAL);

	INT count = NWL_NodeChildCount(g_ctx.network);
	for (INT i = 0; i < count; i++)
	{
		BOOL is_active = FALSE;
		PNODE nw = NWL_NodeEnumChild(g_ctx.network, i);
		struct nk_color color = g_color_error;
		if (!nw)
			continue;
		if (strcmp(NWL_NodeAttrGet(nw, "Status"), "Active") == 0)
		{
			color = g_color_good;
			is_active = TRUE;
		}

		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.64f, 0.36f - g_ctx.gui_ratio, g_ctx.gui_ratio });
		nk_lhsc(ctx, NWL_NodeAttrGet(nw, "Description"), NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);
		nk_lhc(ctx, get_first_ipv4(nw), NK_TEXT_LEFT, color);
		if (quick_access_button(ctx,
			strcmp(NWL_NodeAttrGet(nw, "Type"), "IEEE 802.11 Wireless") == 0 ? GET_PNG(IDR_PNG_WLAN) : GET_PNG(IDR_PNG_ETH), NULL))
		{
			swprintf((WCHAR*)m_buf, MAX_PATH / sizeof(WCHAR),
				L"::{7007ACC7-3202-11D1-AAD2-00805FC1270E}\\::%s", NWL_Utf8ToUcs2(NWL_NodeAttrGet(nw, "Network Adapter")));
			ShellExecuteW(NULL, NULL, (WCHAR*)m_buf, NULL, NULL, SW_NORMAL);
		}

		if (g_ctx.main_flag & MAIN_NET_DETAIL)
		{
			nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.64f, 0.36f });
			int len = snprintf(m_buf, MAX_PATH, "%s", strcmp(NWL_NodeAttrGet(nw, "DHCP Enabled"), NA_BOOL_TRUE) == 0 ? " DHCP" : "");
			if (is_active && len >= 0 && len < MAX_PATH)
				snprintf(m_buf + len, MAX_PATH - len, u8" \u21c5 %s / %s",
					NWL_NodeAttrGet(nw, "Transmit Link Speed"),
					NWL_NodeAttrGet(nw, "Receive Link Speed"));
			nk_lhsc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);
			nk_lhc(ctx,
				NWL_NodeAttrGet(nw, "MAC Address"), NK_TEXT_LEFT, g_color_text_l);

			if (strcmp(NWL_NodeAttrGet(nw, "WLAN State"), "Connected") == 0)
			{
				nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.64f, 0.36f });
				nk_lhscf(ctx, NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true, " %s%% %s",
					NWL_NodeAttrGet(nw, "WLAN Signal Quality"),
					NWL_NodeAttrGet(nw, "WLAN Profile"));
				nk_lhscf(ctx, NK_TEXT_LEFT, g_color_text_d, nk_true, nk_false, "%s %s",
					NWL_NodeAttrGet(nw, "WLAN Auth"),
					NWL_NodeAttrGet(nw, "WLAN Cipher"));
			}
		}
	}
}

static VOID
draw_audio(struct nk_context* ctx)
{
	UINT i;
	if (!g_ctx.audio)
		return;

	nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 1.0f - g_ctx.gui_ratio, g_ctx.gui_ratio });
	nk_image_label(ctx, GET_PNG(IDR_PNG_MM), N_(N__AUDIO), NK_TEXT_LEFT, g_color_text_d);
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_SOUND), NULL))
		ShellExecuteW(NULL, NULL, L"::{26EE0668-A00A-44D7-9371-BEB064C98683}\\2\\::{F2DDFC82-8F12-4CDD-B7DC-D4FE1425AA4D}", NULL, NULL, SW_NORMAL);
	nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.7f, 0.3f });
	for (i = 0; i < g_ctx.audio_count; i++)
	{
		nk_lhsc(ctx, NWL_Ucs2ToUtf8(g_ctx.audio[i].name), NK_TEXT_LEFT, g_color_text_d, nk_true, nk_true);
		nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_l,
			"%s %.0f%%",
			g_ctx.audio[i].is_default ? "*" : " ",
			100.0f * g_ctx.audio[i].volume);
	}
}

static int display_interface = -1;
static int last_display_interface = -2;
static GdipFont* g_title_font = NULL;
static GdipFont* g_button_font = NULL;

 VOID
 draw_heat_dissipation(struct nk_context* ctx)
 {
 	nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 1.0f - g_ctx.gui_ratio, g_ctx.gui_ratio });
 	nk_image_label(ctx, GET_PNG(IDR_PNG_SENSOR), u8"散热信息", NK_TEXT_LEFT, g_color_text_d);
 	nk_spacer(ctx);

 	nk_layout_row(ctx, NK_DYNAMIC, 0, 5, (float[5]) { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f });

	 if (g_ctx.cpu_info && g_ctx.cpu_info[0].MsrTemp > 0)
		snprintf(m_buf, MAX_PATH, u8"CPU温度: %d°C", g_ctx.cpu_info[0].MsrTemp);
	 else
	 	snprintf(m_buf, MAX_PATH, u8"CPU温度: --");
	 nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);

	 double cpu_fan = get_fan_speed_from_sensors("CPU");
	 if (cpu_fan > 0)
	 	snprintf(m_buf, MAX_PATH, u8"CPU风扇: %.0f RPM", cpu_fan);
	 else
	 	snprintf(m_buf, MAX_PATH, u8"CPU风扇: --");
	 nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);

	if (g_ctx.lib.NwGpu && g_ctx.lib.NwGpu->DeviceCount > 0 && g_ctx.lib.NwGpu->Device[0].Temperature > 0)
		snprintf(m_buf, MAX_PATH, u8"显卡温度: %.1f°C", g_ctx.lib.NwGpu->Device[0].Temperature);
	else
		snprintf(m_buf, MAX_PATH, u8"显卡温度: --");
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);

	if (g_ctx.lib.NwGpu && g_ctx.lib.NwGpu->DeviceCount > 0 && g_ctx.lib.NwGpu->Device[0].FanSpeed > 0)
		snprintf(m_buf, MAX_PATH, u8"显卡风扇: %llu RPM", g_ctx.lib.NwGpu->Device[0].FanSpeed);
	else
		snprintf(m_buf, MAX_PATH, u8"显卡风扇: --");
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);

	double chassis_fan = get_fan_speed_from_sensors("Chassis");
	if (chassis_fan > 0)
		snprintf(m_buf, MAX_PATH, u8"机箱风扇: %.0f RPM", chassis_fan);
	else
		snprintf(m_buf, MAX_PATH, u8"机箱风扇: --");
	nk_lhc(ctx, m_buf, NK_TEXT_LEFT, g_color_text_l);
 }

int gnwinfo_get_display_interface(void)
{
	return display_interface;
}

static VOID
draw_startup_screen(struct nk_context* ctx, float width, float height)
{
	struct nk_window* win = ctx->current;
	
	float content_height = g_ctx.gui_title;
	
	if (g_title_font == NULL)
	{
		WCHAR font_name[64];
		GetPrivateProfileStringW(L"Window", L"Font", L"-", font_name, 64, g_ini_path);
		if (wcscmp(font_name, L"-") == 0)
			wcscpy_s(font_name, 64, L"Microsoft YaHei");
		g_title_font = nk_gdip_load_font(font_name, (int)(22 * g_dpi_factor));
		g_button_font = nk_gdip_load_font(font_name, (int)(20 * g_dpi_factor));
	}
	
	nk_layout_row(ctx, NK_STATIC, content_height * 0.1f, 2, (float[2]) { width*0.5f, width*0.5f });
	nk_spacing(ctx, 1);
	
	nk_layout_row_begin(ctx, NK_STATIC, 50, 5);
	{
		nk_layout_row_push(ctx, width * 0.3f);
		nk_spacing(ctx, 1);
		
		nk_layout_row_push(ctx, 50);
		if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_COMPUTER), ""))
		{
			display_interface = -1;
		}
		
		nk_layout_row_push(ctx, width * 0.2f);
		if (g_title_font)
		{
			nk_gdip_set_font(g_title_font);
			nk_label(ctx, u8"系统监控仪表盘", NK_TEXT_CENTERED);
			nk_gdip_set_font(g_font);
		}
		else
		{
			nk_label(ctx, u8"系统监控仪表盘", NK_TEXT_CENTERED);
		}
		
		nk_layout_row_push(ctx, width * 0.2f);
		{
			struct nk_style_button btn = ctx->style.button;
			btn.rounding = 8.0f;
			btn.border = 1.0f;
			btn.padding = nk_vec2(8.0f, 8.0f);
			btn.text_alignment = NK_TEXT_CENTERED;
			if (g_button_font) {
				nk_gdip_set_font(g_button_font);
			}
			if (nk_button_label_styled(ctx, &btn, u8"详细信息")) {
				display_interface = 0;
			}
			nk_gdip_set_font(g_font);
		}
		
		nk_layout_row_push(ctx, width * 0.15f);
		nk_layout_row(ctx, NK_STATIC, 5, 1, (float[1]) { width });
		nk_spacing(ctx, 1);
	}
	nk_layout_row_end(ctx);
	
	float startup_content_height = height - 50 - g_ctx.gui_title - 10;
	nk_layout_row(ctx, NK_STATIC, startup_content_height, 3, (float[3]) { width*0.2f, width*0.6f, width*0.2f });
	nk_spacing(ctx, 1);
	if (nk_group_begin(ctx, "startup_content", 0))
	{
		nk_layout_row(ctx, NK_STATIC, 30, 1, (float[1]) { width });
		nk_layout_row_begin(ctx, NK_STATIC, 30, 1);
		{
			nk_layout_row_push(ctx, width);
		nk_bool has_changes = gnwinfo_hw_compare_check_changes();
		if (has_changes) {
			nk_lhc(ctx, u8"系统配置有变更", NK_TEXT_LEFT, g_color_warning);
		} else {
			nk_lhc(ctx, u8"系统配置未变更", NK_TEXT_LEFT, g_color_good);
		}
		}
		nk_layout_row_end(ctx);
		draw_group_separator(ctx);
		
		draw_processor_simple(ctx);
		draw_group_separator(ctx);
		nk_layout_row(ctx, NK_STATIC, 10, 1, (float[1]) { width });
		nk_spacing(ctx, 1);
		
		draw_memory_simple(ctx);
		draw_group_separator(ctx);
		nk_layout_row(ctx, NK_STATIC, 10, 1, (float[1]) { width });
		nk_spacing(ctx, 1);
		
		draw_storage_simple(ctx);   
		draw_group_separator(ctx);
		nk_layout_row(ctx, NK_STATIC, 10, 1, (float[1]) { width });
		nk_spacing(ctx, 1);
		nk_group_end(ctx);
	}
}

VOID
gnwinfo_draw_main_window(struct nk_context* ctx, float width, float height)
{
	struct nk_window* win;
	const char* title = u8"工控机哨兵";
	for (win = ctx->begin; win != NULL; win = win->next)
	{
		if (strcmp(win->name_string, title) == 0)
		{
			if (win->flags & NK_WINDOW_HIDDEN)
			{
				printf("DEBUG: Window is HIDDEN, setting was_hidden = TRUE\n");
				g_window_was_hidden = nk_true;
			}
			else if (g_window_was_hidden)
			{
				printf("DEBUG: Window was hidden, now shown\n");
				g_window_was_hidden = nk_false;
				printf("DEBUG: Calling gnwinfo_hw_compare_reload()\n");
				gnwinfo_hw_compare_reload();
				g_hw_has_diff = nk_false;
			}
			else if (g_first_window_show)
			{
				printf("DEBUG: First window show\n");
				g_first_window_show = nk_false;
				
				printf("DEBUG: gnwinfo_hw_compare_available() = %d\n", gnwinfo_hw_compare_available());
				if (!gnwinfo_hw_compare_available())
				{
					printf("DEBUG: No JSON available, will save after UI render\n");
					g_hw_has_diff = nk_true;
				}
				else
				{
					g_hw_has_diff = nk_false;
				}
				gnwinfo_hw_compare_reload();
			}
			win->flags &= ~NK_WINDOW_HIDDEN;
			break;
		}
	}
	if (!nk_begin_ex(ctx, u8"工控机哨兵",
		nk_rect(0, 0, width, height),
		g_bginfo ? (NK_WINDOW_BACKGROUND | NK_WINDOW_NO_SCROLLBAR) : (NK_WINDOW_BACKGROUND | NK_WINDOW_CLOSABLE | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR),
		nk_image_id(0), GET_PNG(IDR_PNG_CLOSE)))
	{
		nk_end(ctx);
		printf("DEBUG: Window closing, setting was_hidden = TRUE\n");
		g_window_was_hidden = nk_true;
		//InterlockedExchange(&g_ctx.exit_pending, 1);
		ShowWindow(g_ctx.wnd, SW_HIDE);
		return;
	}

	if (display_interface == -1)
	{
		draw_startup_screen(ctx, width, height);
		nk_end(ctx);
		return;
	}

	nk_layout_row_begin(ctx, NK_DYNAMIC, 50, 7);

	struct nk_rect rect = nk_layout_widget_bounds(ctx);
	float button_ratio = 50.0f / rect.w;
	g_ctx.gui_ratio = 20.0f / rect.w;

	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_SENSOR), N_(N__SENSOR_VIEW)))
		display_interface = 0;
	
	
	
	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_DISK), u8"硬盘信息"))
		display_interface = 1;
	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_PC), u8"蓝屏记录"))
		display_interface = 2;
	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_OPTIMIZE), u8"系统优化"))
		display_interface = 3;
	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_IPMI), u8"IPMI"))
	{	

	}
	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_COMPUTER), u8"返回首页"))
		display_interface = -1;
	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_REFRESH), N_(N__REFRESH)))
	{
		gnwinfo_ctx_update(IDT_TIMER_1M);
		gnwinfo_ctx_update(IDT_TIMER_DISK);
		gnwinfo_ctx_update(IDT_TIMER_DISPLAY);
		gnwinfo_ctx_update(IDT_TIMER_SMB);
	}
	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_INFO), N_(N__ABOUT)))
		g_ctx.window_flag |= GUI_WINDOW_ABOUT;

	
	nk_layout_row_push(ctx, button_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_SETTINGS), N_(N__SETTINGS)))
		g_ctx.window_flag |= GUI_WINDOW_SETTINGS;
	nk_layout_row_push(ctx, g_ctx.gui_ratio);
	nk_layout_row_end(ctx);

	float content_height = height - 50 - g_ctx.gui_title - 10;
	nk_layout_row(ctx, NK_STATIC, content_height, 1, (float[1]) { width });
	if (nk_group_begin(ctx, "main_content", 0)) {
		if (display_interface == 1)
		{
		gnwinfo_load_smart_history();
		int csv_count = gnwinfo_get_smart_history_count();
		
		if (csv_count == 0) {
			nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f });
			nk_lhc(ctx, u8"没有找到SMART历史数据文件", NK_TEXT_LEFT, g_color_text_l);
		} else {
			static const char* critical_ids[] = {
				"01 ", "03 ", "05 ", "10 ", "13 ", "14 ", "15 ", "191 ", "187 ", "199 ", "231 ", NULL
			};
			
			for (int csv_idx = 0; csv_idx < csv_count; csv_idx++) {
				const char* filename = gnwinfo_get_smart_history_filename(csv_idx);
				if (filename) {
					const char* basename = strrchr(filename, '\\');
					if (basename) basename++;
					else basename = filename;
					
					char display_name[128] = {0};
					char drive_letter = '\0';
					char serial[64] = {0};
					
					if (basename[0] && basename[1] == '_' && basename[2] != '\0') {
						drive_letter = basename[0];
						const char* serial_start = basename + 2;
						const char* diskdata = strstr(serial_start, "_diskdata");
						if (diskdata) {
							int len = (int)(diskdata - serial_start);
							if (len > 0 && len < 64) {
								strncpy_s(serial, sizeof(serial), serial_start, len);
							}
						}
					} else {
						const char* underscore = strchr(basename, '_');
						if (underscore) {
							underscore++;
							const char* diskdata = strstr(underscore, "_diskdata");
							if (diskdata) {
								int len = (int)(diskdata - underscore);
								if (len > 0 && len < 64) {
									strncpy_s(serial, sizeof(serial), underscore, len);
								}
							}
						}
					}
					
					int rows = gnwinfo_get_smart_history_rows(csv_idx);
					int cols = gnwinfo_get_smart_history_cols(csv_idx);
					const char* health = "";
					if (rows > 0 && cols > 1) {
						health = gnwinfo_get_smart_history_cell(csv_idx, rows - 1, 1);
						if (health == NULL) health = "";
					}
					
					if (drive_letter && serial[0]) {
						if (health[0]) {
							_snprintf_s(display_name, sizeof(display_name), _TRUNCATE, "%c: (%s) - %s", drive_letter, serial, translate_health_status(health));
						} else {
							_snprintf_s(display_name, sizeof(display_name), _TRUNCATE, "%c: (%s)", drive_letter, serial);
						}
					} else if (serial[0]) {
						if (health[0]) {
							_snprintf_s(display_name, sizeof(display_name), _TRUNCATE, "%s - %s", serial, translate_health_status(health));
						} else {
							strcpy_s(display_name, sizeof(display_name), serial);
						}
					} else {
						strcpy_s(display_name, sizeof(display_name), basename);
					}
					
					nk_layout_row(ctx, NK_DYNAMIC, 25, 1, (float[1]) { 1.0f });
					nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_d, u8"硬盘: %s", display_name);
				}
				
				int rows = gnwinfo_get_smart_history_rows(csv_idx);
				int cols = gnwinfo_get_smart_history_cols(csv_idx);
				
				if (rows > 0 && cols > 0) {
					int critical_cols[64] = {0};
					int critical_count = 0;
					critical_cols[critical_count++] = 0;
					critical_cols[critical_count++] = 1;
					
					for (int c = 2; c < cols && critical_count < 64; c++) {
						const char* cell = gnwinfo_get_smart_history_cell(csv_idx, 0, c);
						if (cell) {
							for (int i = 0; critical_ids[i] != NULL; i++) {
								if (strncmp(cell, critical_ids[i], 3) == 0) {
									critical_cols[critical_count++] = c;
									break;
								}
							}
						}
					}
					
					float col_width = 280.0f;
					float time_width = 180.0f;
					float health_width = 120.0f;
					float row_height = 22.0f;
					
					float table_height = 250.0f;
					if (rows > 1) {
						table_height = (rows > 10) ? 250.0f : (float)(rows * row_height + 30);
					}
					
					nk_layout_row(ctx, NK_DYNAMIC, table_height, 1, (float[1]) { 1.0f });
					
					char group_name[32];
					_snprintf_s(group_name, sizeof(group_name), _TRUNCATE, "smart_table_%d", csv_idx);
					
					if (nk_group_begin(ctx, group_name, NK_WINDOW_BORDER)) {
						float widths[64];
						for (int i = 0; i < critical_count && i < 64; i++) {
							int c = critical_cols[i];
							if (c == 0) widths[i] = time_width;
							else if (c == 1) widths[i] = health_width;
							else widths[i] = col_width;
						}
						
						nk_layout_row(ctx, NK_STATIC, row_height, critical_count, widths);
						for (int i = 0; i < critical_count; i++) {
							int c = critical_cols[i];
							const char* cell = gnwinfo_get_smart_history_cell(csv_idx, 0, c);
							if (cell)
								nk_lhc(ctx, translate_smart_header(cell), NK_TEXT_LEFT, g_color_text_d);
							else
								nk_lhc(ctx, "", NK_TEXT_LEFT, g_color_text_d);
						}
						
						for (int r = rows - 1; r >= 1; r--) {
							nk_layout_row(ctx, NK_STATIC, row_height, critical_count, widths);
							for (int i = 0; i < critical_count; i++) {
								int c = critical_cols[i];
								const char* cell = gnwinfo_get_smart_history_cell(csv_idx, r, c);
								if (cell) {
									if (c == 1) {
										nk_lhc(ctx, translate_health_status(cell), NK_TEXT_LEFT, g_color_text_l);
									} else {
										nk_lhc(ctx, cell, NK_TEXT_LEFT, g_color_text_l);
									}
								} else {
									nk_lhc(ctx, "", NK_TEXT_LEFT, g_color_text_l);
								}
							}
						}
						nk_group_end(ctx);
					}
				}
				
				nk_layout_row(ctx, NK_DYNAMIC, 10, 1, (float[1]) { 1.0f });
				draw_group_separator(ctx);
			}
		}
		goto out;
	}

	if (display_interface == 2)
	{
		int bsod_count = gnwinfo_bsod_get_record_count();
		
		if (bsod_count == 0) {
			nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f });
			nk_lhc(ctx, u8"没有检测到蓝屏记录", NK_TEXT_LEFT, g_color_text_l);
		} else {
			nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f });
			nk_lhcf(ctx, NK_TEXT_LEFT, g_color_text_d, u8"检测到 %d 条蓝屏记录", bsod_count);
			
			nk_layout_row(ctx, NK_DYNAMIC, 10, 1, (float[1]) { 1.0f });
			draw_group_separator(ctx);
			
			for (int i = 0; i < bsod_count; i++) {
				const BSOD_RECORD* record = gnwinfo_bsod_get_record(i);
				if (record == NULL)
					continue;
				
				nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
				nk_lhc(ctx, u8"时间:", NK_TEXT_LEFT, g_color_text_d);
				nk_lhc(ctx, record->timestamp, NK_TEXT_LEFT, g_color_text_l);
				
				nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
				nk_lhc(ctx, u8"错误码:", NK_TEXT_LEFT, g_color_text_d);
				nk_lhcf(ctx, NK_TEXT_LEFT, g_color_warning, "%s (%s)", record->bugcheck_code, record->bugcheck_name);
				
				if (record->dump_file[0] != '\0') {
					nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
					nk_lhc(ctx, u8"转储文件:", NK_TEXT_LEFT, g_color_text_d);
					const char* basename = strrchr(record->dump_file, '\\');
					if (basename) basename++;
					else basename = record->dump_file;
					nk_lhc(ctx, basename, NK_TEXT_LEFT, g_color_text_l);
				}
				
				nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
				nk_lhc(ctx, u8"描述:", NK_TEXT_LEFT, g_color_text_d);
				nk_lhc(ctx, gnwinfo_bsod_get_code_desc(record->bugcheck_id), NK_TEXT_LEFT, g_color_text_l);
				
				if (record->process_name[0] != '\0') {
					nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
					nk_lhc(ctx, u8"崩溃时运行的程序:", NK_TEXT_LEFT, g_color_text_d);
					nk_lhc(ctx, record->process_name, NK_TEXT_LEFT, g_color_text_l);
				}
				
				if (record->caused_by_driver[0] != '\0') {
					nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
					nk_lhc(ctx, u8"导致问题的驱动模块:", NK_TEXT_LEFT, g_color_text_d);
					nk_lhc(ctx, record->caused_by_driver, NK_TEXT_LEFT, g_color_warning);
				}
				
				if (record->caused_by_driver[0] != '\0' && record->process_name[0] != '\0') {
					nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
					nk_lhc(ctx, u8"原因分析:", NK_TEXT_LEFT, g_color_text_d);
					char analysis[256];
					_snprintf_s(analysis, sizeof(analysis), _TRUNCATE, 
						u8"%s驱动存在缺陷，%s运行并进行某些特定操作时导致系统崩溃", 
						record->caused_by_driver, record->process_name);
					nk_lhc(ctx, analysis, NK_TEXT_LEFT, g_color_warning);
				} else if (record->process_name[0] != '\0') {
					nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
					nk_lhc(ctx, u8"原因分析:", NK_TEXT_LEFT, g_color_text_d);
					char analysis[256];
					_snprintf_s(analysis, sizeof(analysis), _TRUNCATE, u8"程序 %s 运行时发生崩溃", record->process_name);
					nk_lhc(ctx, analysis, NK_TEXT_LEFT, g_color_warning);
				} else if (record->caused_by_driver[0] != '\0') {
					nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
					nk_lhc(ctx, u8"原因分析:", NK_TEXT_LEFT, g_color_text_d);
					char analysis[256];
					_snprintf_s(analysis, sizeof(analysis), _TRUNCATE, u8"驱动 %s 导致系统崩溃", record->caused_by_driver);
					nk_lhc(ctx, analysis, NK_TEXT_LEFT, g_color_warning);
				}
				
				const char* diagnosis = gnwinfo_bsod_get_code_diagnosis(record->bugcheck_id);
				if (diagnosis && diagnosis[0] != '\0') {
					nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f });
					nk_lhcf(ctx, NK_TEXT_LEFT, g_color_good, u8"诊断建议: %s", diagnosis);
				}
				
				nk_layout_row(ctx, NK_DYNAMIC, 10, 1, (float[1]) { 1.0f });
				draw_group_separator(ctx);
			}
		}
		goto out;
	}

	if (display_interface == 3)
	{
		nk_layout_row(ctx, NK_STATIC, 25, 1, (float[1]) { 900 });
		nk_lhc(ctx, u8"当前状态: ", NK_TEXT_LEFT, g_color_text_l);
		nk_lhc(ctx, get_optimization_status(), NK_TEXT_LEFT, g_color_good);
		
		nk_layout_row(ctx, NK_STATIC, 10, 1, (float[1]) { 900 });
		
		float panel_width = 180.0f;
		float button_width = 160.0f;
		
		nk_layout_row(ctx, NK_STATIC, 10, 1, (float[1]) { 900 });
		nk_layout_row_begin(ctx, NK_STATIC, 450, 5);
		nk_layout_row_push(ctx, panel_width);
		if (nk_group_begin(ctx, "panel_basic", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
			nk_layout_row(ctx, NK_STATIC, 25, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"基础优化", NK_TEXT_CENTERED, g_color_text_l);
			nk_layout_row(ctx, NK_STATIC, 100, 2, (float[2]) { 40,100 });
			nk_spacing(ctx, 1);
			if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_BASIC), u8"基础优化")) {
				run_powershell_script("run-optimize.ps1 -Level basic ");
			}
			nk_layout_row(ctx, NK_STATIC, 20, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"1. 禁用遥测", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 20, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"2. 禁用隐私收集", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 20, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"3. 移除预装软件", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 20, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"4. 清除本地GPO", NK_TEXT_LEFT, g_color_text_d);
			nk_group_end(ctx);
		}
		nk_layout_row_push(ctx, panel_width);
		if (nk_group_begin(ctx, "panel_deep", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
			nk_layout_row(ctx, NK_STATIC, 25, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"深度优化", NK_TEXT_CENTERED, g_color_text_l);
			nk_layout_row(ctx, NK_STATIC, 100, 2, (float[2]) { 40, 100 });
			nk_spacing(ctx, 1);
			if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_DEEP), u8"深度优化")) {
				run_powershell_script("run-optimize.ps1 -Level deep");
			}
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"1. 禁用遥测", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"2. 禁用隐私收集", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"3. 移除预装软件", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"4. 清除本地GPO", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"5. 启用防火墙", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"6. 配置Defender", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"7. 禁用SMBv1", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"8. SSL/TLS加固", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"9. 启用安全缓解", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"10. 更新管理", NK_TEXT_LEFT, g_color_text_d);
			nk_group_end(ctx);
		}
		nk_layout_row_push(ctx, panel_width);
		if (nk_group_begin(ctx, "panel_full", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
			nk_layout_row(ctx, NK_STATIC, 25, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"完全优化", NK_TEXT_CENTERED, g_color_text_l);
			nk_layout_row(ctx, NK_STATIC, 100, 2, (float[2]) { 40, 100 });
			nk_spacing(ctx, 1);
			if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_FULL), u8"完全优化")) {
				run_powershell_script("run-optimize.ps1 -Level full");
			}
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"1. 禁用遥测", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"2. 禁用隐私收集", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"3. 移除预装软件", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"4. 清除本地GPO", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"5. 启用防火墙", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"6. 配置Defender", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"7. 禁用SMBv1", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"8. SSL/TLS加固", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"9. 启用安全缓解", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"10. 更新管理", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"11. PowerShell加固", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 18, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"12. Defender加固", NK_TEXT_LEFT, g_color_text_d);
			nk_group_end(ctx);
		}
		nk_layout_row_push(ctx, panel_width);
		if (nk_group_begin(ctx, "panel_customize", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
			nk_layout_row(ctx, NK_STATIC, 30, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"深度定制", NK_TEXT_CENTERED, g_color_text_l);
			nk_layout_row(ctx, NK_STATIC, 100, 2, (float[2]) { 40, 100 });
			nk_spacing(ctx, 1);
			if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_CUSTOMIZE), u8"深度定制")) {
				g_ctx.window_flag |= GUI_WINDOW_CUSTOMIZE;
			}
			nk_layout_row(ctx, NK_STATIC, 30, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"自定义12项", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 30, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"按需优化", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 30, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"常见病毒抵制", NK_TEXT_LEFT, g_color_warning);
			nk_group_end(ctx);
		}
		nk_layout_row_push(ctx, panel_width);
		if (nk_group_begin(ctx, "panel_undo", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
			nk_layout_row(ctx, NK_STATIC, 30, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"优化还原", NK_TEXT_CENTERED, g_color_text_l);
			nk_layout_row(ctx, NK_STATIC, 100, 2, (float[2]) { 40, 100 });
			nk_spacing(ctx, 1);
			if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_CANCEL), u8"优化还原")) {
				run_powershell_script("undo-optimize.ps1 -Level prev");
			}
			nk_layout_row(ctx, NK_STATIC, 30, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"回退上一版本", NK_TEXT_LEFT, g_color_text_d);
			nk_layout_row(ctx, NK_STATIC, 30, 1, (float[1]) { 180 });
			nk_lhc(ctx, u8"恢复设置", NK_TEXT_LEFT, g_color_text_d);
			nk_group_end(ctx);
		}
		nk_layout_row_end(ctx);
		
		goto out;
	}

	if (g_ctx.main_flag & MAIN_INFO_OS)
	{
		draw_os(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_BIOS)
	{
		draw_bios(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_BOARD)
	{
		draw_computer(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_CPU)
	{
		draw_processor(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_MEMORY)
	{
		draw_memory(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_MONITOR)
	{
		draw_display(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_STORAGE)
	{
		draw_storage(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_HEAT_DISSIPATION)
	{
		 draw_heat_dissipation(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_NETWORK)
	{
		draw_network(ctx);
		draw_group_separator(ctx);
	}
	if (g_ctx.main_flag & MAIN_INFO_AUDIO)
	{
		draw_audio(ctx);
		draw_group_separator(ctx);
	}
	draw_pci_simple(ctx);

out:
	if (g_hw_has_diff == nk_true)
	{
		if (g_ctx.disk && g_ctx.cpuid && g_ctx.pci)
		{
			printf("DEBUG: Hardware difference detected, saving new JSON file (disk=%p, spd=%p, edid=%p, cpuid=%p, pci=%p)\n",
				g_ctx.disk, g_ctx.spd, g_ctx.edid, g_ctx.cpuid, g_ctx.pci);
			gnwinfo_save_hw_config();
			g_hw_has_diff = 2;
		}
		else
		{
			printf("DEBUG: Hardware data not ready yet, skipping save (disk=%p, spd=%p, edid=%p, cpuid=%p, pci=%p)\n",
				g_ctx.disk, g_ctx.spd, g_ctx.edid, g_ctx.cpuid, g_ctx.pci);
			g_hw_has_diff = nk_false;
		}
	}
		nk_group_end(ctx);
	}
	{
		struct nk_window* win = ctx->current;
		if (win) {
			struct nk_rect bounds;
			nk_widget(&bounds, ctx);
			struct nk_color gray_color = { 0x70, 0x70, 0x70, 0xFF };
			nk_stroke_line(&win->buffer, 0, height - 3, width, height - 3, 20, gray_color);
		}
	}
	nk_end(ctx);
}
