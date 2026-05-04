/**
 * platform.h - サクラエディタ プラットフォーム抽象化レイヤー (PAL)
 *
 * Win32 API への直接依存を排除し、wxWidgets / POSIX 実装に
 * 切り替えられるようにするための抽象化インターフェース。
 *
 * 使用方法:
 *   #include "platform/platform.h"
 *   // プラットフォーム非依存のコードを記述
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace SakuraPlatform {

// ============================================================
// 文字列型の統一
// ============================================================
// Windows でも Linux/macOS でも wchar_t ベースの文字列を使用
using NativeString = std::wstring;
using NativeStringView = std::wstring_view;

// ============================================================
// ファイル操作
// ============================================================
namespace File {
    /**
     * ファイルの内容をバイト配列として読み込む
     * @return 成功時 true、失敗時 false（エラーは GetLastError/errno で取得）
     */
    bool ReadBytes(const std::filesystem::path& path,
                   std::vector<char>& buffer);

    /**
     * バイト配列をファイルに書き込む（上書き）
     */
    bool WriteBytes(const std::filesystem::path& path,
                    const std::vector<char>& buffer);

    /**
     * ファイルが存在するか確認
     */
    bool Exists(const std::filesystem::path& path);

    /**
     * ファイルサイズを取得（バイト数）
     */
    std::uintmax_t GetSize(const std::filesystem::path& path);

    /**
     * 一時ファイルのパスを生成
     */
    std::filesystem::path GetTempFilePath(const std::wstring& prefix = L"sakura");
} // namespace File

// ============================================================
// 設定ストア（レジストリ / INIファイル の抽象化）
// ============================================================
class IConfigStore {
public:
    virtual ~IConfigStore() = default;

    virtual bool GetString(NativeStringView key, NativeString& value) = 0;
    virtual bool SetString(NativeStringView key, NativeStringView value) = 0;
    virtual bool GetInt(NativeStringView key, int& value) = 0;
    virtual bool SetInt(NativeStringView key, int value) = 0;
    virtual bool GetBool(NativeStringView key, bool& value) = 0;
    virtual bool SetBool(NativeStringView key, bool value) = 0;
    virtual bool DeleteKey(NativeStringView key) = 0;
    virtual bool Flush() = 0;
};

/**
 * プラットフォームに応じた IConfigStore 実装を返す
 * Windows: レジストリ実装
 * 非Windows: INIファイル実装（~/.config/sakura/sakura.ini）
 */
std::unique_ptr<IConfigStore> CreateConfigStore(NativeStringView section);

// ============================================================
// クリップボード
// ============================================================
namespace Clipboard {
    bool HasText();
    bool GetText(NativeString& text);
    bool SetText(NativeStringView text);
    void Clear();
} // namespace Clipboard

// ============================================================
// メッセージ・ダイアログ
// ============================================================
namespace Dialog {
    enum class MessageType { Info, Warning, Error, Question };

    void ShowMessage(const NativeString& message,
                     const NativeString& title = L"サクラエディタ",
                     MessageType type = MessageType::Info);

    bool ShowQuestion(const NativeString& message,
                      const NativeString& title = L"確認");

    /**
     * ファイルを開くダイアログ
     * @return 選択されたファイルパス（キャンセル時は空）
     */
    std::filesystem::path ShowOpenFileDialog(
        const NativeString& title = L"ファイルを開く",
        const NativeString& filter = L"すべてのファイル (*.*)|*.*");

    std::filesystem::path ShowSaveFileDialog(
        const NativeString& title = L"名前を付けて保存",
        const NativeString& filter = L"すべてのファイル (*.*)|*.*",
        const std::filesystem::path& defaultPath = {});
} // namespace Dialog

// ============================================================
// システム情報
// ============================================================
namespace System {
    /**
     * ユーザー設定ディレクトリを取得
     * Windows: %APPDATA%\sakura
     * macOS: ~/Library/Application Support/SakuraEditor
     * Linux: ~/.config/sakura
     */
    std::filesystem::path GetConfigDir();

    /**
     * アプリケーションの実行ファイルのディレクトリ
     */
    std::filesystem::path GetExecutableDir();

    /**
     * OS名を返す（ログ用）
     */
    std::string GetOSName();
} // namespace System

// ============================================================
// IPC（プロセス間通信）
// ============================================================
/**
 * 共有メモリの抽象化
 * Windows: CreateFileMapping
 * 非Windows: POSIX shm_open
 */
class ISharedMemory {
public:
    virtual ~ISharedMemory() = default;
    virtual void* GetPtr() = 0;
    virtual const void* GetPtr() const = 0;
    virtual std::size_t GetSize() const = 0;
    virtual bool IsValid() const = 0;
};

std::unique_ptr<ISharedMemory> CreateSharedMemory(
    const std::string& name,
    std::size_t size,
    bool createNew = true);

} // namespace SakuraPlatform
