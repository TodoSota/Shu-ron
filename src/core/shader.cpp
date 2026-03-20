// シェーダー関連の処理
#include "shader.h"

// 標準ライブラリ
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

/// シェーダーオブジェクトのコンパイル結果を表示
/// 他から使用しないので static
/// @param[in] shader シェーダーオブジェクト名
/// @param[in] str コンパイルエラーの発生場所を示す文字列
/// return コンパイル結果が正常ならGL_TRUE、異常ならGL_FALSE
static auto printShaderInfoLog(GLuint shader, const std::string& str) -> GLboolean {
	// コンパイル結果を取得する
	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

	// シェーダーのコンパイル時のログの長さを取得
	GLsizei bufSize;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &bufSize);

	// シェーダーのコンパイル時のログがあるなら
	if (bufSize > 1) {
		// ログの内容を取得
		std::string infoLog(bufSize, '\0');
		GLsizei length;
		glGetShaderInfoLog(shader, bufSize, &length, &infoLog[0]);

		// ログの内容を表示する
		std::cerr << &infoLog << std::endl;

		std::vector<char> log(bufSize);
		glGetShaderInfoLog(shader, bufSize, nullptr, log.data());
		std::cerr << log.data() << std::endl;
	}

	if (status == GL_FALSE) std::cerr << "Compile Error in " << str << std::endl;

	// コンパイル結果を返す
	return status;
}

/// プログラムオブジェクトのリンク結果を表示する
/// param[in] program プログラムオブジェクト名
/// return リンク結果が正常なら GL_TRUE、異常なら GL_FALSE
static auto printProgramInfoLog(GLuint program) -> GLboolean {
	// リンク結果を取得
	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == GL_FALSE) std::cerr << "Link Error." << std::endl;

	// シェーダーのリンク時のログの長さを取得
	GLsizei bufSize;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufSize);

	// シェーダーのリンク時のログあるなら
	if (bufSize > 1) {
		// ログ内容を取得
		std::string infoLog(bufSize, '\0');
		GLsizei length;
		glGetProgramInfoLog(program, bufSize, &length, &infoLog[0]);

		// ログの内容を表示
		std::cerr << &infoLog << std::endl;
	}

	// リンク結果を返す
	return static_cast<GLboolean>(status);
}

/// シェーダーオブジェクトを作成しプログラムオブジェクトへ組み込む
/// @param program シェーダーオブジェクトを組み込むプログラムオブジェクトの名前
/// @param srcシェーダーのソースプログラムの文字列
/// @param msg メッセージに追加する文字列
/// @param type シェーダーの種類(GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_COMPUTE_SHADER)
/// @return シェーダーオブジェクトの作成とプログラムオブジェクトへの組み込みに成功すれば true 失敗なら false
static auto createShader(GLuint program, const std::string& src, const std::string& msg, GLenum type) -> bool {
	// プログラムオブジェクトの作成に失敗している
	if (program == 0) {
		std::cerr << "Error: Could not create program object." << std::endl;
		return false;
	}

	// シェーダーオブジェクトを作成
	const auto shader{ glCreateShader(type) };

	// シェーダーオブジェクト作成失敗
	if (shader == 0) {
		std::cerr << "Error: Could not create shader object." << std::endl;
		return false;
	}

	// シェーダーオブジェクトにソースプログラムの文字列を設定してコンパイルする
	auto ptr{ src.c_str() };
	glShaderSource(shader, 1, &ptr, nullptr);
	glCompileShader(shader);

	// コンパイル時のメッセージを表示しコンパイル結果を得る
	const auto status{ printShaderInfoLog(shader, msg) != GL_FALSE };

	// エラーがないならシェーダーオブジェクトをプログラムオブジェクトに組み込む
	if (status)glAttachShader(program, shader);

	// シェーダーのに削除マークを付ける
	glDeleteShader(shader);

	// コンパイル結果を返す
	return status;
}

