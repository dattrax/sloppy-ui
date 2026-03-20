/*
 * InputProcessor: GLFW key callback wiring to SkiaRenderer.
 * The main thread calls glfwWaitEventsTimeout; callbacks enqueue key events.
 */

#pragma once

#include <GLFW/glfw3.h>

class SkiaRenderer;

class InputProcessor {
public:
    InputProcessor(SkiaRenderer* renderer);
    ~InputProcessor() = default;

    void setWindow(GLFWwindow*);

    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

private:
    SkiaRenderer* fRenderer;
};
