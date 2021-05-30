#include "includes.h"

using fSetCursorPos = BOOL(WINAPI*)(int, int);
fSetCursorPos pSetCursorPos = NULL;

using fPresent = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
fPresent pPresent = NULL;

using fResizeBuffers = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
fResizeBuffers pResizeBuffers = NULL;

using fBitBlt = BOOL(WINAPI*)(HDC, int, int, int, int, HDC, int, int, DWORD);
fBitBlt pfBitBlt = NULL;

IDXGISwapChain* swapchain = nullptr;
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
ID3D11RenderTargetView* render_view = nullptr;

static bool renderview_lost = true;

enum IDXGISwapChainvTable //for dx10 / dx11
{
	QUERY_INTERFACE,
	ADD_REF,
	RELEASE,
	SET_PRIVATE_DATA,
	SET_PRIVATE_DATA_INTERFACE,
	GET_PRIVATE_DATA,
	GET_PARENT,
	GET_DEVICE,
	PRESENT,
	GET_BUFFER,
	SET_FULLSCREEN_STATE,
	GET_FULLSCREEN_STATE,
	GET_DESC,
	RESIZE_BUFFERS,
	RESIZE_TARGET,
	GET_CONTAINING_OUTPUT,
	GET_FRAME_STATISTICS,
	GET_LAST_PRESENT_COUNT
};

HWND game_hwnd;
WNDPROC pWndProc;

DWORD viewmatrix_address;

class Name;

class BotData //AssemblyCSharp.BotData
{
public:
	bool get_is_alive()
	{
		return memory_utils::read<bool>({ (DWORD)this, 0x18 }) == false;
	}

	int get_teamid()
	{
		return memory_utils::read<int>({ (DWORD)this, 0x88 });
	}

	Name* get_name_class()
	{
		return memory_utils::read<Name*>({ (DWORD)this, 0x8C });
	}

	Vector get_origin()
	{
		return memory_utils::read<Vector>({ (DWORD)this, 0xA8 });
	}

	bool get_is_active()
	{
		return memory_utils::read<bool>({ (DWORD)this, 0x90 });
	}

	bool get_is_zombie()
	{
		return memory_utils::read<bool>({ (DWORD)this, 0xC0 });
	}
};

class Name //AssemblyCSharp.BotData.Name
{
public:
	int get_length_name()
	{
		return memory_utils::read<int>({ (DWORD)this, 0x8 });
	}

	wchar_t* get_name()
	{
		return memory_utils::read_wstring({ (DWORD)this, 0xC });
	}
};

bool world_to_screen(const Matrix4x4& matrix, const Vector& vIn, float* flOut)
{
	const auto& view_projection = matrix;

	float w = view_projection.m[0][3] * vIn.x + view_projection.m[1][3] * vIn.y + view_projection.m[2][3] * vIn.z + view_projection.m[3][3];

	if (w < 0.01)
		return false;

	flOut[0] = view_projection.m[0][0] * vIn.x + view_projection.m[1][0] * vIn.y + view_projection.m[2][0] * vIn.z + view_projection.m[3][0];
	flOut[1] = view_projection.m[0][1] * vIn.x + view_projection.m[1][1] * vIn.y + view_projection.m[2][1] * vIn.z + view_projection.m[3][1];

	float invw = 1.0f / w;

	flOut[0] *= invw;
	flOut[1] *= invw;

	int width, height;

	auto io = ImGui::GetIO();
	width = io.DisplaySize.x;
	height = io.DisplaySize.y;

	float x = (float)width / 2;
	float y = (float)height / 2;

	x += 0.5 * flOut[0] * (float)width + 0.5;
	y -= 0.5 * flOut[1] * (float)height + 0.5;

	flOut[0] = x;
	flOut[1] = y;

	return true;
}

BYTE mask[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 };

