// SPDX-License-Identifier: Unlicense

#include "gnwinfo.h"

static int tree_id = 0;

static nk_bool check_pci_node_changed(PNODE node)
{
	if (!node || !node->attributes)
		return nk_false;

	LPCSTR hwid = NWL_NodeAttrGet(node, "HWID");
	LPCSTR location = NWL_NodeAttrGet(node, "Location");
	
	if (!gnwinfo_hw_compare_available() || !gnwinfo_hw_compare_pci_exists_by_hwid_location(hwid, location))
		return nk_false;

	INT count = NWL_NodeAttrCount(node);
	for (INT i = 0; i < count; i++)
	{
		PNODE_ATT att = NWL_NodeAttrEnum(node, i);
		if (!att || strcmp(att->key, "HWID") == 0)
			continue;
		
		LPCSTR saved_value = gnwinfo_hw_compare_get_pci_attr_by_hwid_location(hwid, location, att->key);
		if (saved_value && gnwinfo_hw_compare_is_different(att->value, saved_value))
			return nk_true;
	}
	return nk_false;
}

static nk_bool draw_pci_node_compare(struct nk_context* ctx, PNODE node, nk_bool is_new, struct nk_color node_color)
{
	nk_bool has_change = nk_false;
	
	if (!node || !node->attributes)
		return nk_false;

	LPCSTR hwid = NWL_NodeAttrGet(node, "HWID");
	LPCSTR location = NWL_NodeAttrGet(node, "Location");
	
	if (nk_tree_image_push_id_color(ctx, NK_TREE_TAB,
		GET_PNG(IDR_PNG_PCI),
		hwid,
		NK_MINIMIZED, tree_id++, node_color))
	{
		const float ratio[] = { 0.2f, 0.8f };
		INT count = NWL_NodeAttrCount(node);
		nk_layout_row(ctx, NK_DYNAMIC, 0, 2, ratio);
		for (INT i = 0; i < count; i++)
		{
			PNODE_ATT att = NWL_NodeAttrEnum(node, i);
			if (!att || strcmp(att->key, "HWID") == 0)
				continue;
			nk_l(ctx, att->key, NK_TEXT_LEFT);
			
			if (is_new)
			{
				nk_lhc(ctx, att->value, NK_TEXT_RIGHT, g_color_good);
				has_change = nk_true;
			}
			else
			{
				LPCSTR saved_value = gnwinfo_hw_compare_get_pci_attr_by_hwid_location(hwid, location, att->key);
				if (saved_value && gnwinfo_hw_compare_is_different(att->value, saved_value))
				{
					nk_lhc(ctx, att->value, NK_TEXT_RIGHT, g_color_error);
					has_change = nk_true;
				}
				else
				{
					nk_lhc(ctx, att->value, NK_TEXT_RIGHT, g_color_text_l);
				}
			}
		}
		nk_tree_pop(ctx);
	}
	return has_change;
}

static nk_bool draw_pci_class_compare(struct nk_context* ctx, const char* title, struct nk_image image, const char* code)
{
	nk_bool class_has_change = nk_false;
	
	if (!g_ctx.pci)
		return nk_false;

	INT count = NWL_NodeChildCount(g_ctx.pci);
	INT class_count = 0;
	
	for (INT i = 0; i < count; i++)
	{
		PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
		const char* cl = NWL_NodeAttrGet(pci, "Class Code");
		if (_strnicmp(cl, code, 2) != 0)
			continue;
		class_count++;
		
		LPCSTR hwid = NWL_NodeAttrGet(pci, "HWID");
		LPCSTR location = NWL_NodeAttrGet(pci, "Location");
		nk_bool is_new = gnwinfo_hw_compare_available() && !gnwinfo_hw_compare_pci_exists_by_hwid_location(hwid, location);
		nk_bool has_changed = check_pci_node_changed(pci);
		
		if (is_new || has_changed)
			class_has_change = nk_true;
	}
	
	if (class_count == 0)
		return nk_false;
	
	struct nk_color class_color = class_has_change ? g_color_error : g_color_text_d;
	
	if (nk_tree_image_push_id_color(ctx, NK_TREE_TAB, image, title, NK_MINIMIZED, tree_id++, class_color))
	{
		for (INT i = 0; i < count; i++)
		{
			PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
			const char* cl = NWL_NodeAttrGet(pci, "Class Code");
			if (_strnicmp(cl, code, 2) != 0)
				continue;
			
			LPCSTR hwid = NWL_NodeAttrGet(pci, "HWID");
			LPCSTR location = NWL_NodeAttrGet(pci, "Location");
			nk_bool is_new = gnwinfo_hw_compare_available() && !gnwinfo_hw_compare_pci_exists_by_hwid_location(hwid, location);
			nk_bool has_changed = check_pci_node_changed(pci);
			
			struct nk_color node_color = (is_new || has_changed) ? g_color_error : g_color_text_d;
			
			if (is_new)
			{
				if(g_hw_has_diff == nk_false)
					g_hw_has_diff = nk_true;
			}
			
			if (draw_pci_node_compare(ctx, pci, is_new, node_color))
			{
				if(g_hw_has_diff == nk_false)
					g_hw_has_diff = nk_true;
			}
		}
		nk_tree_pop(ctx);
	}
	
	return class_has_change;
}

