#pragma once
#include "GLFW/glfw3.h"
#include "Window.h"

class Application
{
public:

private:
	Window* window;
public:
	Application();
	~Application();

	bool Initalize();
	bool Terminate();

	static void glfwErrorCallback(int error, const char* description);
private:


};

