#pragma once

// バッファオブジェクト関連の宣言はgl.h は含まれていないので glew.h を使う
#include <GL/glew.h>

// GLM 関連
#if !defined(_USE_MATH_DEFINES)
#	define _USE_MATH_DEFINES
#endif
#if !defined(GLM_FORCE_RADIANS)
#	define GLM_FORCE_RADIANS
#endif
#include <GLM/glm.hpp>

/// 粒子の物理量
struct Particle {

	alignas(16) glm::vec4 position;// 位置
	alignas(16) glm::vec3 velocity;// 速度
	alignas(16) glm::vec3 force;// 力

};

/// 頂点配列オブジェクト
struct Object {
	
	const GLuint vao;		// 頂点配列オブジェクト
	const GLuint vbo;		// 頂点バッファオブジェクト名
	const GLsizei count;	// 頂点数

	/// コンストラクタ
	/// @param[in] count 頂点の数
	/// @param[in] data データ
	Object(GLsizei count, const void* data = nullptr);

	Object(const Object& object) = delete;				// コピーコンストラクタを禁止
	virtual ~Object();									// デコンストラクタ
	Object& operator = (const Object& object) = delete;	// 代入演算子は使用しない
	Object& operator=(Object&& object) = default;		// ムーブ代入演算子はデフォルトを使用
};