#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gnwinfo.h"
#include "cJSON.h"
#include "utils.h"

static cJSON* g_hw_json = NULL;
static WCHAR g_hw_json_path[MAX_PATH] = {0};
static char g_num_buf[16][32];
static int g_num_buf_index = 0;
static nk_bool g_need_initial_save = nk_false;

static char* get_num_buf(void)
{
	char* buf = g_num_buf[g_num_buf_index % 16];
	g_num_buf_index++;
	return buf;
}

nk_bool gnwinfo_hw_compare_need_initial_save(void)
{
	return g_need_initial_save;
}

void gnwinfo_hw_compare_init(void)
{
	WCHAR search_path[MAX_PATH];
	WIN32_FIND_DATAW find_data;
	HANDLE hFind;
	FILETIME latest_time = {0};
	WCHAR latest_file[MAX_PATH] = {0};

	if (GetEnvironmentVariableW(L"USERPROFILE", search_path, MAX_PATH) > 0)
	{
		wcscat_s(search_path, MAX_PATH, L"\\herosys_data");
	}
	else
	{
		GetModuleFileNameW(NULL, search_path, MAX_PATH);
		WCHAR* last_slash = wcsrchr(search_path, L'\\');
		if (last_slash)
			*(last_slash + 1) = L'\0';
		wcscat_s(search_path, MAX_PATH, L"herosys_data");
	}
	CreateDirectoryW(search_path, NULL);
	wcscat_s(search_path, MAX_PATH, L"\\");

	wcscpy_s(g_hw_json_path, MAX_PATH, search_path);
	wcscat_s(search_path, MAX_PATH, L"hw_config_*.json");

	hFind = FindFirstFileW(search_path, &find_data);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		g_need_initial_save = nk_true;
		return;
	}

	do
	{
		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			if (CompareFileTime(&find_data.ftLastWriteTime, &latest_time) > 0)
			{
				latest_time = find_data.ftLastWriteTime;
				wcscpy_s(latest_file, MAX_PATH, find_data.cFileName);
			}
		}
	} while (FindNextFileW(hFind, &find_data) != 0);

	FindClose(hFind);

	if (latest_file[0] != L'\0')
	{
		WCHAR full_path[MAX_PATH];
		wcscpy_s(full_path, MAX_PATH, g_hw_json_path);
		wcscat_s(full_path, MAX_PATH, latest_file);

		FILE* fp = NULL;
		if (_wfopen_s(&fp, full_path, L"rb") == 0 && fp)
		{
			fseek(fp, 0, SEEK_END);
			long file_size = ftell(fp);
			fseek(fp, 0, SEEK_SET);

			char* json_buffer = (char*)malloc(file_size + 1);
			if (json_buffer)
			{
				size_t read_size = fread(json_buffer, 1, file_size, fp);
				json_buffer[read_size] = '\0';

				if (g_hw_json)
				{
					cJSON_Delete(g_hw_json);
					g_hw_json = NULL;
				}
				g_hw_json = cJSON_Parse(json_buffer);
				if (g_hw_json)
				{
					wcscpy_s(g_hw_json_path, MAX_PATH, full_path);
					g_need_initial_save = nk_false;
				}
				else
				{
					g_need_initial_save = nk_true;
				}

				free(json_buffer);
			}
			fclose(fp);
		}
		else
		{
			g_need_initial_save = nk_true;
		}
	}
	else
	{
		g_need_initial_save = nk_true;
	}
}

void gnwinfo_hw_compare_fini(void)
{
	if (g_hw_json)
	{
		cJSON_Delete(g_hw_json);
		g_hw_json = NULL;
	}
}

