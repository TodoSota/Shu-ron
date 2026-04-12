#pragma once

#if defined(_DEBUG)
/// OpenGL のエラーチェック(APIなのでコンパイラがエラーメッセージを吐かない)
/// @param[in] name エラー発生時に標準エラー出力へ出力するファイル名などの文字列。nullptrなら出力なし
/// @param[in] line エラー発生時に標準エラー出力へ出力する行番号などの整数値
extern auto _errorcheck(const char* name, unsigned int line) -> void;

/// OpenGL のエラー発生検知時にソースファイルの名前と行番号を示す
/// @def errorcheck()
/// @note このマクロを置いた位置より前でエラーが発生していた時、マクロをおいたファイル名と行番号を出力
/// リリースビルドの時には無視する
# define errorcheck() _errorcheck(__FILE__, __LINE__)
#else
# define errorcheck()
#endif

class errorcheck
{
};

