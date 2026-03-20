#include "InputProcessor.hpp"
#include "SkiaRenderer.hpp"
#include <GLFW/glfw3.h>

static InputProcessor* gInputProcessor = nullptr;

InputProcessor::InputProcessor(SkiaRenderer* renderer)
    : fRenderer(renderer) {
}

void InputProcessor::setWindow(GLFWwindow*) {
    gInputProcessor = this;
}

void InputProcessor::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    (void)window;

    if (gInputProcessor && action == GLFW_PRESS) {
        gInputProcessor->fRenderer->enqueueInputEvent(key, true);
    }
}
