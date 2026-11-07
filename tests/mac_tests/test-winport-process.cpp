/*
	test-winport-process.cpp — 仮想プロセス単位のシングルトンのテスト（mac 版独自）

	上流は大きなファイルのレイアウトと読み込みを std::thread で並列化する。ワーカーは親と同じシングルトンを
	見る必要がある（別のものを作ると未初期化のまま使ってしまう）。

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <string>
#include <thread>
#include <vector>

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

namespace winport {
void ReleaseProcess(uint32_t key);
}

namespace {

std::vector<std::string> g_destroyed;

}  // namespace

// CEditApp が持つ CEditDoc のデストラクタは TInstanceHolder の一覧を触る。一覧は CEditApp より後に作られるので、
// 単純な逆順で破棄すると解放済みの一覧を触って終了時に落ちる
TEST(WinportProcess, ContainersOutliveOwnedObjects)
{
	g_destroyed.clear();
	std::thread([] {
		winport::AdoptProcess(kOtherProcess + 1);
		static char owner, container, later;
		winport::ProcessSingleton(&owner, []() -> void* { return new int; }, [](void* p) { delete static_cast<int*>(p); g_destroyed.push_back("owner"); });
		winport::ProcessSingleton(&container, []() -> void* { return new int; }, [](void* p) { delete static_cast<int*>(p); g_destroyed.push_back("container"); }, true);
		winport::ProcessSingleton(&later, []() -> void* { return new int; }, [](void* p) { delete static_cast<int*>(p); g_destroyed.push_back("later"); });
		winport::ReleaseProcess(kOtherProcess + 1);
	}).join();
	const std::vector<std::string> expected{"later", "owner", "container"};
	EXPECT_EQ(expected, g_destroyed);
}
