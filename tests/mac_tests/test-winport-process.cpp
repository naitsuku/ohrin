/*
	test-winport-process.cpp — 仮想プロセス単位のシングルトンのテスト（mac 版独自）

	上流は大きなファイルのレイアウトと読み込みを std::thread で並列化する。ワーカーは親と同じシングルトンを
	見る必要がある（別のものを作ると未初期化のまま使ってしまう）。

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <thread>

#include "util/design_template.h"

namespace {

class Counter : public TSingleton<Counter> {
	friend class TSingleton<Counter>;
	Counter() = default;
public:
	int value = 0;
};

class Single : public TSingleInstance<Single> {};

class Held : public TInstanceHolder<Held> {};

constexpr uint32_t kOtherProcess = 0x7FFFFFF1;

}  // namespace

TEST(WinportProcess, WorkerSharesSingletonWithParent)
{
	Counter* parent = Counter::getInstance();
	const uint32_t key = winport::CurrentProcessKey();
	Counter* seen = nullptr;
	std::thread([&] {
		winport::AdoptProcess(key);
		seen = Counter::getInstance();
	}).join();
	EXPECT_EQ(parent, seen);
}

TEST(WinportProcess, OtherProcessHasItsOwnSingleton)
{
	Counter* parent = Counter::getInstance();
	Counter* seen = nullptr;
	std::thread([&] {
		winport::AdoptProcess(kOtherProcess);
		seen = Counter::getInstance();
	}).join();
	EXPECT_NE(parent, seen);
}

TEST(WinportProcess, WorkerSeesSingleInstanceOfParentOnly)
{
	Single instance;
	const uint32_t key = winport::CurrentProcessKey();
	Single* same = nullptr;
	Single* other = &instance;
	std::thread([&] {
		winport::AdoptProcess(key);
		same = Single::getInstance();
	}).join();
	std::thread([&] {
		winport::AdoptProcess(kOtherProcess);
		other = Single::getInstance();
	}).join();
	EXPECT_EQ(&instance, same);
	EXPECT_EQ(nullptr, other);
}

TEST(WinportProcess, InstanceHolderIsSharedWithWorker)
{
	Held held;
	const uint32_t key = winport::CurrentProcessKey();
	size_t count = 0;
	std::thread([&] {
		winport::AdoptProcess(key);
		count = Held::GetInstanceCount();
	}).join();
	EXPECT_EQ(1u, count);
}
