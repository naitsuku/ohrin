// tests/wx_tests/test_platform_pal.cpp
//
// PAL（プラットフォーム抽象化レイヤー）の単体テスト。
// 非 Windows 環境（macOS / Linux）で wxWidgets / POSIX 実装を検証する。

#include <gtest/gtest.h>

#include "platform/platform.h"

#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace SakuraPlatform;

// ---------------------------------------------------------------
// System
// ---------------------------------------------------------------
TEST(PlatformPAL_System, ConfigDirIsCreatable) {
    auto dir = System::GetConfigDir();
    EXPECT_FALSE(dir.empty());
    fs::create_directories(dir);
    EXPECT_TRUE(fs::exists(dir));
}

TEST(PlatformPAL_System, OSNameIsNonEmpty) {
    EXPECT_FALSE(System::GetOSName().empty());
}

// ---------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------
TEST(PlatformPAL_File, ReadWriteRoundTrip) {
    auto tmp = File::GetTempFilePath(L"sakura_test_io");
    ASSERT_FALSE(tmp.empty());

    // 日本語を含むバイト列（UTF-8 で "Hello あ"）
    std::vector<char> data = {
        'H', 'e', 'l', 'l', 'o', ' ',
        static_cast<char>(0xE3),
        static_cast<char>(0x81),
        static_cast<char>(0x82),
    };
    ASSERT_TRUE(File::WriteBytes(tmp, data));
    EXPECT_TRUE(File::Exists(tmp));
    EXPECT_EQ(File::GetSize(tmp), static_cast<std::uintmax_t>(data.size()));

    std::vector<char> readback;
    ASSERT_TRUE(File::ReadBytes(tmp, readback));
    EXPECT_EQ(data, readback);

    fs::remove(tmp);
}

TEST(PlatformPAL_File, NonExistentFileReturnsFalse) {
    fs::path nonexistent = fs::temp_directory_path() / "definitely_nonexistent_sakura.bin";
    fs::remove(nonexistent);
    std::vector<char> buf;
    EXPECT_FALSE(File::ReadBytes(nonexistent, buf));
}

// ---------------------------------------------------------------
// ConfigStore
// ---------------------------------------------------------------
TEST(PlatformPAL_ConfigStore, StringRoundTrip) {
    auto store = CreateConfigStore(L"test_section");
    ASSERT_NE(store, nullptr);

    EXPECT_TRUE(store->SetString(L"test_key_str", L"テスト値ABC"));
    EXPECT_TRUE(store->Flush());

    NativeString value;
    EXPECT_TRUE(store->GetString(L"test_key_str", value));
    EXPECT_EQ(value, L"テスト値ABC");

    EXPECT_TRUE(store->DeleteKey(L"test_key_str"));
    store->Flush();
}

TEST(PlatformPAL_ConfigStore, IntRoundTrip) {
    auto store = CreateConfigStore(L"test_section");
    ASSERT_NE(store, nullptr);

    EXPECT_TRUE(store->SetInt(L"test_key_int", 42));
    EXPECT_TRUE(store->SetInt(L"test_key_neg", -7));
    store->Flush();

    int value = 0;
    EXPECT_TRUE(store->GetInt(L"test_key_int", value));
    EXPECT_EQ(value, 42);

    EXPECT_TRUE(store->GetInt(L"test_key_neg", value));
    EXPECT_EQ(value, -7);

    store->DeleteKey(L"test_key_int");
    store->DeleteKey(L"test_key_neg");
    store->Flush();
}

TEST(PlatformPAL_ConfigStore, BoolRoundTrip) {
    auto store = CreateConfigStore(L"test_section");
    ASSERT_NE(store, nullptr);

    EXPECT_TRUE(store->SetBool(L"test_key_bool", true));
    store->Flush();

    bool value = false;
    EXPECT_TRUE(store->GetBool(L"test_key_bool", value));
    EXPECT_TRUE(value);

    store->DeleteKey(L"test_key_bool");
    store->Flush();
}

TEST(PlatformPAL_ConfigStore, MissingKeyReturnsFalse) {
    auto store = CreateConfigStore(L"test_section");
    ASSERT_NE(store, nullptr);

    NativeString value;
    EXPECT_FALSE(store->GetString(L"definitely_not_set_key_xyz", value));
}

// ---------------------------------------------------------------
// Shared Memory
// ---------------------------------------------------------------
TEST(PlatformPAL_SharedMemory, CreateAndWrite) {
    const std::string name = "sakura_test_shm_v1";
    constexpr std::size_t kSize = 1024;

    auto shm = CreateSharedMemory(name, kSize, /*createNew=*/true);
    ASSERT_NE(shm, nullptr);
    ASSERT_TRUE(shm->IsValid());
    EXPECT_EQ(shm->GetSize(), kSize);

    auto* ptr = static_cast<char*>(shm->GetPtr());
    ASSERT_NE(ptr, nullptr);
    const char kPayload[] = "SharedMemory test payload";
    std::memcpy(ptr, kPayload, sizeof(kPayload));
    EXPECT_STREQ(ptr, kPayload);
}
