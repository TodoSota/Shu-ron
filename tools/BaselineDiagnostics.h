#pragma once
// For an isolated baseline test copy only. Not included by the application project.
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>

inline void printBaselineContext(GLFWwindow* window) {
    for (auto entry : { GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION }) {
        const auto* value = glGetString(entry);
        std::cout << "BASELINE GL[" << entry << "]: "
                  << (value ? reinterpret_cast<const char*>(value) : "unavailable") << std::endl;
    }
    for (auto entry : { GL_MAX_3D_TEXTURE_SIZE, GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,
                        GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS }) {
        GLint value = 0;
        glGetIntegerv(entry, &value);
        std::cout << "BASELINE LIMIT[" << entry << "]: " << value << std::endl;
    }
    GLint64 ssboBytes = 0;
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &ssboBytes);
    std::cout << "BASELINE MAX_SSBO_BYTES: " << ssboBytes << std::endl;
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    std::cout << "BASELINE FRAMEBUFFER: " << width << "x" << height << std::endl;
}