void gnwinfo_hw_compare_reload(void)
{
	WCHAR search_path[MAX_PATH];
	WIN32_FIND_DATAW find_data;
	HANDLE hFind;
	FILETIME latest_time = {0};
	WCHAR latest_file[MAX_PATH] = {0};

	printf("DEBUG: gnwinfo_hw_compare_reload() called\n");

	if (GetEnvironmentVariableW(L"USERPROFILE", search_path, MAX_PATH) > 0)
	{
		wcscat_s(search_path, MAX_PATH, L"\\herosys_data");
	}
	else
	{
		GetModuleFileNameW(NULL, search_path, MAX_PATH);
		WCHAR* last_slash = wcsrchr(search_path, L'\\');
		if (last_slash)
			*(last_slash + 1) = L'\0';
		wcscat_s(search_path, MAX_PATH, L"herosys_data");
	}
	CreateDirectoryW(search_path, NULL);
	wcscat_s(search_path, MAX_PATH, L"\\");

	wcscpy_s(g_hw_json_path, MAX_PATH, search_path);
	wcscat_s(search_path, MAX_PATH, L"hw_config_*.json");

	hFind = FindFirstFileW(search_path, &find_data);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		printf("DEBUG: No JSON files found\n");
		return;
	}

	do
	{
		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			if (CompareFileTime(&find_data.ftLastWriteTime, &latest_time) > 0)
			{
				latest_time = find_data.ftLastWriteTime;
				wcscpy_s(latest_file, MAX_PATH, find_data.cFileName);
			}
		}
	} while (FindNextFileW(hFind, &find_data) != 0);

	FindClose(hFind);

	if (latest_file[0] != L'\0')
	{
		WCHAR full_path[MAX_PATH];
		wcscpy_s(full_path, MAX_PATH, g_hw_json_path);
		wcscat_s(full_path, MAX_PATH, latest_file);

		printf("DEBUG: Found latest JSON file: %S\n", latest_file);

		FILE* fp = NULL;
		if (_wfopen_s(&fp, full_path, L"rb") == 0 && fp)
		{
			fseek(fp, 0, SEEK_END);
			long file_size = ftell(fp);
			fseek(fp, 0, SEEK_SET);

			char* json_buffer = (char*)malloc(file_size + 1);
			if (json_buffer)
			{
				size_t read_size = fread(json_buffer, 1, file_size, fp);
				json_buffer[read_size] = '\0';

				if (g_hw_json)
				{
					cJSON_Delete(g_hw_json);
					g_hw_json = NULL;
				}
				g_hw_json = cJSON_Parse(json_buffer);
				if (g_hw_json)
				{
					wcscpy_s(g_hw_json_path, MAX_PATH, full_path);
					printf("DEBUG: Reloaded JSON file: %S\n", latest_file);
				}
				else
				{
					printf("DEBUG: Failed to parse JSON file: %S\n", latest_file);
				}

				free(json_buffer);
			}
			fclose(fp);
		}
	}
}

nk_bool gnwinfo_hw_compare_available(void)
{
	return g_hw_json != NULL;
}

LPCSTR gnwinfo_hw_compare_get_nested_string(LPCSTR node_name, LPCSTR sub_name, LPCSTR attr_name)
{
	if (!g_hw_json)
		return NULL;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return NULL;

	cJSON* sub = sub_name ? cJSON_GetObjectItem(node, sub_name) : node;
	if (!sub)
		return NULL;

	cJSON* attr = cJSON_GetObjectItem(sub, attr_name);
	if (!attr)
		return NULL;

	if (attr->type & cJSON_String)
		return attr->valuestring;
	if (attr->type & cJSON_Number)
	{
		char* buf = get_num_buf();
		if (attr->valueint == (int)attr->valuedouble)
			snprintf(buf, 32, "%d", attr->valueint);
		else
			snprintf(buf, 32, "%.2f", attr->valuedouble);
		return buf;
	}

	return NULL;
}

LPCSTR gnwinfo_hw_compare_get_deep_nested_string(LPCSTR node_name, LPCSTR sub_name1, LPCSTR sub_name2, LPCSTR attr_name)
{
	if (!g_hw_json)
		return NULL;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return NULL;

	cJSON* sub1 = sub_name1 ? cJSON_GetObjectItem(node, sub_name1) : node;
	if (!sub1)
		return NULL;

	cJSON* sub2 = sub_name2 ? cJSON_GetObjectItem(sub1, sub_name2) : sub1;
	if (!sub2)
		return NULL;

	cJSON* attr = cJSON_GetObjectItem(sub2, attr_name);
	if (!attr)
		return NULL;

	if (attr->type & cJSON_String)
		return attr->valuestring;
	if (attr->type & cJSON_Number)
	{
		char* buf = get_num_buf();
		if (attr->valueint == (int)attr->valuedouble)
			snprintf(buf, 32, "%d", attr->valueint);
		else
			snprintf(buf, 32, "%.2f", attr->valuedouble);
		return buf;
	}

	return NULL;
}

LPCSTR gnwinfo_hw_compare_get_string(LPCSTR node_name, LPCSTR attr_name)
{
	if (!g_hw_json)
		return NULL;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return NULL;

	cJSON* attr = cJSON_GetObjectItem(node, attr_name);
	if (!attr || !(attr->type & cJSON_String))
		return NULL;

	return attr->valuestring;
}

