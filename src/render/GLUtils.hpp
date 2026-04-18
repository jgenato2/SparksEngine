
#pragma once
#include <string>
#include <filesystem>

namespace GLUtils {
	unsigned int compileShader(unsigned int shaderType, const char* source);
	unsigned int createProgram(const char* vertexSource, const char* fragmentSource);
	unsigned int loadTexture2DFromFile(const std::filesystem::path& filePath, bool clampToEdge);
}
