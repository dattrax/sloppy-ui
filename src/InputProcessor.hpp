/*
 * InputProcessor: Handles keyboard input in a separate thread.
 * Polls GLFW events at ~60fps (16ms) and forwards key press events
 * to SkiaRenderer via an event queue.
 */

#pragma once

#include <thread>
#include <queue>
#include <mutex>
#include <utility>
#include <GLFW/glfw3.h>

class SkiaRenderer;

class InputProcessor {
public:
    InputProcessor(SkiaRenderer* renderer);
    ~InputProcessor();

    void start();
    void stop();
    void setWindow(GLFWwindow* window);

    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

private:
    SkiaRenderer* fRenderer;
    GLFWwindow* fWindow = nullptr;
    std::thread fThread;
    bool fRunning = false;

    void threadLoop();
};
