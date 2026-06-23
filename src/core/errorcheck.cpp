#include "Errorcheck.h"

// デバッグモードの時のみ定義
#if defined(_DEBUG)

#include <GL/glew.h>
#include <iostream>

/// OpenGL のエラーをチェックする
/// @param[in] name エラー発生時に標準エラー出力へ出力するファイル名などの文字列。nullptrなら出力なし
/// @param[in] line エラー発生時に標準エラー出力へ出力する行番号などの整数値
auto _Errorcheck(const char* name, unsigned int line) -> void {
	const GLenum error{ glGetError() };

	if (error != GL_NO_ERROR) {
		if (name != nullptr && *name != '\0') {
			std::cerr << name;
			if (line > 0)std::cerr << "(" << line << ")";
			std::cerr << ":";
		}

		switch (error) {
		case GL_INVALID_ENUM:
			std::cerr << "An unacceptable value is specified for an enumerated argument" << std::endl;
			break;
		case GL_INVALID_VALUE:
			std::cerr << "A numeric argument is out of range" << std::endl;
			break;
		case GL_INVALID_OPERATION:
			std::cerr << "The specified operation is not allowed in the current state" << std::endl;
			break;
		case GL_OUT_OF_MEMORY:
			std::cerr << "There is not enough memory left to execute the command" << std::endl;
			break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			std::cerr << "The specified operation is not allowed current" << std::endl;
			break;
		default:
			std::cerr << "An OpenGL error has occured: " << std::hex << std::showbase << error << std::endl;
			break;
		}
	}
}

#endif