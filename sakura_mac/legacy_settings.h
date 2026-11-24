/*
	legacy_settings.h — 旧名（SakuraEditor）の設定ファイルを新しい名前へ引き継ぐ

	設定ファイルの名前は「実行ファイル名 + .ini」で決まる（上流の CProcess::GetIniFileName ほか）。
	アプリを SakuraEditor から改名すると、同じフォルダーでも別の名前の設定ファイルを読みにいくため、
	初回起動時に旧名のファイルを新しい名前へ**コピー**する。旧版のアプリと同時に使われても
	同じファイルを奪い合わないよう、移動ではなくコピーにしている。既にあるファイルは上書きしない。

	対象（フォルダーは ~/Library/Application Support/sakura。上流の既定の UserSubFolder）:
	  <旧>.ini         → <新>.ini           … 設定本体
	  <旧>_prof.ini    → <新>_prof.ini      … プロファイルマネージャー
	  <プロファイル>/<旧>.ini → <プロファイル>/<新>.ini

	SPDX-License-Identifier: Zlib
*/
#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace sakura_mac {

//! 改名前の実行ファイル名
inline constexpr const char* kLegacyExeName = "SakuraEditor";

/*!
	@param dir      設定フォルダー
	@param newStem  今の実行ファイル名（拡張子なし）
	@return コピーしたファイルの数
*/
inline int CarryOverLegacySettings(const std::filesystem::path& dir, const std::string& newStem,
	const std::string& legacyStem = kLegacyExeName)
{
	namespace fs = std::filesystem;
	if (newStem.empty() || newStem == legacyStem) return 0;

	std::error_code ec;
	if (!fs::is_directory(dir, ec)) return 0;

	int copied = 0;
	auto carry = [&](const fs::path& from, const fs::path& to) {
		std::error_code e;
		if (!fs::is_regular_file(from, e) || fs::exists(to, e)) return;
		if (fs::copy_file(from, to, fs::copy_options::skip_existing, e) && !e) ++copied;
	};

	carry(dir / (legacyStem + ".ini"), dir / (newStem + ".ini"));
	carry(dir / (legacyStem + "_prof.ini"), dir / (newStem + "_prof.ini"));
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		std::error_code e;
		if (!entry.is_directory(e)) continue;
		carry(entry.path() / (legacyStem + ".ini"), entry.path() / (newStem + ".ini"));
	}
	return copied;
}

} // namespace sakura_mac