static void draw_pci_node(struct nk_context* ctx, PNODE node)
{
	if (!node || !node->attributes)
		return;
	if (nk_tree_image_push_id(ctx, NK_TREE_TAB,
		GET_PNG(IDR_PNG_PCI),
		NWL_NodeAttrGet(node, "HWID"),
		NK_MINIMIZED, tree_id++))
	{
		const float ratio[] = { 0.2f, 0.8f };
		INT count = NWL_NodeAttrCount(node);
		nk_layout_row(ctx, NK_DYNAMIC, 0, 2, ratio);
		for (INT i = 0; i < count; i++)
		{
			PNODE_ATT att = NWL_NodeAttrEnum(node, i);
			if (!att || strcmp(att->key, "HWID") == 0)
				continue;
			nk_l(ctx, att->key, NK_TEXT_LEFT);
			nk_lhc(ctx, att->value, NK_TEXT_RIGHT, g_color_text_l);
		}
		nk_tree_pop(ctx);
	}
}

void draw_pci_class(struct nk_context* ctx, const char* title, struct nk_image image, const char* code)
{
	if (!g_ctx.pci)
		return;

	INT count = NWL_NodeChildCount(g_ctx.pci);
	INT class_count = 0;
	
	for (INT i = 0; i < count; i++)
	{
		PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
		const char* cl = NWL_NodeAttrGet(pci, "Class Code");
		if (_strnicmp(cl, code, 2) == 0)
			class_count++;
	}
	
	if (class_count == 0)
		return;
	
	if (nk_tree_image_push_id(ctx, NK_TREE_TAB, image, title, NK_MINIMIZED, tree_id++))
	{
		for (INT i = 0; i < count; i++)
		{
			PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
			const char* cl = NWL_NodeAttrGet(pci, "Class Code");
			if (_strnicmp(cl, code, 2) != 0)
				continue;
			draw_pci_node(ctx, pci);
		}
		nk_tree_pop(ctx);
	}
}

