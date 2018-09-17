#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "dx.h"
GLFWwindow* window;
DXEngine dxEngine;
int main(int argc, char** argv) {
	glfwInit();
	window = glfwCreateWindow(1280, 720, "DXR", nullptr, nullptr);
	bool close = false;
	dxEngine.Init(glfwGetWin32Window(window), 1280, 720);
	while (!glfwWindowShouldClose(window)) {
		dxEngine.Render();
		glfwPollEvents();
	}
	return 0;
}