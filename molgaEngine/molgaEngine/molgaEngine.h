#pragma once

#define MOLGAENGINE_API __declspec(dllexport)

extern "C" MOLGAENGINE_API bool InitializeWindow(int width, int height, const char* title);
extern "C" MOLGAENGINE_API HWND GetMolgaEngineWindowHandle();