VOID gnwinfo_draw_pci_window(struct nk_context* ctx, float width, float height)
{
	if (!(g_ctx.window_flag & GUI_WINDOW_PCI))
		return;
	if (!nk_begin_ex(ctx, u8"PCI设备",
		nk_rect(0, height / 4.0f, width * 0.98f, height / 2.0f),
		NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE,
		GET_PNG(IDR_PNG_PCI), GET_PNG(IDR_PNG_CLOSE)))
	{
		g_ctx.window_flag &= ~GUI_WINDOW_PCI;
		goto out;
	}
	tree_id = 0;
	draw_pci_class(ctx, "Unclassified device", GET_PNG(IDR_PNG_INFO), "00");
	draw_pci_class(ctx, "Mass storage controller", GET_PNG(IDR_PNG_DISK), "01");
	draw_pci_class(ctx, "Network controller", GET_PNG(IDR_PNG_ETH), "02");
	draw_pci_class(ctx, "Display controller", GET_PNG(IDR_PNG_DISPLAY), "03");
	draw_pci_class(ctx, "Multimedia controller", GET_PNG(IDR_PNG_MM), "04");
	draw_pci_class(ctx, "Memory controller", GET_PNG(IDR_PNG_MEMORY), "05");
	draw_pci_class(ctx, "Bridge", GET_PNG(IDR_PNG_PCI), "06");
	draw_pci_class(ctx, "Communication controller", GET_PNG(IDR_PNG_FIRMWARE), "07");
	draw_pci_class(ctx, "Generic system peripheral", GET_PNG(IDR_PNG_PC), "08");
	draw_pci_class(ctx, "Input device controller", GET_PNG(IDR_PNG_PCI), "09");
	draw_pci_class(ctx, "Docking station", GET_PNG(IDR_PNG_PCI), "0a");
	draw_pci_class(ctx, "Processor", GET_PNG(IDR_PNG_CPU), "0b");
	draw_pci_class(ctx, "Serial bus controller", GET_PNG(IDR_PNG_FIRMWARE), "0c");
	draw_pci_class(ctx, "Wireless controller", GET_PNG(IDR_PNG_WLAN), "0d");
	draw_pci_class(ctx, "Intelligent controller", GET_PNG(IDR_PNG_PCI), "0e");
	draw_pci_class(ctx, "Satellite communications controller", GET_PNG(IDR_PNG_WLAN), "0f");
	draw_pci_class(ctx, "Encryption controller", GET_PNG(IDR_PNG_FIRMWARE), "10");

	if (g_ctx.pci)
	{
		INT count = NWL_NodeChildCount(g_ctx.pci);
		INT other_count = 0;
		for (INT i = 0; i < count; i++)
		{
			PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
			const char* cl = NWL_NodeAttrGet(pci, "Class Code");
			if (cl[0] != '0' && !(cl[0] == '1' && cl[1] == '0'))
				other_count++;
		}
		
		if (other_count > 0 && nk_tree_image_push_id(ctx, NK_TREE_TAB, GET_PNG(IDR_PNG_PCI), "Other", NK_MINIMIZED, tree_id++))
		{
			for (INT i = 0; i < count; i++)
			{
				PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
				const char* cl = NWL_NodeAttrGet(pci, "Class Code");
				if (cl[0] == '0' || (cl[0] == '1' && cl[1] == '0'))
					continue;
				draw_pci_node(ctx, pci);
			}
			nk_tree_pop(ctx);
		}
	}
out:
	nk_end(ctx);
}

static void draw_removed_pci_callback(LPCSTR hwid, LPCSTR location, LPCSTR desc, void* userdata)
{
	struct nk_context* ctx = (struct nk_context*)userdata;
	
	char title[512];
	if (location && location[0])
		snprintf(title, sizeof(title), "%s [%s]", hwid, location);
	else
		snprintf(title, sizeof(title), "%s", hwid);
	
	if (nk_tree_image_push_id_color(ctx, NK_TREE_TAB,
		GET_PNG(IDR_PNG_PCI),
		title,
		NK_MINIMIZED, tree_id++, g_color_error))
	{
		const float ratio[] = { 0.2f, 0.8f };
		nk_layout_row(ctx, NK_DYNAMIC, 0, 2, ratio);
		nk_l(ctx, "HWID", NK_TEXT_LEFT);
		nk_lhc(ctx, hwid, NK_TEXT_RIGHT, g_color_error);
		nk_l(ctx, "Location", NK_TEXT_LEFT);
		nk_lhc(ctx, location ? location : "-", NK_TEXT_RIGHT, g_color_error);
		nk_l(ctx, "Description", NK_TEXT_LEFT);
		nk_lhc(ctx, desc, NK_TEXT_RIGHT, g_color_error);
		nk_l(ctx, "Status", NK_TEXT_LEFT);
		nk_lhc(ctx, "Removed", NK_TEXT_RIGHT, g_color_error);
		nk_tree_pop(ctx);
	}
}

static nk_bool draw_pci_removed_devices(struct nk_context* ctx)
{
	if (!gnwinfo_hw_compare_available())
		return nk_false;
	
	INT saved_count = gnwinfo_hw_compare_get_pci_count();
	INT current_count = NWL_NodeChildCount(g_ctx.pci);
	
	if (saved_count <= current_count)
		return nk_false;
	
	gnwinfo_hw_compare_get_pci_removed_devices(draw_removed_pci_callback, ctx);
	return nk_true;
}

