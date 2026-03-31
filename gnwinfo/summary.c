// SPDX-License-Identifier: Unlicense

#include <windows.h>
#include <shellapi.h>
#include "gnwinfo.h"
#include "gettext.h"
#include "utils.h"

static CHAR m_buf[MAX_PATH];

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
	
	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2, g_ctx.gui_ratio });
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
		nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2, g_ctx.gui_ratio });
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
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
			nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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

	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2, g_ctx.gui_ratio });
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
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_BATTERY), NULL))
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

		nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2,g_ctx.gui_ratio });
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
		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
	int max_count = current_device_count > saved_device_count ? current_device_count : saved_device_count;

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

		LPCSTR saved_ddr = gnwinfo_hw_compare_get_smbios_attr_by_index(17, device_index, "Device Type");
		LPCSTR saved_speed = gnwinfo_hw_compare_get_smbios_attr_by_index(17, device_index, "Speed (MT/s)");
		LPCSTR saved_size = gnwinfo_hw_compare_get_smbios_attr_by_index(17, device_index, "Device Size");
		LPCSTR saved_manuf = gnwinfo_hw_compare_get_smbios_attr_by_index(17, device_index, "Manufacturer");
		LPCSTR saved_serial = gnwinfo_hw_compare_get_smbios_attr_by_index(17, device_index, "Serial Number");

		nk_bool is_new = gnwinfo_hw_compare_available() && !saved_ddr;

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

	if (gnwinfo_hw_compare_available() && saved_device_count > current_device_count)
	{
		for (int i = current_device_count; i < saved_device_count; i++)
		{
			LPCSTR saved_ddr = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Device Type");
			LPCSTR saved_speed = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Speed (MT/s)");
			LPCSTR saved_size = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Device Size");
			LPCSTR saved_manuf = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Manufacturer");
			LPCSTR saved_serial = gnwinfo_hw_compare_get_smbios_attr_by_index(17, i, "Serial Number");

			if (saved_ddr)
			{
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
		nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2, g_ctx.gui_ratio });
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
draw_display(struct nk_context* ctx)
{
	INT i;

	nk_layout_row(ctx, NK_DYNAMIC, 0, 4, (float[4]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2, g_ctx.gui_ratio });
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

				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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

		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });

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
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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

		nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
			nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
			nk_spacer(ctx);

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

			nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
				nk_layout_row(ctx, NK_DYNAMIC, 0, 3, (float[3]) { 0.2f, 0.4f - g_ctx.gui_ratio/2, 0.4f - g_ctx.gui_ratio/2 });
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
	if (quick_access_button(ctx, GET_PNG(IDR_PNG_SETTINGS), NULL))
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