nk_bool gnwinfo_hw_compare_get_bool(LPCSTR node_name, LPCSTR attr_name)
{
	if (!g_hw_json)
		return nk_false;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return nk_false;

	cJSON* attr = cJSON_GetObjectItem(node, attr_name);
	if (!attr)
		return nk_false;

	if (attr->type & cJSON_True)
		return nk_true;
	if (attr->type & cJSON_False)
		return nk_false;

	return nk_false;
}

LPCSTR gnwinfo_hw_compare_get_array_item(LPCSTR node_name, LPCSTR child_name, int index, LPCSTR attr_name)
{
	if (!g_hw_json)
		return NULL;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return NULL;

	cJSON* child = NULL;
	if (child_name == NULL)
	{
		if (!(node->type & cJSON_Array))
			return NULL;
		child = cJSON_GetArrayItem(node, index);
	}
	else
	{
		cJSON* child_array = cJSON_GetObjectItem(node, child_name);
		if (!child_array || !(child_array->type & cJSON_Array))
			return NULL;
		child = cJSON_GetArrayItem(child_array, index);
	}

	if (!child)
		return NULL;

	cJSON* attr = cJSON_GetObjectItem(child, attr_name);
	if (!attr)
		return NULL;

	if (attr->type & cJSON_String)
		return attr->valuestring;
	if (attr->type & cJSON_Number)
	{
		char* buf = get_num_buf();
		if (attr->valueint == (int)attr->valuedouble)
			snprintf(buf, 32, "%d", attr->valueint);
		else
			snprintf(buf, 32, "%.2f", attr->valuedouble);
		return buf;
	}
	return NULL;
}

LPCSTR gnwinfo_hw_compare_get_display_item(LPCSTR node_name, LPCSTR child_name, int index, LPCSTR attr_name)
{
	if (!g_hw_json)
		return NULL;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return NULL;

	cJSON* child = NULL;
	if (child_name == NULL)
	{
		if (!(node->type & cJSON_Array))
			return NULL;
		child = cJSON_GetArrayItem(node, index);
	}
	else
	{
		cJSON* child_array = cJSON_GetObjectItem(node, child_name);
		if (!child_array || !(child_array->type & cJSON_Array))
			return NULL;
		child = cJSON_GetArrayItem(child_array, index);
	}

	if (!child)
		return NULL;

	cJSON* attr = cJSON_GetObjectItem(child, attr_name);
	if (!attr)
		return NULL;

	if (attr->type & cJSON_String)
		return attr->valuestring;
	if (attr->type & cJSON_Number)
	{
		char* buf = get_num_buf();
		snprintf(buf, 32, "%.2f", attr->valuedouble);
		return buf;
	}
	return NULL;
}

int gnwinfo_hw_compare_get_array_size(LPCSTR node_name, LPCSTR child_name)
{
	if (!g_hw_json)
		return 0;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return 0;

	if (child_name == NULL)
	{
		if (node->type & cJSON_Array)
			return cJSON_GetArraySize(node);
		return 0;
	}

	cJSON* child_array = cJSON_GetObjectItem(node, child_name);
	if (!child_array || !(child_array->type & cJSON_Array))
		return 0;

	return cJSON_GetArraySize(child_array);
}

nk_bool gnwinfo_hw_compare_is_different(LPCSTR current_value, LPCSTR saved_value)
{
	if (!saved_value)
		return nk_false;
	if (!current_value)
		return nk_true;
	if (saved_value[0] == '\0' || strcmp(saved_value, "-") == 0)
		return nk_false;
	if (current_value[0] == '\0' || strcmp(current_value, "-") == 0)
		return nk_false;
	nk_bool is_diff = strcmp(current_value, saved_value) != 0;
	if (is_diff)
	{
		if(g_hw_has_diff == nk_false)
		g_hw_has_diff = nk_true;
	}
		
	return is_diff;
}