VOID draw_pci_simple(struct nk_context* ctx)
{
	if (!g_ctx.pci)
		return;

	nk_bool pci_has_change = nk_false;

	nk_layout_row(ctx, NK_DYNAMIC, 0, 1, (float[1]) { 1.0f - g_ctx.gui_ratio});
	
	tree_id = 0;
	
	if (gnwinfo_hw_compare_available())
	{
		INT count = NWL_NodeChildCount(g_ctx.pci);
		for (INT i = 0; i < count; i++)
		{
			PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
			LPCSTR hwid = NWL_NodeAttrGet(pci, "HWID");
			LPCSTR location = NWL_NodeAttrGet(pci, "Location");
			nk_bool is_new = !gnwinfo_hw_compare_pci_exists_by_hwid_location(hwid, location);
			nk_bool has_changed = check_pci_node_changed(pci);
			
			if (is_new || has_changed)
			{
				pci_has_change = nk_true;
				break;
			}
		}
		
		INT saved_count = gnwinfo_hw_compare_get_pci_count();
		if (saved_count > count)
			pci_has_change = nk_true;
	}
	
	struct nk_color pci_title_color = pci_has_change ? g_color_error : g_color_text_d;
	nk_image_label(ctx, GET_PNG(IDR_PNG_PCI), u8"PCI设备", NK_TEXT_LEFT, pci_title_color);

	if (draw_pci_class_compare(ctx, u8"未分类设备", GET_PNG(IDR_PNG_INFO), "00"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"大容量存储控制器", GET_PNG(IDR_PNG_DISK), "01"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"网络控制器", GET_PNG(IDR_PNG_ETH), "02"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"显示控制器", GET_PNG(IDR_PNG_DISPLAY), "03"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"多媒体控制器", GET_PNG(IDR_PNG_MM), "04"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"内存控制器", GET_PNG(IDR_PNG_MEMORY), "05"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"桥接器", GET_PNG(IDR_PNG_PCI), "06"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"通信控制器", GET_PNG(IDR_PNG_FIRMWARE), "07"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"通用系统外设", GET_PNG(IDR_PNG_PC), "08"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"输入设备控制器", GET_PNG(IDR_PNG_PCI), "09"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"扩展坞", GET_PNG(IDR_PNG_PCI), "0a"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"处理器", GET_PNG(IDR_PNG_CPU), "0b"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"串行总线控制器", GET_PNG(IDR_PNG_FIRMWARE), "0c"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"无线控制器", GET_PNG(IDR_PNG_WLAN), "0d"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"智能控制器", GET_PNG(IDR_PNG_PCI), "0e"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"卫星通信控制器", GET_PNG(IDR_PNG_WLAN), "0f"))
		pci_has_change = nk_true;
	if (draw_pci_class_compare(ctx, u8"加密控制器", GET_PNG(IDR_PNG_FIRMWARE), "10"))
		pci_has_change = nk_true;
	
	if (g_ctx.pci)
	{
		INT count = NWL_NodeChildCount(g_ctx.pci);
		INT other_count = 0;
		for (INT i = 0; i < count; i++)
		{
			PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
			const char* cl = NWL_NodeAttrGet(pci, "Class Code");
			if (cl[0] != '0' && !(cl[0] == '1' && cl[1] == '0'))
				other_count++;
		}
		
		if (other_count > 0 && nk_tree_image_push_id_color(ctx, NK_TREE_TAB, GET_PNG(IDR_PNG_PCI), u8"其他设备", NK_MINIMIZED, tree_id++, g_color_text_d))
		{
			for (INT i = 0; i < count; i++)
			{
				PNODE pci = NWL_NodeEnumChild(g_ctx.pci, i);
				const char* cl = NWL_NodeAttrGet(pci, "Class Code");
				if (cl[0] == '0' || (cl[0] == '1' && cl[1] == '0'))
					continue;
				
				LPCSTR hwid = NWL_NodeAttrGet(pci, "HWID");
				LPCSTR location = NWL_NodeAttrGet(pci, "Location");
				nk_bool is_new = gnwinfo_hw_compare_available() && !gnwinfo_hw_compare_pci_exists_by_hwid_location(hwid, location);
				nk_bool has_changed = check_pci_node_changed(pci);
				
				struct nk_color node_color = (is_new || has_changed) ? g_color_error : g_color_text_d;
				
				if (is_new)
				{
					if(g_hw_has_diff == nk_false)
						g_hw_has_diff = nk_true;
				}
				
				if (draw_pci_node_compare(ctx, pci, is_new, node_color))
				{
					pci_has_change = nk_true;
					if(g_hw_has_diff == nk_false)
						g_hw_has_diff = nk_true;
				}
			}
			nk_tree_pop(ctx);
		}
	}
	
	if (draw_pci_removed_devices(ctx))
		pci_has_change = nk_true;
	
	if (pci_has_change)
	{
		if(g_hw_has_diff == nk_false)
			g_hw_has_diff = nk_true;
	}
}
