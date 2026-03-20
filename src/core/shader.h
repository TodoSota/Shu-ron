#pragma once

/// シェーダー関連の処理
// シェーダー関連の処理には gl.h に含まれていないので glew.h を使う
#include <GL/glew.h>

// 標準ライブラリ
#include <string>

/// プログラムオブジェクトを作成する
/// @param[in] vsrc バーテックスシェーダーのソースプログラムの文字列
/// @param[in] fsrc フラグメントシェーダーのソースプログラムの文字列
/// @param[in] vert バーテックスシェーダーのコンパイル時のメッセージに追加する文字列
/// @param[in] frag フラグメントシェーダーのコンパイル時のメッセージに追加する文字列
/// @return 作成したプログラムオブジェクトの名前、作成できなかったら 0
extern auto createProgram(const std::string& vsrc, const std::string& fsrc, const std::string& vert = "vertex shader", const std::string& frag = "fragment shader") -> GLuint;

/// シェーダーオブジェクトのソースファイルを読み込んでプログラムオブジェクトを作成する
/// @param[in] vsrc バーテックスシェーダーのソースファイル名
/// @param[in] fsrc フラグメントシェーダーのソースファイル名
/// @return 作成したプログラムオブジェクトの名前、作成できなかったら 0
extern auto loadProgram(const std::string& vert, const std::string& frag) -> GLuint;

/// コンピュートシェーダーのソースプログラムの文字列を読み込んでプログラムオブジェクトを作成
/// @param[in] csrc コンピュートシェーダーのソースプログラムの文字列
/// @param[in] cmsg コンピュートシェーダーのコンパイル時のメッセージに追加する文字列
/// @return プログラムオブジェクトのプログラム名、作成できなければ 0
extern auto createCompute(const std::string& csrc, const std::string& cmsg) -> GLuint;

/// コンピュートシェーダーのソース
/// @param[in] comp コンピュートシェーダーのソースファイル名
/// @retunr プログラムオブジェクトのプログラム名、作成できなければ 0
extern auto loadCompute(const std::string& comp) -> GLuint;