#include "MpmObjectSSBO.h"
#include <stdexcept>
#include <string>

/// コンストラクタ
/// @param[in] count 頂点数
/// @param[in] gridSize グリッドの解像度
MpmObjectSSBO::MpmObjectSSBO(GLsizei count, int gridSize) : IMpmObject(count, gridSize)
{
	// SSBO固有の生成処理のみ記述
	glGenBuffers(1, &gridSsbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, gridSsbo);

	size_t nodeCount = static_cast<size_t>(gridSize) * gridSize * gridSize;	// 1成分あたりのサイズを計算
	size_t bufferSize = sizeof(GLuint) * 4 * nodeCount;						// 4成分でのサイズを計算

	glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW); // SSBO を確保

	GLenum err = glGetError();
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    if (err != GL_NO_ERROR) {
        glDeleteBuffers(1, &gridSsbo);
        throw std::runtime_error("Cannot allocate the grid SSBO (OpenGL " + std::to_string(err) + ").");
    }
}

/// デストラクタ
MpmObjectSSBO::~MpmObjectSSBO() {
	if (gridSsbo != 0) glDeleteBuffers(1, &gridSsbo);
}