/*

	98 5F F5 0A 00 00 00 00 AF 4D 20 0B A8 D9 21 0B 
	B4 0E 9A 0B 00 00 12 00 B4 0E 9A 0B 00 00 12 40 
	90 8E C0 15 90 8E C0 15 00 00 00 00 88 53 F1 0A 
	00 00 00 00 B4 0E 9A 0B 00 00 00 00 90 8E C0 15 
	18 F8 C7 1B 00 00 00 00 00 00 00 00 E0 F7 C7 1B 
	00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	00 00 00 00 48 EF BD 15 00 00 00 00 00 00 00 00

	98 62 08 0B 00 00 00 00 AF 4D 33 0B A8 D9 34 0B 
	B4 0E AD 0B 00 00 12 00 B4 0E AD 0B 00 00 12 40 
	F0 8B DA 15 F0 8B DA 15 00 00 00 00 90 8C 0A 0B 
	00 00 00 00 B4 0E AD 0B 00 00 00 00 F0 8B DA 15 
	F8 A5 DF 1B 00 00 00 00 00 00 00 00 C0 A5 DF 1B 
	00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
	00 00 00 00 F0 CB D5 15 00 00 00 00 00 00 00 00 

	98 56 34 0B 00 00 00 00 AF 4D 5F 0B A8 D9 60 0B 
	B4 0E D9 0B 00 00 12 00 B4 0E D9 0B 00 00 12 40 
	20 11 FF 15 20 11 FF 15 00 00 00 00 78 EE E2 02 
	00 00 00 00 B4 0E D9 0B 00 00 00 00 20 11 FF 15 
	08 C3 08 1C 00 00 00 00 00 00 00 00 D0 C2 08 1C
	00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

	88 7E 16 0B 00 00 00 00 AF 4D 41 0B A8 D9 42 0B 
	B4 0E BB 0B 00 00 12 00 B4 0E BB 0B 00 00 12 40 
	88 EC DE 15 88 EC DE 15 00 00 00 00 28 DC B0 02 
	00 00 00 00 B4 0E BB 0B 00 00 00 00 88 EC DE 15 
	F8 37 E8 1B 00 00 00 00 00 00 00 00 C0 37 E8 1B

	88 66 9F 0B 00 00 00 00 AF 4D CA 0B A8 D9 CB 0B 
	B4 0E 44 0C 00 00 12 00 B4 0E 44 0C 00 00 12 40 
	50 AA 63 16 50 AA 63 16 00 00 00 00 78 5A 9B 0B 
	00 00 00 00 B4 0E 44 0C 00 00 00 00 50 AA 63 16 
	20 B3 F9 1B 00 00 00 00 00 00 00 00 E8 B2 F9 1B

	08 BD 61 02 00 00 00 00 AF 4D E1 0A A8 D9 E2 0A 
	B4 0E 5B 0B 00 00 12 00 B4 0E 5B 0B 00 00 12 40 
	D8 66 81 15 D8 66 81 15 00 00 00 00 D0 DD 61 02 
	00 00 00 00 B4 0E 5B 0B 00 00 00 00 D8 66 81 15 
	B8 17 8E 1B 00 00 00 00 00 00 00 00 80 17 8E 1B
*/

/*
	00 00 00 00 AF 4D ? ? A8 D9 ? ? B4 0E ? ? 00 00 12 00 B4
	-= 0x4
*/

namespace vars
{
	bool menu_open;
	bool unload_lib;
	namespace global
	{
		bool enable;
	}
	namespace visuals
	{
		int player_type = 0;
		int box_type = 0;
		bool name = false;
		bool health = false;
		bool radar_3d = false;
		bool distance = false;
		float col_enemy[4];
		float col_teammate[4];
	}
	namespace font
	{
		int style = 0;
		float size = 0;
	}
	void load_default_settings() {
		global::enable = true;

		visuals::player_type = 0;
		visuals::box_type = 4;
		visuals::name = true;
		visuals::health = true;
		visuals::radar_3d = true;
		visuals::distance = true;
		
		visuals::col_enemy[0] = 1.f;
		visuals::col_enemy[1] = 0.f;
		visuals::col_enemy[2] = 0.f;
		visuals::col_enemy[3] = 1.f;

		visuals::col_teammate[0] = 0.f;
		visuals::col_teammate[1] = 0.f;
		visuals::col_teammate[2] = 1.f;
		visuals::col_teammate[3] = 1.f;

		font::style = 1;
	}
}