static int display_interface = 0;
VOID
gnwinfo_draw_main_window(struct nk_context* ctx, float width, float height)
{
	struct nk_window* win;
	const char* title = "NWinfo GUI";
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
	if (!nk_begin_ex(ctx, "NWinfo GUI",
		nk_rect(0, 0, width, height),
		g_bginfo ? NK_WINDOW_BACKGROUND : (NK_WINDOW_BACKGROUND | NK_WINDOW_CLOSABLE | NK_WINDOW_TITLE),
		nk_image_id(0), GET_PNG(IDR_PNG_CLOSE)))
	{
		nk_end(ctx);
		printf("DEBUG: Window closing, setting was_hidden = TRUE\n");
		g_window_was_hidden = nk_true;
		//InterlockedExchange(&g_ctx.exit_pending, 1);
		ShowWindow(g_ctx.wnd, SW_HIDE);
		return;
	}

	nk_layout_row_begin(ctx, NK_DYNAMIC, 0, 6);

	struct nk_rect rect = nk_layout_widget_bounds(ctx);
	g_ctx.gui_ratio = rect.h / rect.w;

	nk_layout_row_push(ctx, g_ctx.gui_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_SENSOR), N_(N__SENSOR_VIEW)))
		display_interface = 0;
	nk_layout_row_push(ctx, g_ctx.gui_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_SETTINGS), N_(N__SETTINGS)))
		g_ctx.window_flag |= GUI_WINDOW_SETTINGS;
	nk_layout_row_push(ctx, g_ctx.gui_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_REFRESH), N_(N__REFRESH)))
	{
		gnwinfo_ctx_update(IDT_TIMER_1M);
		gnwinfo_ctx_update(IDT_TIMER_DISK);
		gnwinfo_ctx_update(IDT_TIMER_DISPLAY);
		gnwinfo_ctx_update(IDT_TIMER_SMB);
	}
	nk_layout_row_push(ctx, g_ctx.gui_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_INFO), N_(N__ABOUT)))
		g_ctx.window_flag |= GUI_WINDOW_ABOUT;
	nk_layout_row_push(ctx, g_ctx.gui_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_PCI), u8"测试"))
		display_interface = 1;
	nk_layout_row_push(ctx, g_ctx.gui_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_PCI), u8"蓝屏"))
		display_interface = 2;
	nk_layout_row_push(ctx, g_ctx.gui_ratio);
	if (nk_button_image_hover(ctx, GET_PNG(IDR_PNG_CLOSE), N_(N__CLOSE)))
		InterlockedExchange(&g_ctx.exit_pending, 1);
	nk_layout_row_end(ctx);

	if (display_interface == 1)
	{
		static int selected_csv = 0;
		gnwinfo_load_smart_history();
		int csv_count = gnwinfo_get_smart_history_count();
		
		if (csv_count == 0) {
			nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f });
			nk_lhc(ctx, u8"没有找到SMART历史数据文件", NK_TEXT_LEFT, g_color_text_l);
		} else {
			for (int i = 0; i < csv_count; i++) {
				if (i % 4 == 0) {
					nk_layout_row(ctx, NK_DYNAMIC, 30, 4, (float[4]) { 0.25f, 0.25f, 0.25f, 0.25f });
				}
				const char* filename = gnwinfo_get_smart_history_filename(i);
				if (filename) {
					const char* basename = strrchr(filename, '\\');
					if (basename) basename++;
					else basename = filename;
					
					char display_name[64] = {0};
					const char* underscore = strchr(basename, '_');
					if (underscore) {
						underscore++;
						const char* diskdata = strstr(underscore, "_diskdata");
						if (diskdata) {
							int len = (int)(diskdata - underscore);
							if (len > 0 && len < 64) {
								strncpy_s(display_name, sizeof(display_name), underscore, len);
							}
						}
					}
					if (display_name[0] == '\0') {
						strcpy_s(display_name, sizeof(display_name), basename);
					}
					
					if (nk_button_label(ctx, display_name)) {
						selected_csv = i;
					}
				}
			}
			
			nk_layout_row(ctx, NK_DYNAMIC, 10, 1, (float[1]) { 1.0f });
			
			int rows = gnwinfo_get_smart_history_rows(selected_csv);
			int cols = gnwinfo_get_smart_history_cols(selected_csv);
			
			if (rows > 0 && cols > 0) {
				float col_width = 280.0f;
				float time_width = 180.0f;
				
				nk_layout_row_begin(ctx, NK_STATIC, 0, cols);
				for (int c = 0; c < cols; c++) {
					float width = (c == 0) ? time_width : col_width;
					nk_layout_row_push(ctx, width);
					const char* cell = gnwinfo_get_smart_history_cell(selected_csv, 0, c);
					if (cell)
						nk_lhc(ctx, cell, NK_TEXT_LEFT, g_color_text_d);
					else
						nk_lhc(ctx, "", NK_TEXT_LEFT, g_color_text_d);
				}
				nk_layout_row_end(ctx);
				
				for (int r = rows - 1; r >= 1; r--) {
					nk_layout_row_begin(ctx, NK_STATIC, 0, cols);
					for (int c = 0; c < cols; c++) {
						float width = (c == 0) ? time_width : col_width;
						nk_layout_row_push(ctx, width);
						const char* cell = gnwinfo_get_smart_history_cell(selected_csv, r, c);
						if (cell)
							nk_lhc(ctx, cell, NK_TEXT_LEFT, g_color_text_l);
						else
							nk_lhc(ctx, "", NK_TEXT_LEFT, g_color_text_l);
					}
					nk_layout_row_end(ctx);
				}
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
				
				nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
				nk_lhc(ctx, u8"描述:", NK_TEXT_LEFT, g_color_text_d);
				nk_lhc(ctx, gnwinfo_bsod_get_code_desc(record->bugcheck_id), NK_TEXT_LEFT, g_color_text_l);
				
				if (record->dump_file[0] != '\0') {
					nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.2f, 0.8f });
					nk_lhc(ctx, u8"转储文件:", NK_TEXT_LEFT, g_color_text_d);
					const char* basename = strrchr(record->dump_file, '\\');
					if (basename) basename++;
					else basename = record->dump_file;
					nk_lhc(ctx, basename, NK_TEXT_LEFT, g_color_text_l);
				}
				
				nk_layout_row(ctx, NK_DYNAMIC, 10, 1, (float[1]) { 1.0f });
			}
		}
		goto out;
	}

	if (g_ctx.main_flag & MAIN_INFO_OS)
		draw_os(ctx);
	if (g_ctx.main_flag & MAIN_INFO_BIOS)
		draw_bios(ctx);
	if (g_ctx.main_flag & MAIN_INFO_BOARD)
		draw_computer(ctx);
	if (g_ctx.main_flag & MAIN_INFO_CPU)
		draw_processor(ctx);
	if (g_ctx.main_flag & MAIN_INFO_MEMORY)
		draw_memory(ctx);
	if (g_ctx.main_flag & MAIN_INFO_MONITOR)
		draw_display(ctx);
	if (g_ctx.main_flag & MAIN_INFO_STORAGE)
		draw_storage(ctx);
	if (g_ctx.main_flag & MAIN_INFO_NETWORK)
		draw_network(ctx);
	if (g_ctx.main_flag & MAIN_INFO_AUDIO)
		draw_audio(ctx);
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
	nk_end(ctx);
}
