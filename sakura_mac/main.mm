/*
	main.mm — サクラエディタ mac 版のエントリポイント

	メインスレッドは Cocoa のイベントループ（表示サーバ役）に専念し、sakura 本体（上流の wWinMain）は
	仮想プロセスとして別スレッドで動かす（docs/porting/mac_native_plan.md「仮想プロセス」）。

	SPDX-License-Identifier: Zlib
*/
#import <Cocoa/Cocoa.h>

#include <cstdlib>

#include "backend.h"
#include "legacy_settings.h"

namespace {

//! 改名前（SakuraEditor.app）の設定を引き継ぐ。sakura 本体が設定を読む前に行う
void CarryOverLegacySettings()
{
	const char* home = std::getenv("HOME");	// winport の SHGetKnownFolderPath と同じ基準
	NSString* exe = [[[NSBundle mainBundle] executablePath] lastPathComponent];
	if (!home || !exe) return;
	// 上流の既定の保存先（FOLDERID_RoamingAppData / UserSubFolder "sakura"）
	const auto dir = std::filesystem::path(home) / "Library" / "Application Support" / "sakura";
	sakura_mac::CarryOverLegacySettings(dir, exe.UTF8String);
}

} // namespace

int main(int argc, char** argv)
{
	@autoreleasepool {
		CarryOverLegacySettings();
		winport::backend::Initialize();
		winport::backend::Run(argc, argv);
	}
	return 0;
}
