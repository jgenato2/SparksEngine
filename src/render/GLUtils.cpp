#include "GLUtils.hpp"
#include <glad/gl.h>
#include <string>
#include <fstream>
#include <vector>
#include <filesystem>
#include <stb_image.h>

// All functions have external linkage; no anonymous namespace here.

namespace GLUtils {
unsigned int compileShader(unsigned int shaderType, const char* source) {
    unsigned int shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        int length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(length, '\0');
        glGetShaderInfoLog(shader, length, nullptr, infoLog.data());
        throw std::runtime_error("Shader compilation failed: " + infoLog);
    }
    return shader;
}

unsigned int createProgram(const char* vertexSource, const char* fragmentSource) {
    unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(length, '\0');
        glGetProgramInfoLog(program, length, nullptr, infoLog.data());
        throw std::runtime_error("Program link failed: " + infoLog);
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

unsigned int loadTexture2DFromFile(const std::filesystem::path& filePath, bool clampToEdge) {
    bool isHdr = stbi_is_hdr(filePath.string().c_str()) == 1;
    int width = 0, height = 0, channels = 0;
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    if (isHdr) {
        float* pixelsHdr = stbi_loadf(filePath.string().c_str(), &width, &height, &channels, 3);
        if (!pixelsHdr || width <= 0 || height <= 0) {
            fprintf(stderr, "[GLUtils] Failed to load HDR texture: %s\n", filePath.string().c_str());
            if (pixelsHdr) stbi_image_free(pixelsHdr);
            glDeleteTextures(1, &texture);
            return 0;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, pixelsHdr);
        stbi_image_free(pixelsHdr);
    } else {
        stbi_uc* pixels = stbi_load(filePath.string().c_str(), &width, &height, &channels, 4);
        if (!pixels || width <= 0 || height <= 0) {
            fprintf(stderr, "[GLUtils] Failed to load texture: %s\n", filePath.string().c_str());
            if (pixels) stbi_image_free(pixels);
            glDeleteTextures(1, &texture);
            return 0;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        stbi_image_free(pixels);
    }
    glGenerateMipmap(GL_TEXTURE_2D);
    return texture;
}
} // namespace GLUtils
