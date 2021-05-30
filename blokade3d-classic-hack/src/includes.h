#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <mutex>

#include <d3d11.h>
#pragma comment (lib, "d3d11")

#include "../libs/minhook/minhook.h"
#pragma comment (lib, "libs/minhook/minhook.lib")

#include "../libs/imgui/imgui.h"
#include "../libs/imgui/imgui_internal.h"
#include "../libs/imgui/imgui_impl_dx11.h"
#include "../libs/imgui/imgui_impl_win32.h"

#include "memory_utils/memory_utils.h"

#include "math/matrix4x4/matrix4x4.h"
#include "math/vector/vector.h"

bool world_to_screen(const Matrix4x4& matrix, const Vector& vIn, float* flOut);

#include "draw_list/draw_list.h"

#include "shellcode_patch_helper/shellcode_patch_helper.h"