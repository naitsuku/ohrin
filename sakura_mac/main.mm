/*
	main.mm — サクラエディタ mac 版のエントリポイント

	メインスレッドは Cocoa のイベントループ（表示サーバ役）に専念し、sakura 本体（上流の wWinMain）は
	仮想プロセスとして別スレッドで動かす（docs/porting/mac_native_plan.md「仮想プロセス」）。

	SPDX-License-Identifier: Zlib
*/
#import <Cocoa/Cocoa.h>

#include "backend.h"

int main(int argc, char** argv)
{
	@autoreleasepool {
		winport::backend::Initialize();
		winport::LaunchFirstProcess(argc, argv);
		winport::backend::Run();
	}
	return 0;
}