/// プログラムオブジェクトを作成
/// @param[in] vsrc バーテックスシェーダのソースプログラムの文字列
/// @param[in] fsrc フラグメントシェーダのソースプログラムの文字列
/// @param[in] vmsg バーテックスシェーダのコンパイル時のメッセージに追加する文字列
/// @param[in] fmsg フラグメントシェーダのコンパイル時のメッセージに追加する文字列
/// @return 作成したプログラムオブジェクトの名前、作成できなかった場合は 0
///
/// @note この関数は、
/// バーテックスシェーダとフラグメントシェーダの両方のソースプログラムを受け取るが、
/// フラグメントシェーダのソースプログラムは空文字列でも構わない。
/// これは Transform Feedback を使用する場合などで、
/// glEnable(GL_RASTERIZER_DISCARD) としてラスタライザを無効にしたときには、
/// フラグメントシェーダが不要になるため。
/// なお、バーテックスシェーダのソースプログラムがコンパイルエラーになった場合は、
/// フラグメントシェーダのコンパイルは行わない。
auto createProgram(const std::string& vsrc, const std::string& fsrc, const std::string& vmsg, const std::string& fmsg) -> GLuint {
	// バーテックスシェーダーのソースの文字列が与えられていない時
	if (vsrc.empty()) {
		std::cerr << "Error: No vertex shader specified." << std::endl;
		return 0;
	}

	// 空のオブジェクトを作成する
	const auto program{ glCreateProgram() };

	// バーテックスシェーダーの作成と組み込みに成功すれば
	if (createShader(program, vsrc, vmsg, GL_VERTEX_SHADER)) {
		// フラグメントシェーダーがないかフラグメントシェーダーの作成と組み込みに成功すれば
		if (fsrc.empty() || createShader(program, fsrc, fmsg, GL_FRAGMENT_SHADER)) {
			// プログラムオブジェクトをリンク
			glLinkProgram(program);

			// エラーがないならプログラムオブジェクトを返す
			if (printProgramInfoLog(program))return program;
		}
	}

	// エラーならプログラムオブジェクトを削除し 0 を返す
	glDeleteProgram(program);
	return 0;
}

/// シェーダーのソースファイルを読み込む
/// @param[in] name シェーダーのソース名
/// @return 読み込みに成功すれば true ,失敗したら false
static auto readShaderSource(const std::string& name) -> std::string {
	// ソースファイルを開く
	std::ifstream  file(name, std::ios::binary);

	// ファイルを開けない場合
	if (file.fail()) {
		std::cerr << "Error: Cant not open Source file: " << name << std::endl;
		return "";
	}

	// ファイル全体を読み込む
	std::stringstream buffer;
	buffer << file.rdbuf();

	// 読み込み失敗時
	if (file.fail()) {
		std::cerr << "Error: Could not read source file: " << name << std::endl;
		return "";
	}

	//成功時
	return buffer.str();
}

/// シェーダーのソースファイルを読んでプログラムオブジェクトを作成する
/// @param[in] vert バーテックスシェーダーのソースファイル名
/// @param[in] frag フラグメントシェーダーのソースファイル名
/// @return 作成したプログラムオブジェクトの名前、作成失敗時は 0
auto loadProgram(const std::string& vert, const std::string& frag) -> GLuint {
	// バーテックスシェーダーのソースファイルが与えられていなければ
	if (vert.empty()) {
		std::cerr << "Error: No shader source file specified." << std::endl;
		return 0;
	}

	// バーテックスシェーダーのソースファイルを読む
	const std::string vsrc{ readShaderSource(vert) };

	// バーテックスシェーダーのソースファイルが読み込めなければ
	if (vsrc.empty())return 0;

	// フラグメントシェーダーのソースの文字列(初期値は empty)
	std::string fsrc;

	// フラグメントシェーダーのソースファイル名が指定されていたら
	if (!frag.empty()) {
		// フラグメントシェーダーのソースファイル名が指定されていたら
		fsrc = readShaderSource(frag);

		// 読み込み失敗
		if (fsrc.empty()) return 0;
	}

	// 両方のソースファイルを読み込めたらプログラムオブジェクトを作成する
	return createProgram(vsrc, fsrc, vert, frag);
}

/// コンピュートシェーダーのソースプログラムの文字列を読み込んでプログラムオブジェクトを作成
/// @param[in] csrc コンピュートシェーダーのソースプログラムの文字列
/// @param[in] cmsg コンピュートシェーダーのコンパイル時のメッセージに追加する文字列
/// @return プログラムオブジェクトのプログラム名、作成できなければ 0
auto createCompute(const std::string& csrc, const std::string& cmsg) -> GLuint {
	// 空のプログラムオブジェクトを作成
	const auto program{ glCreateProgram() };

	// コンピュートシェーダーの作成と組み込みに成功したら
	if (createShader(program, csrc, cmsg, GL_COMPUTE_SHADER)) {
		// プログラムオブジェクトをリンク
		glLinkProgram(program);

		// エラーがないならプログラムオブジェクトを返す
		if (printProgramInfoLog(program)) return program;
	}

	// エラーならプログラムオブジェクトを削除し 0 を返す
	glDeleteProgram(program);
	return 0;
}

/// コンピュートシェーダーのソース
/// @param[in] comp コンピュートシェーダーのソースファイル名
/// @retunr プログラムオブジェクトのプログラム名、作成できなければ 0
extern auto loadCompute(const std::string& comp) -> GLuint {
	// コンピュートシェーダーのソースファイルを読み込んで
	std::string csrc{ readShaderSource(comp) };

	// ソースファイルが読めたらプログラムオブジェクトを作成
	if (!csrc.empty()) return createCompute(csrc, comp);

	// ソースファイルを読み込み失敗なので 0 を返す
	return 0;
}