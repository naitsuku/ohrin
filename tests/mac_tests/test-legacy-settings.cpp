/*
	test-legacy-settings.cpp — 旧名の設定ファイルの引き継ぎ（sakura_mac/legacy_settings.h）

	SPDX-License-Identifier: Zlib
*/
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <random>

#include "../../sakura_mac/legacy_settings.h"

namespace fs = std::filesystem;

namespace {

class LegacySettingsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		dir = fs::temp_directory_path() / ("legacy-settings-" + std::to_string(std::random_device{}()));
		fs::create_directories(dir);
	}
	void TearDown() override
	{
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
	static void Write(const fs::path& p, const std::string& text)
	{
		std::ofstream(p, std::ios::binary) << text;
	}
	static std::string Read(const fs::path& p)
	{
		std::ifstream in(p, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), {}};
	}
	fs::path dir;
};

TEST_F(LegacySettingsTest, CopiesIniProfileManagerAndProfiles)
{
	Write(dir / "SakuraEditor.ini", "main");
	Write(dir / "SakuraEditor_prof.ini", "prof");
	fs::create_directories(dir / "work");
	Write(dir / "work" / "SakuraEditor.ini", "work");

	EXPECT_EQ(3, sakura_mac::CarryOverLegacySettings(dir, "Ohrin"));
	EXPECT_EQ("main", Read(dir / "Ohrin.ini"));
	EXPECT_EQ("prof", Read(dir / "Ohrin_prof.ini"));
	EXPECT_EQ("work", Read(dir / "work" / "Ohrin.ini"));
	// 旧版のアプリも使い続けられるよう、元のファイルは残す
	EXPECT_EQ("main", Read(dir / "SakuraEditor.ini"));
}

TEST_F(LegacySettingsTest, NeverOverwritesExistingSettings)
{
	Write(dir / "SakuraEditor.ini", "old");
	Write(dir / "Ohrin.ini", "new");

	EXPECT_EQ(0, sakura_mac::CarryOverLegacySettings(dir, "Ohrin"));
	EXPECT_EQ("new", Read(dir / "Ohrin.ini"));
}

TEST_F(LegacySettingsTest, DoesNothingWithoutLegacyFilesOrWhenNotRenamed)
{
	EXPECT_EQ(0, sakura_mac::CarryOverLegacySettings(dir, "Ohrin"));
	EXPECT_FALSE(fs::exists(dir / "Ohrin.ini"));

	Write(dir / "SakuraEditor.ini", "main");
	EXPECT_EQ(0, sakura_mac::CarryOverLegacySettings(dir, "SakuraEditor"));
	EXPECT_EQ(0, sakura_mac::CarryOverLegacySettings(dir / "missing", "Ohrin"));
}

} // namespace