LPCSTR gnwinfo_hw_compare_get_smbios_attr(int table_type, LPCSTR attr_name)
{
	if (!g_hw_json)
		return NULL;

	cJSON* smbios = cJSON_GetObjectItem(g_hw_json, "SMBIOS");
	if (!smbios || !(smbios->type & cJSON_Array))
		return NULL;

	int size = cJSON_GetArraySize(smbios);
	for (int i = 0; i < size; i++)
	{
		cJSON* item = cJSON_GetArrayItem(smbios, i);
		if (!item)
			continue;

		cJSON* type = cJSON_GetObjectItem(item, "Table Type");
		if (type && (type->type & cJSON_Number) && type->valueint == table_type)
		{
			cJSON* attr = cJSON_GetObjectItem(item, attr_name);
			if (!attr)
				return NULL;

			if (attr->type & cJSON_String)
				return attr->valuestring;
			if (attr->type & cJSON_Number)
			{
				char* buf = get_num_buf();
				if (attr->valueint == (int)attr->valuedouble)
					snprintf(buf, 32, "%d", attr->valueint);
				else
					snprintf(buf, 32, "%.2f", attr->valuedouble);
				return buf;
			}
			return NULL;
		}
	}
	return NULL;
}

LPCSTR gnwinfo_hw_compare_get_smbios_attr_by_index(int table_type, int index, LPCSTR attr_name)
{
	if (!g_hw_json)
		return NULL;

	cJSON* smbios = cJSON_GetObjectItem(g_hw_json, "SMBIOS");
	if (!smbios || !(smbios->type & cJSON_Array))
		return NULL;

	int size = cJSON_GetArraySize(smbios);
	int match_count = 0;
	for (int i = 0; i < size; i++)
	{
		cJSON* item = cJSON_GetArrayItem(smbios, i);
		if (!item)
			continue;

		cJSON* type = cJSON_GetObjectItem(item, "Table Type");
		if (type && (type->type & cJSON_Number) && type->valueint == table_type)
		{
			if (match_count == index)
			{
				cJSON* attr = cJSON_GetObjectItem(item, attr_name);
				if (!attr)
					return NULL;

				if (attr->type & cJSON_String)
					return attr->valuestring;
				if (attr->type & cJSON_Number)
				{
					char* buf = get_num_buf();
					if (attr->valueint == (int)attr->valuedouble)
						snprintf(buf, 32, "%d", attr->valueint);
					else
						snprintf(buf, 32, "%.2f", attr->valuedouble);
					return buf;
				}
				return NULL;
			}
			match_count++;
		}
	}
	return NULL;
}

int gnwinfo_hw_compare_get_smbios_count(int table_type)
{
	if (!g_hw_json)
		return 0;

	cJSON* smbios = cJSON_GetObjectItem(g_hw_json, "SMBIOS");
	if (!smbios || !(smbios->type & cJSON_Array))
		return 0;

	int count = 0;
	int size = cJSON_GetArraySize(smbios);
	for (int i = 0; i < size; i++)
	{
		cJSON* item = cJSON_GetArrayItem(smbios, i);
		if (!item)
			continue;

		cJSON* type = cJSON_GetObjectItem(item, "Table Type");
		if (type && (type->type & cJSON_Number) && type->valueint == table_type)
			count++;
	}
	return count;
}

nk_bool gnwinfo_hw_compare_smbios_serial_exists(int table_type, LPCSTR serial)
{
	if (!g_hw_json || !serial || serial[0] == '\0' || serial[0] == '-')
		return nk_false;

	cJSON* smbios = cJSON_GetObjectItem(g_hw_json, "SMBIOS");
	if (!smbios || !(smbios->type & cJSON_Array))
		return nk_false;

	int size = cJSON_GetArraySize(smbios);
	for (int i = 0; i < size; i++)
	{
		cJSON* item = cJSON_GetArrayItem(smbios, i);
		if (!item)
			continue;

		cJSON* type = cJSON_GetObjectItem(item, "Table Type");
		if (type && (type->type & cJSON_Number) && type->valueint == table_type)
		{
			cJSON* serial_attr = cJSON_GetObjectItem(item, "Serial Number");
			if (serial_attr && (serial_attr->type & cJSON_String))
			{
				if (strcmp(serial_attr->valuestring, serial) == 0)
					return nk_true;
			}
		}
	}
	return nk_false;
}