void init_imgui()
{
	ImGui::CreateContext();
	ImGui::StyleColorsClassic();

	auto& style = ImGui::GetStyle();

	style.FrameRounding = 3.f;
	style.ChildRounding = 3.f;
	style.ChildBorderSize = 1.f;
	style.ScrollbarSize = 0.6f;
	style.ScrollbarRounding = 3.f;
	style.GrabRounding = 3.f;
	style.WindowRounding = 0.f;

	style.Colors[ImGuiCol_FrameBg] = ImColor(200, 200, 200);
	style.Colors[ImGuiCol_FrameBgHovered] = ImColor(220, 220, 220);
	style.Colors[ImGuiCol_FrameBgActive] = ImColor(230, 230, 230);
	style.Colors[ImGuiCol_Separator] = ImColor(180, 180, 180);
	style.Colors[ImGuiCol_CheckMark] = ImColor(255, 172, 19);
	style.Colors[ImGuiCol_SliderGrab] = ImColor(255, 172, 19);
	style.Colors[ImGuiCol_SliderGrabActive] = ImColor(255, 172, 19);
	style.Colors[ImGuiCol_ScrollbarBg] = ImColor(120, 120, 120);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImColor(255, 172, 19);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab);
	style.Colors[ImGuiCol_Header] = ImColor(160, 160, 160);
	style.Colors[ImGuiCol_HeaderHovered] = ImColor(200, 200, 200);
	style.Colors[ImGuiCol_Button] = ImColor(180, 180, 180);
	style.Colors[ImGuiCol_ButtonHovered] = ImColor(200, 200, 200);
	style.Colors[ImGuiCol_ButtonActive] = ImColor(230, 230, 230);
	style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.78f, 0.78f, 0.78f, 1.f);
	style.Colors[ImGuiCol_WindowBg] = ImColor(220, 220, 220, 0.7 * 255);
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.40f, 0.40f, 0.80f, 0.20f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.40f, 0.40f, 0.55f, 0.80f);
	style.Colors[ImGuiCol_Border] = ImVec4(0.72f, 0.72f, 0.72f, 0.70f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.83f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.75f, 0.75f, 0.75f, 0.87f);
	style.Colors[ImGuiCol_Text] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.72f, 0.72f, 0.72f, 0.76f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.81f, 0.81f, 0.81f, 1.00f);
	style.Colors[ImGuiCol_Tab] = ImVec4(0.61f, 0.61f, 0.61f, 0.79f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(0.71f, 0.71f, 0.71f, 0.80f);
	style.Colors[ImGuiCol_TabActive] = ImVec4(0.77f, 0.77f, 0.77f, 0.84f);
	style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.73f, 0.73f, 0.73f, 0.82f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.58f, 0.58f, 0.58f, 0.84f);

	auto& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 15.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
	ImGui_ImplWin32_Init(game_hwnd);
	ImGui_ImplDX11_Init(device, context);
	ImGui_ImplDX11_CreateDeviceObjects();

	ImGuiWindowFlags flags_color_edit = ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoInputs;
	ImGui::SetColorEditOptions(flags_color_edit);
}

Matrix4x4 get_viewmatrix()
{
	return memory_utils::read<Matrix4x4>({ viewmatrix_address, 0x154 });
}

