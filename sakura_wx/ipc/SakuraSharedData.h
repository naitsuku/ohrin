#pragma once
#include "platform/platform.h"

#include <atomic>
#include <cstdint>
#include <memory>

/**
 * SakuraSharedData — wxWidgets 版の最小プロセス間共有データ。
 *
 * 上流の DLLSHAREDATA 構造体は sakura_core/env/ に多数の依存があり、
 * 現状（StdAfx.h 解体未完）では非 Windows でコンパイル不可能。
 * 本クラスは PAL ISharedMemory を使った最小実装を提供し、
 * Phase 6 以降で上流 DLLSHAREDATA を取り込み可能になった時点で統合する。
 *
 * 共有内容（最小）:
 *   - 起動中インスタンス数（プロセスカウンタ）
 *   - 最後のフォーカスウィンドウ位置（同名インスタンスへのフォーカス転送用、
 *     現状は記録のみ）
 *
 * 実装方針:
 *   - 名前付き共有メモリ "sakura_editor_wxport_v1" を使う
 *   - 先頭プロセスが新規作成、以降はアタッチ
 *   - std::atomic<uint32_t> でインスタンスカウンタを管理
 *   - 異常終了対策として、Open() のたびにマジックナンバーを検査し、
 *     破損していたら再初期化する
 */
namespace sakura_wx::ipc {

struct SharedHeader {
    static constexpr uint32_t kMagic   = 0x53414B55u; // 'SAKU'
    static constexpr uint32_t kVersion = 1u;

    uint32_t          magic;
    uint32_t          version;
    std::atomic<uint32_t> instanceCount;
    int32_t           lastWindowX;
    int32_t           lastWindowY;
    int32_t           lastWindowW;
    int32_t           lastWindowH;
    uint32_t          reserved[8];
};
static_assert(sizeof(SharedHeader) <= 4096, "SharedHeader must fit in a page");

class SakuraSharedData {
public:
    SakuraSharedData();
    ~SakuraSharedData();

    SakuraSharedData(const SakuraSharedData&) = delete;
    SakuraSharedData& operator=(const SakuraSharedData&) = delete;

    /**
     * 共有メモリを開く（起動時に呼ぶ）。
     * @return true 正常に開けた場合
     */
    bool Open();

    /**
     * 共有メモリを閉じる（終了時に呼ぶ）。
     * 最後のインスタンスならカウンタは 0 になる。
     */
    void Close();

    /**
     * 自プロセスが最初のインスタンスかどうか。
     * Open() 直後に有効。
     */
    bool IsPrimaryInstance() const { return m_isPrimary; }

    /**
     * 起動中インスタンスの総数。
     */
    uint32_t GetInstanceCount() const;

    /**
     * 最後のウィンドウ位置を記録（プロセス間でフォーカス転送する場合の参考値）。
     */
    void SetLastWindowGeometry(int x, int y, int w, int h);

    /**
     * 最後のウィンドウ位置を取得。
     */
    bool GetLastWindowGeometry(int& x, int& y, int& w, int& h) const;

private:
    SharedHeader* Header() const;

    std::unique_ptr<SakuraPlatform::ISharedMemory> m_shm;
    bool m_isPrimary = false;
    bool m_attached  = false;
};

} // namespace sakura_wx::ipc
