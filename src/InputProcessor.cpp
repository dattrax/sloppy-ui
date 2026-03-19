#include "InputProcessor.hpp"
#include "SkiaRenderer.hpp"
#include <GLFW/glfw3.h>
#include <chrono>

static InputProcessor* gInputProcessor = nullptr;

InputProcessor::InputProcessor(SkiaRenderer* renderer)
    : fRenderer(renderer) {
}

InputProcessor::~InputProcessor() {
    stop();
}

void InputProcessor::setWindow(GLFWwindow* window) {
    fWindow = window;
    gInputProcessor = this;
}

void InputProcessor::start() {
    if (fRunning) return;
    fRunning = true;
    fThread = std::thread(&InputProcessor::threadLoop, this);
}

void InputProcessor::stop() {
    if (!fRunning) return;
    fRunning = false;
    if (fThread.joinable()) {
        fThread.join();
    }
}

void InputProcessor::threadLoop() {
    while (fRunning) {
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void InputProcessor::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    (void)window;
    
    if (gInputProcessor && action == GLFW_PRESS) {
        gInputProcessor->fRenderer->enqueueInputEvent(key, true);
    }
}