void menu()
{
	ImGui::Begin("BLOCKADE Classic hack by SEXBOMBA5612");

	ImGui::BeginChild("Visuals", ImVec2(), true);

	if (ImGui::TreeNode("Globals"))
	{
		ImGui::ColorEdit4("##invis enemy", vars::visuals::col_enemy);
		ImGui::SameLine();
		ImGui::Text("enemy   ");
		ImGui::SameLine();
		ImGui::ColorEdit4("##invis teammate", vars::visuals::col_teammate);
		ImGui::SameLine();
		ImGui::Text("teammate    ");

		const char* pcszBoxesType[] = { "Off", "Box", "Box outline", "Corner box", "Corner box outline",  "Round box", "Round box outline" };
		ImGui::Combo("Bounding box", &vars::visuals::box_type, pcszBoxesType, IM_ARRAYSIZE(pcszBoxesType));

		ImGui::TreePop();
	}

	const char* pcszPlayerType[] = { "Enemies", "Enemies and teammates", "Off", };
	ImGui::Combo("Player type", &vars::visuals::player_type, pcszPlayerType, IM_ARRAYSIZE(pcszPlayerType));

	if (ImGui::TreeNode("ESP"))
	{
		ImGui::Checkbox("Name", &vars::visuals::name);
		ImGui::Checkbox("3D radar", &vars::visuals::radar_3d);
		ImGui::Checkbox("Distance", &vars::visuals::distance);
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Font"))
	{
		const char* pcszFontType[] = { "Default", "Shadow", "Outline" };
		ImGui::SliderFloat("Font size", &vars::font::size, 0.f, 20.f, "%.0f", 1.0f);
		ImGui::Combo("Font type", &vars::font::style, pcszFontType, IM_ARRAYSIZE(pcszFontType));

		ImGui::TreePop();
	}

	ImGui::End();
}

std::vector<DWORD>founded_entity;
std::mutex mtx;

void get_entity_data_thread()
{
	while (!vars::unload_lib)
	{
		auto entity_arr = memory_utils::find_pattern_in_heap_array((const char*)mask, "xxxxxxxx");

		mtx.lock();
		founded_entity = entity_arr;
		mtx.unlock();

		Sleep(1000);
	}
}

void DrawBox(float x, float y, float w, float h, const ImColor col)
{
	m_pDrawing->DrawEspBox(vars::visuals::box_type, x, y, w, h, col.Value.x, col.Value.y, col.Value.z, col.Value.w);
}

void DrawName(const char* pcszPlayerName, float x, float y, float w, ImColor col)
{
	if (vars::visuals::name == false)
		return;

	if (pcszPlayerName == NULL)
		return;

	ImFont* Font = ImGui::GetIO().Fonts->Fonts[0];
	ImVec2 text_size = Font->CalcTextSizeA(vars::font::size ? vars::font::size : Font->FontSize, FLT_MAX, 0, "");

	m_pDrawing->AddText(x + w / 2.f, y - text_size.y - 2.f, ImColor(1.f, 1.f, 1.f, col.Value.w), vars::font::size, vars::font::style, FL_CENTER_X, u8"%s", pcszPlayerName);
}

void features()
{
	mtx.lock();
	if (vars::visuals::player_type != 2)
	{
		int my_team = 1337;
		for (auto& ent : founded_entity)
		{
			BotData* bot = (BotData*)ent;

			if (!memory_utils::is_valid_ptr(bot))
				continue;

			if (!memory_utils::is_valid_ptr(bot->get_name_class()))
				continue;

			if (bot->get_name_class()->get_length_name() > 32)
				continue;

			if (!bot->get_is_active())
				continue;

			if (!bot->get_is_alive())
				continue;

			auto origin = bot->get_origin();

			if (origin.x == -1000.f && origin.y == -1000.f && origin.z == -1000.f) //local player check
			{
				my_team = bot->get_teamid();
			}
		}
		for (auto& ent : founded_entity)
		{
			BotData* bot = (BotData*)ent;

			if (!memory_utils::is_valid_ptr(bot))
				continue;

			if (!memory_utils::is_valid_ptr(bot->get_name_class()))
				continue;

			if (bot->get_name_class()->get_length_name() > 32)
				continue;

			if (!bot->get_is_active())
				continue;

			if (!bot->get_is_alive())
				continue;

			if (vars::visuals::player_type == 0 && bot->get_teamid() == my_team)
				continue;

			auto name_wch = bot->get_name_class()->get_name();
			char name_ch[255];
			wcstombs(name_ch, name_wch, 255);
			auto teamid = bot->get_teamid();
			auto origin = bot->get_origin();

			Vector origin_bottom;
			origin_bottom.x = origin.x;
			origin_bottom.y = origin.y - 0.5f;
			origin_bottom.z = origin.z;
			Vector origin_top;
			origin_top.x = origin.x;
			origin_top.y = origin.y + 2.2f;
			origin_top.z = origin.z;

			if (origin.x == -1000.f && origin.y == -1000.f && origin.z == -1000.f) //local player check
				continue;

			ImColor col;

			if (bot->get_teamid() == my_team)
			{
				/*if (is_visible)
					col = ImColor(vars::visuals::teammate_color[0], vars::visuals::teammate_color[1], vars::visuals::teammate_color[2],
						vars::visuals::teammate_color[3]);
				else
					col = ImColor(vars::visuals::teammate_invis_color[0], vars::visuals::teammate_invis_color[1], vars::visuals::teammate_invis_color[2],
						vars::visuals::teammate_invis_color[3]);*/

				col = ImColor(vars::visuals::col_teammate[0], vars::visuals::col_teammate[1], vars::visuals::col_teammate[2],
					vars::visuals::col_teammate[3]);
			}
			else
			{
				/*if (is_visible)
					col = ImColor(vars::visuals::enemy_color[0], vars::visuals::enemy_color[1], vars::visuals::enemy_color[2],
						vars::visuals::enemy_color[3]);
				else
					col = ImColor(vars::visuals::enemy_invis_color[0], vars::visuals::enemy_invis_color[1], vars::visuals::enemy_invis_color[2],
						vars::visuals::enemy_invis_color[3]);*/

				col = ImColor(vars::visuals::col_enemy[0], vars::visuals::col_enemy[1], vars::visuals::col_enemy[2],
					vars::visuals::col_enemy[3]);
			}

			float out_bottom[2], out_top[2];
			if (world_to_screen(get_viewmatrix(), origin_bottom, out_bottom)
				&& world_to_screen(get_viewmatrix(), origin_top, out_top))
			{
				float h = out_bottom[1] - out_top[1];
				float w = h / 2;
				float x = out_bottom[0] - w / 2;
				float y = out_top[1];

				DrawBox(x, y, w, h, col);
				DrawName(name_ch, x, y, w, ImColor(1.f, 1.f, 1.f));
			}
		}

	}
	mtx.unlock();
}

void begin_scene()
{
	if (vars::unload_lib)
		return;

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	if (vars::menu_open)
	{
		ImGui::GetIO().MouseDrawCursor = true;
		menu();
	}
	else
		ImGui::GetIO().MouseDrawCursor = false;


	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::Begin("##BackBuffer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
	ImGui::SetWindowPos(ImVec2(), ImGuiCond_Always);
	ImGui::SetWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), ImGuiCond_Always);

	features();

	ImGui::GetCurrentWindow()->DrawList->PushClipRectFullScreen();
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::EndFrame();

	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

HRESULT WINAPI Present_Hooked(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags)
{
	if (renderview_lost)
	{
		if (SUCCEEDED(pChain->GetDevice(__uuidof(ID3D11Device), (void**)&device)))
		{
			device->GetImmediateContext(&context);

			ID3D11Texture2D* pBackBuffer;
			pChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
			device->CreateRenderTargetView(pBackBuffer, NULL, &render_view);
			pBackBuffer->Release();

			std::cout << __FUNCTION__ << " > renderview successfully received!" << std::endl;
			renderview_lost = false;
		}
	}

	static auto once = [pChain, SyncInterval, Flags]()
	{
		init_imgui();
		std::cout << __FUNCTION__ << " > first called!" << std::endl;
		return true;
	}();

	context->OMSetRenderTargets(1, &render_view, NULL);

	begin_scene();

	return pPresent(pChain, SyncInterval, Flags);
}

HRESULT WINAPI ResizeBuffers_hooked(IDXGISwapChain* pChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT Flags)
{
	static auto once = []()
	{
		std::cout << __FUNCTION__ << " > first called!" << std::endl;
		return true;
	}();

	render_view->Release();
	render_view = nullptr;
	renderview_lost = true;

	ImGui_ImplDX11_CreateDeviceObjects();
	ImGui_ImplDX11_InvalidateDeviceObjects();

	return pResizeBuffers(pChain, BufferCount, Width, Height, NewFormat, Flags);
}

LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc_Hooked(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static auto once = []()
	{
		std::cout << __FUNCTION__ << " first called!" << std::endl;
		return true;
	}();

	if (uMsg == WM_KEYDOWN && wParam == VK_INSERT)
	{
		vars::menu_open = !vars::menu_open;
		return FALSE;
	}

	if (vars::menu_open && ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
	{
		return TRUE;
	}

	return CallWindowProc(pWndProc, hwnd, uMsg, wParam, lParam);
}

struct find_window_s
{
	char* window_name;
	HWND hWnd;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	auto* window_inform_s = (find_window_s*)lParam;

	if ((!GetWindow(hwnd, GW_OWNER)) && IsWindow(hwnd))
	{
		DWORD process_id = NULL;
		GetWindowThreadProcessId(hwnd, &process_id);

		char* text_window = new char[255];

		GetWindowText(hwnd, text_window, 255);

		if (GetCurrentProcessId() == process_id && strstr(text_window, window_inform_s->window_name) && !strstr(text_window, ".exe"))
		{
			std::cout << "Window name: " << text_window << std::endl;
			window_inform_s->hWnd = hwnd;
			return 0;
		}
	}

	return 1;
}

HWND find_game_window(const char* psz_part_of_word_window_name)
{
	find_window_s window_inform_struct{};

	window_inform_struct.window_name = new char[strlen(psz_part_of_word_window_name)];

	strcpy(window_inform_struct.window_name, psz_part_of_word_window_name);

	EnumWindows(EnumWindowsProc, (LPARAM)&window_inform_struct);

	delete[] window_inform_struct.window_name;

	return window_inform_struct.hWnd;
}

/*
	Address of signature = UnityPlayer.dll + 0x004BBD17
	"\x0F\x11\x00\x00\x0F\x28\x00\x0F\xC6\xC8\x00\x0F\x11\x00\x00\x0F\x28", "xx??xx?xxx?xx??xx"
	"0F 11 ? ? 0F 28 ? 0F C6 C8 ? 0F 11 ? ? 0F 28"



	Address of signature = UnityPlayer.dll + 0x0049CCDD
	"\x0F\x11\x00\x00\x00\x00\x00\x0F\x11\x00\x00\x00\x00\x00\x0F\x11\x00\x00\x00\x00\x00\x0F\x11\x00\x00\x00\x00\x00\x8B\x4E", "xx?????xx?????xx?????xx?????xx"
	"0F 11 ? ? ? ? ? 0F 11 ? ? ? ? ? 0F 11 ? ? ? ? ? 0F 11 ? ? ? ? ? 8B 4E"

	Address of signature = GameAssembly.dll + 0x001DF443
	"\x66\x0F\x00\x00\x00\x66\x0F\x00\x00\x00\x66\x0F\x00\x00\x00\x66\x0F\x00\x00\x00\x8D\xBF", "xx???xx???xx???xx???xx"
	"66 0F ? ? ? 66 0F ? ? ? 66 0F ? ? ? 66 0F ? ? ? 8D BF"
*/

void GreatHackThread(void* arg)
{
	vars::load_default_settings();

	game_hwnd = find_game_window("BlockadeClassic");

	if (game_hwnd == NULL)
	{
		std::cout << "[-] Game Window not found\n";
		return;
	}

	DWORD assemblycsharpbotdata_mask = NULL;
	while (!assemblycsharpbotdata_mask)
	{
		assemblycsharpbotdata_mask = memory_utils::find_pattern_in_heap("\x00\x00\x00\x00\xAF\x4D\xFF\xFF\xA8\xD9\xFF\xFF\xB4\x0E\xFF\xFF\x00\x00\x12\x00\xB4", "xxxxxx??xx??xx??xxxxx");
		if (!assemblycsharpbotdata_mask)
			printf("not found AssemblyCSharp.BotData mask\n");
		Sleep(1000);
	}

	assemblycsharpbotdata_mask -= 0x4;

	printf("AssemblyCSharp.BotData mask address: 0x%I32X\n", assemblycsharpbotdata_mask);

	memcpy(mask, (void*)(assemblycsharpbotdata_mask + 0x20), 4);

	HMODULE unity_player_module = GetModuleHandle("UnityPlayer.dll");

	if (unity_player_module == INVALID_HANDLE_VALUE) {
		printf("not found UnityPlayer.dll\n");
		return;
	}

	auto viewmatrixwriter_address = memory_utils::find_pattern(unity_player_module, 
		"\x0F\x11\x00\x00\x00\x00\x00\x0F\x11\x00\x00\x00\x00\x00\x0F\x11\x00\x00\x00\x00\x00\x0F\x11\x00\x00\x00\x00\x00\x8B\x4E", "xx?????xx?????xx?????xx?????xx");

	if (viewmatrixwriter_address == NULL) {
		printf("not found viewmatrixwriter_address\n");
		return;
	}

	CShellCodeHelper* m_pViewMatrixFinder = new CShellCodeHelper;

	m_pViewMatrixFinder->setup((void*)viewmatrixwriter_address);

	BYTE viewmatrix_finder_patch[] =
	{
		0x0F, 0x11, 0x9F, 0x54, 0x01, 0x00, 0x00,	//movups[edi + 00000154],xmm3	|	original
		0x89, 0x3D, 0xFF, 0xFF, 0xFF, 0xFF,			//mov [address_of_my_value_for_copyng_viewmatix], edi
	};

	auto address_of_value = (DWORD)&viewmatrix_address;
	memcpy(viewmatrix_finder_patch + 9, &address_of_value, 4);

	m_pViewMatrixFinder->patch(viewmatrix_finder_patch, sizeof(viewmatrix_finder_patch), 7);

	CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)get_entity_data_thread, NULL, NULL, NULL);

	pWndProc = (WNDPROC)SetWindowLongPtr(game_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc_Hooked);

	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
	DXGI_SWAP_CHAIN_DESC scd{};
	ZeroMemory(&scd, sizeof(scd));
	scd.BufferCount = 1;
	scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	scd.OutputWindow = game_hwnd;
	scd.SampleDesc.Count = 1;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	scd.Windowed = TRUE;
	scd.BufferDesc.RefreshRate.Numerator = 60;
	scd.BufferDesc.RefreshRate.Denominator = 1;

	if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, NULL, &feature_level, 1, D3D11_SDK_VERSION, &scd, &swapchain, &device, NULL, &context)))
	{
		std::cout << "failed to create device\n";
		return;
	}

	void** pVTableSwapChain = *reinterpret_cast<void***>(swapchain);

	void* pPresentAddress = reinterpret_cast<LPVOID>(pVTableSwapChain[IDXGISwapChainvTable::PRESENT]);
	void* pResizeBuffersAddress = reinterpret_cast<LPVOID>(pVTableSwapChain[IDXGISwapChainvTable::RESIZE_BUFFERS]);

	MH_Initialize();

	if (MH_CreateHook(pPresentAddress, &Present_Hooked, (LPVOID*)&pPresent) != MH_OK
		|| MH_EnableHook(pPresentAddress) != MH_OK)
	{
		std::cout << "failed create hook present\n";
		return;
	}

	if (MH_CreateHook(pResizeBuffersAddress, &ResizeBuffers_hooked, (LPVOID*)&pResizeBuffers) != MH_OK
		|| MH_EnableHook(pResizeBuffersAddress) != MH_OK)
	{
		std::cout << "failed create hook resizebuffers\n";
		return;
	}

	while (true)
	{
		if (GetAsyncKeyState(VK_DELETE))
		{
			vars::unload_lib = true;
			Sleep(1000);
			break;
		}
		Sleep(1);
	}

	MH_DisableHook(pPresentAddress);
	MH_RemoveHook(pPresentAddress);

	Sleep(100);

	MH_DisableHook(pResizeBuffersAddress);
	MH_RemoveHook(pResizeBuffersAddress);

	Sleep(100);

	SetWindowLongPtr(game_hwnd, GWL_WNDPROC, (LONG)pWndProc);

	Sleep(100);

	render_view->Release();
	render_view = nullptr;

	Sleep(100);

	m_pViewMatrixFinder->disable();

	Sleep(100);

	FreeLibraryAndExitThread((HMODULE)arg, 0);
}

BOOL APIENTRY DllMain( HMODULE hModule,
					   DWORD  ul_reason_for_call,
					   LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		if (AllocConsole())
		{
			freopen("conout$", "w", stdout);
			SetConsoleTitle("mamy check");
		}
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)GreatHackThread, hModule, NULL, NULL);
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

