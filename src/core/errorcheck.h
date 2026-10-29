#pragma once

//==============================================================================
// errorcheck.h : OpenGL のエラー検出 (Debug ビルドのみ有効)
// 目的: glGetError() の結果を、マクロを置いたファイル名・行番号と一緒に出力して、
//       エラーが起きた場所を絞り込めるようにする。Release ビルドでは何もしない。
//==============================================================================

#if defined(_DEBUG)
/// OpenGL のエラーチェック(APIなのでコンパイラがエラーメッセージを吐かない)
/// @param[in] name エラー発生時に標準エラー出力へ出力するファイル名などの文字列。nullptrなら出力なし
/// @param[in] line エラー発生時に標準エラー出力へ出力する行番号などの整数値
extern auto _Errorcheck(const char* name, unsigned int line) -> void;

/// OpenGL のエラー発生検知時にソースファイルの名前と行番号を示す
/// @def Errorcheck()
/// @note このマクロを置いた位置より前でエラーが発生していた時、マクロをおいたファイル名と行番号を出力
/// リリースビルドの時には無視する
# define Errorcheck() _Errorcheck(__FILE__, __LINE__)
#else
# define Errorcheck()
#endif

class Errorcheck
{
};

