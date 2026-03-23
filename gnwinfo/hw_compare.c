#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gnwinfo.h"
#include "cJSON.h"
#include "utils.h"

static cJSON* g_hw_json = NULL;
static WCHAR g_hw_json_path[MAX_PATH] = {0};

void gnwinfo_hw_compare_init(void)
{
	WCHAR search_path[MAX_PATH];
	WIN32_FIND_DATAW find_data;
	HANDLE hFind;
	FILETIME latest_time = {0};
	WCHAR latest_file[MAX_PATH] = {0};

	GetModuleFileNameW(NULL, search_path, MAX_PATH);
	WCHAR* last_slash = wcsrchr(search_path, L'\\');
	if (last_slash)
		*(last_slash + 1) = L'\0';
	else
		return;

	wcscpy_s(g_hw_json_path, MAX_PATH, search_path);
	wcscat_s(search_path, MAX_PATH, L"hw_config_*.json");

	hFind = FindFirstFileW(search_path, &find_data);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		gnwinfo_save_hw_config();
		wcscpy_s(search_path, MAX_PATH, g_hw_json_path);
		wcscat_s(search_path, MAX_PATH, L"hw_config_*.json");
		hFind = FindFirstFileW(search_path, &find_data);
		if (hFind == INVALID_HANDLE_VALUE)
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
				}

				free(json_buffer);
			}
			fclose(fp);
		}
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

nk_bool gnwinfo_hw_compare_available(void)
{
	return g_hw_json != NULL;
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

LPCSTR gnwinfo_hw_compare_get_nested_string(LPCSTR node_name, LPCSTR child_name, int index, LPCSTR attr_name)
{
	if (!g_hw_json)
		return NULL;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return NULL;

	cJSON* child_array = cJSON_GetObjectItem(node, child_name);
	if (!child_array || !(child_array->type & cJSON_Array))
		return NULL;

	cJSON* child = cJSON_GetArrayItem(child_array, index);
	if (!child)
		return NULL;

	cJSON* attr = cJSON_GetObjectItem(child, attr_name);
	if (!attr || !(attr->type & cJSON_String))
		return NULL;

	return attr->valuestring;
}

int gnwinfo_hw_compare_get_array_size(LPCSTR node_name, LPCSTR child_name)
{
	if (!g_hw_json)
		return 0;

	cJSON* node = cJSON_GetObjectItem(g_hw_json, node_name);
	if (!node)
		return 0;

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
	return strcmp(current_value, saved_value) != 0;
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
			if (attr && (attr->type & cJSON_String))
				return attr->valuestring;
			return NULL;
		}
	}
	return NULL;
}

LPCWSTR gnwinfo_hw_compare_get_path(void)
{
	return g_hw_json_path;
}
