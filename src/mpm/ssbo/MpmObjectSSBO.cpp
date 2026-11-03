#include "MpmObjectSSBO.h"
#include <iostream>

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

	while (glGetError() != GL_NO_ERROR) {} // エラークリア

	glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW); // SSBO を確保

	GLenum err = glGetError();
	if (err != GL_NO_ERROR) std::cerr << "Error: Failed to allocate SSBO. Code: " << err << std::endl;

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

/// デストラクタ
MpmObjectSSBO::~MpmObjectSSBO() {
	if (gridSsbo != 0) glDeleteBuffers(1, &gridSsbo);
}