LPCSTR gnwinfo_hw_compare_get_smbios_attr_by_serial(int table_type, LPCSTR serial, LPCSTR attr_name)
{
	if (!g_hw_json || !serial || serial[0] == '\0' || serial[0] == '-')
		return NULL;

	cJSON* smbios = cJSON_GetObjectItem(g_hw_json, "SMBIOS");
	if (!smbios || !(smbios->type & cJSON_Array))
		return NULL;

	int size = cJSON_GetArraySize(smbios);
	for (int i = 0; i < size; i++)
	{
		cJSON* item = cJSON_GetArrayItem(smbios, i);
		if (!item)
			continue;

		cJSON* type = cJSON_GetObjectItem(item, "Table Type");
		if (type && (type->type & cJSON_Number) && type->valueint == table_type)
		{
			cJSON* serial_attr = cJSON_GetObjectItem(item, "Serial Number");
			if (serial_attr && (serial_attr->type & cJSON_String))
			{
				if (strcmp(serial_attr->valuestring, serial) == 0)
				{
					cJSON* attr = cJSON_GetObjectItem(item, attr_name);
					if (!attr)
						return NULL;

					if (attr->type & cJSON_String)
						return attr->valuestring;
					if (attr->type & cJSON_Number)
					{
						char* buf = get_num_buf();
						if (attr->valueint == (int)attr->valuedouble)
							snprintf(buf, 32, "%d", attr->valueint);
						else
							snprintf(buf, 32, "%.2f", attr->valuedouble);
						return buf;
					}
					return NULL;
				}
			}
		}
	}
	return NULL;
}

LPCWSTR gnwinfo_hw_compare_get_path(void)
{
	return g_hw_json_path;
}
nk_bool gnwinfo_hw_compare_check_changes(void)
{
	if (!g_hw_json)
	{
		printf("DEBUG: No JSON data available for comparison\n");
		return nk_false;
	}

	nk_bool has_changes = nk_false;

	INT current_cpu_count = g_ctx.cpuid ? NWL_NodeChildCount(g_ctx.cpuid) : 0;
	INT saved_cpu_count = gnwinfo_hw_compare_get_array_size("CPUID", NULL);
	printf("DEBUG: CPU count - current: %d, saved: %d\n", current_cpu_count, saved_cpu_count);
	if (current_cpu_count > 0 && saved_cpu_count > 0 && current_cpu_count != saved_cpu_count)
		has_changes = nk_true;

	INT current_memory_count = g_ctx.spd ? NWL_NodeChildCount(g_ctx.spd) : 0;
	INT saved_memory_count = gnwinfo_hw_compare_get_array_size("SPD", NULL);
	printf("DEBUG: Memory count - current: %d, saved: %d\n", current_memory_count, saved_memory_count);
	if (current_memory_count > 0 && saved_memory_count > 0 && current_memory_count != saved_memory_count)
		has_changes = nk_true;

	INT current_disk_count = g_ctx.disk ? NWL_NodeChildCount(g_ctx.disk) : 0;
	INT saved_disk_count = gnwinfo_hw_compare_get_array_size("Disks", NULL);
	printf("DEBUG: Disk count - current: %d, saved: %d\n", current_disk_count, saved_disk_count);
	if (current_disk_count > 0 && saved_disk_count > 0 && current_disk_count != saved_disk_count)
		has_changes = nk_true;

	INT current_display_count = g_ctx.edid ? NWL_NodeChildCount(g_ctx.edid) : 0;
	INT saved_display_count = gnwinfo_hw_compare_get_array_size("Display", NULL);
	printf("DEBUG: Display count - current: %d, saved: %d\n", current_display_count, saved_display_count);
	if (current_display_count > 0 && saved_display_count > 0 && current_display_count != saved_display_count)
		has_changes = nk_true;

	INT current_pci_count = g_ctx.pci ? NWL_NodeChildCount(g_ctx.pci) : 0;
	INT saved_pci_count = gnwinfo_hw_compare_get_array_size("PCI", NULL);
	printf("DEBUG: PCI count - current: %d, saved: %d\n", current_pci_count, saved_pci_count);
	if (current_pci_count > 0 && saved_pci_count > 0 && current_pci_count != saved_pci_count)
		has_changes = nk_true;

	printf("DEBUG: Has changes: %s\n", has_changes ? "YES" : "NO");
	return has_changes;
}

