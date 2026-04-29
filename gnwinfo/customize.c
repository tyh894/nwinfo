#include "gnwinfo.h"

extern VOID run_powershell_script(LPCSTR script_name_with_args);

static nk_bool opt_disable_telemetry = nk_true;
static nk_bool opt_disable_privacy = nk_true;
static nk_bool opt_remove_bloatware = nk_true;
static nk_bool opt_clear_gpo = nk_true;
static nk_bool opt_enable_firewall = nk_true;
static nk_bool opt_enable_defender = nk_true;
static nk_bool opt_disable_smb1 = nk_true;
static nk_bool opt_ssl_hardening = nk_true;
static nk_bool opt_enable_mitigations = nk_true;
static nk_bool opt_update_management = nk_true;
static nk_bool opt_ps_hardening = nk_true;
static nk_bool opt_defender_hardening = nk_true;
static nk_bool opt_virus_resistance = nk_true;

VOID
gnwinfo_draw_customize_window(struct nk_context* ctx, float width, float height)
{
	if (!(g_ctx.window_flag & GUI_WINDOW_CUSTOMIZE))
		return;

	if (!nk_begin_ex(ctx, u8"深度定制",
		nk_rect(width / 4.0f, height / 4.0f, width / 2.0f, height / 2.0f),
		NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE,
		GET_PNG(IDR_PNG_SETTINGS), GET_PNG(IDR_PNG_CLOSE)))
	{
		g_ctx.window_flag &= ~GUI_WINDOW_CUSTOMIZE;
		goto out;
	}

	nk_layout_row(ctx, NK_STATIC, 10, 1, (float[1]) { width });

	nk_layout_row(ctx, NK_DYNAMIC, 0, 2, (float[2]) { 0.5f, 0.5f });
	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"禁用遥测", &opt_disable_telemetry);
	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"禁用隐私收集", &opt_disable_privacy);

	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"移除预装软件", &opt_remove_bloatware);
	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"清除本地GPO", &opt_clear_gpo);

	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"启用防火墙", &opt_enable_firewall);
	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"配置Defender", &opt_enable_defender);

	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"禁用SMBv1", &opt_disable_smb1);
	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"SSL/TLS加固", &opt_ssl_hardening);

	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"启用安全缓解", &opt_enable_mitigations);
	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"更新管理", &opt_update_management);

	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"PowerShell加固", &opt_ps_hardening);
	nk_layout_row_push(ctx, 0.5f);
	nk_checkbox_label(ctx, u8"Defender加固", &opt_defender_hardening);

	nk_layout_row_push(ctx, 0.5f);
	{
		struct nk_color saved_normal = ctx->style.checkbox.text_normal;
		struct nk_color saved_hover = ctx->style.checkbox.text_hover;
		struct nk_color saved_active = ctx->style.checkbox.text_active;
		ctx->style.checkbox.text_normal = g_color_warning;
		ctx->style.checkbox.text_hover = g_color_warning;
		ctx->style.checkbox.text_active = g_color_warning;
		nk_checkbox_label(ctx, u8"常见病毒抵制", &opt_virus_resistance);
		ctx->style.checkbox.text_normal = saved_normal;
		ctx->style.checkbox.text_hover = saved_hover;
		ctx->style.checkbox.text_active = saved_active;
	}
	

	nk_layout_row(ctx, NK_STATIC, 25, 1, (float[1]) { width*0.2f });
	if (nk_button_label(ctx, u8"开始优化"))
	{
		char items[512] = "";
		if (opt_disable_telemetry == nk_false) strcat_s(items, sizeof(items), "disable-telemetry;");
		if (opt_disable_privacy == nk_false) strcat_s(items, sizeof(items), "disable-privacy;");
		if (opt_remove_bloatware == nk_false) strcat_s(items, sizeof(items), "remove-bloatware;");
		if (opt_clear_gpo == nk_false) strcat_s(items, sizeof(items), "clear-gpo;");
		if (opt_enable_firewall == nk_false) strcat_s(items, sizeof(items), "enable-firewall;");
		if (opt_enable_defender == nk_false) strcat_s(items, sizeof(items), "enable-defender;");
		if (opt_disable_smb1 == nk_false) strcat_s(items, sizeof(items), "disable-smb1;");
		if (opt_ssl_hardening == nk_false) strcat_s(items, sizeof(items), "ssl-hardening;");
		if (opt_enable_mitigations == nk_false) strcat_s(items, sizeof(items), "enable-mitigations;");
		if (opt_update_management == nk_false) strcat_s(items, sizeof(items), "update-management;");
		if (opt_ps_hardening == nk_false) strcat_s(items, sizeof(items), "ps-hardening;");
		if (opt_defender_hardening == nk_false) strcat_s(items, sizeof(items), "defender-hardening;");

		if (strlen(items) > 0) {
			char cmd[MAX_PATH * 4];
			sprintf_s(cmd, sizeof(cmd), "menu-optimize.ps1 -Items \"%s\"", items);
			run_powershell_script(cmd);
		}
	}

out:
	nk_end(ctx);
}