LPCSTR gnwinfo_hw_compare_get_pci_attr_by_hwid_location(LPCSTR hwid, LPCSTR location, LPCSTR attr_name)
{
	if (!g_hw_json || !hwid)
		return NULL;

	cJSON* pci = cJSON_GetObjectItem(g_hw_json, "PCI");
	if (!pci || !(pci->type & cJSON_Array))
		return NULL;

	int size = cJSON_GetArraySize(pci);
	for (int i = 0; i < size; i++)
	{
		cJSON* item = cJSON_GetArrayItem(pci, i);
		if (!item)
			continue;

		cJSON* item_hwid = cJSON_GetObjectItem(item, "HWID");
		cJSON* item_location = cJSON_GetObjectItem(item, "Location");
		
		if (item_hwid && (item_hwid->type & cJSON_String) && strcmp(item_hwid->valuestring, hwid) == 0)
		{
			if (location && item_location && (item_location->type & cJSON_String))
			{
				if (strcmp(item_location->valuestring, location) != 0)
					continue;
			}
			
			cJSON* attr = cJSON_GetObjectItem(item, attr_name);
			if (!attr)
				return NULL;

			if (attr->type & cJSON_String)
				return attr->valuestring;
			if (attr->type & cJSON_Number)
			{
				char* buf = get_num_buf();
				if (attr->valueint == (int)attr->valuedouble)
					snprintf(buf, 32, "%d", attr->valueint);
				else
					snprintf(buf, 32, "%.2f", attr->valuedouble);
				return buf;
			}
			return NULL;
		}
	}
	return NULL;
}

nk_bool gnwinfo_hw_compare_pci_exists_by_hwid_location(LPCSTR hwid, LPCSTR location)
{
	if (!g_hw_json || !hwid)
		return nk_false;

	cJSON* pci = cJSON_GetObjectItem(g_hw_json, "PCI");
	if (!pci || !(pci->type & cJSON_Array))
		return nk_false;

	int size = cJSON_GetArraySize(pci);
	for (int i = 0; i < size; i++)
	{
		cJSON* item = cJSON_GetArrayItem(pci, i);
		if (!item)
			continue;

		cJSON* item_hwid = cJSON_GetObjectItem(item, "HWID");
		cJSON* item_location = cJSON_GetObjectItem(item, "Location");
		
		if (item_hwid && (item_hwid->type & cJSON_String) && strcmp(item_hwid->valuestring, hwid) == 0)
		{
			if (location && item_location && (item_location->type & cJSON_String))
			{
				if (strcmp(item_location->valuestring, location) == 0)
					return nk_true;
			}
			else if (!location && !item_location)
			{
				return nk_true;
			}
		}
	}
	return nk_false;
}

int gnwinfo_hw_compare_get_pci_count(void)
{
	if (!g_hw_json)
		return 0;

	cJSON* pci = cJSON_GetObjectItem(g_hw_json, "PCI");
	if (!pci || !(pci->type & cJSON_Array))
		return 0;

	return cJSON_GetArraySize(pci);
}

void gnwinfo_hw_compare_get_pci_removed_devices(void (*callback)(LPCSTR hwid, LPCSTR location, LPCSTR desc, void* userdata), void* userdata)
{
	if (!g_hw_json || !callback)
		return;

	cJSON* pci = cJSON_GetObjectItem(g_hw_json, "PCI");
	if (!pci || !(pci->type & cJSON_Array))
		return;

	int size = cJSON_GetArraySize(pci);
	for (int i = 0; i < size; i++)
	{
		cJSON* item = cJSON_GetArrayItem(pci, i);
		if (!item)
			continue;

		cJSON* item_hwid = cJSON_GetObjectItem(item, "HWID");
		if (!item_hwid || !(item_hwid->type & cJSON_String))
			continue;

		LPCSTR hwid = item_hwid->valuestring;
		cJSON* item_location = cJSON_GetObjectItem(item, "Location");
		LPCSTR location = (item_location && (item_location->type & cJSON_String)) ? item_location->valuestring : NULL;
		
		nk_bool found = nk_false;
		INT current_count = NWL_NodeChildCount(g_ctx.pci);
		for (INT j = 0; j < current_count; j++)
		{
			PNODE pci_node = NWL_NodeEnumChild(g_ctx.pci, j);
			if (pci_node)
			{
				LPCSTR current_hwid = NWL_NodeAttrGet(pci_node, "HWID");
				LPCSTR current_location = NWL_NodeAttrGet(pci_node, "Location");
				
				if (current_hwid && strcmp(current_hwid, hwid) == 0)
				{
					if (location && current_location)
					{
						if (strcmp(current_location, location) == 0)
						{
							found = nk_true;
							break;
						}
					}
					else if (!location && !current_location)
					{
						found = nk_true;
						break;
					}
				}
			}
		}

		if (!found)
		{
			cJSON* desc = cJSON_GetObjectItem(item, "Description");
			LPCSTR desc_str = (desc && (desc->type & cJSON_String)) ? desc->valuestring : "-";
			callback(hwid, location, desc_str, userdata);
		}
	}
}
