#include "SakuraSharedData.h"

#include <cstring>

namespace sakura_wx::ipc {

namespace {
constexpr const char* kShmName = "sakura_editor_wxport_v1";
constexpr std::size_t kShmSize = 4096;
} // namespace

SakuraSharedData::SakuraSharedData() = default;

SakuraSharedData::~SakuraSharedData() {
    Close();
}

bool SakuraSharedData::Open() {
    if (m_attached) return true;

    // まず既存共有メモリへのアタッチを試みる
    auto existing = SakuraPlatform::CreateSharedMemory(
        kShmName, kShmSize, /*createNew=*/false);
    if (existing && existing->IsValid()) {
        m_shm = std::move(existing);
        // ヘッダの妥当性をチェック
        auto* h = Header();
        if (h && h->magic == SharedHeader::kMagic
              && h->version == SharedHeader::kVersion) {
            m_isPrimary = false;
            h->instanceCount.fetch_add(1, std::memory_order_acq_rel);
            m_attached = true;
            return true;
        }
        // 破損または互換性なし → 既存を捨てて新規作成にフォールスルー
        m_shm.reset();
    }

    // 新規作成
    m_shm = SakuraPlatform::CreateSharedMemory(
        kShmName, kShmSize, /*createNew=*/true);
    if (!m_shm || !m_shm->IsValid()) {
        return false;
    }
    auto* h = Header();
    if (!h) return false;

    std::memset(h, 0, sizeof(SharedHeader));
    h->magic   = SharedHeader::kMagic;
    h->version = SharedHeader::kVersion;
    new (&h->instanceCount) std::atomic<uint32_t>{1};

    m_isPrimary = true;
    m_attached  = true;
    return true;
}

void SakuraSharedData::Close() {
    if (!m_attached) return;
    if (auto* h = Header()) {
        // インスタンスカウンタを 1 減らす（0 未満にならないようガード）
        uint32_t cur = h->instanceCount.load(std::memory_order_acquire);
        while (cur > 0) {
            if (h->instanceCount.compare_exchange_weak(
                    cur, cur - 1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                break;
            }
        }
    }
    m_shm.reset();
    m_attached  = false;
    m_isPrimary = false;
}

uint32_t SakuraSharedData::GetInstanceCount() const {
    if (auto* h = Header()) {
        return h->instanceCount.load(std::memory_order_acquire);
    }
    return 0;
}

void SakuraSharedData::SetLastWindowGeometry(int x, int y, int w, int h) {
    if (auto* hdr = Header()) {
        hdr->lastWindowX = x;
        hdr->lastWindowY = y;
        hdr->lastWindowW = w;
        hdr->lastWindowH = h;
    }
}

bool SakuraSharedData::GetLastWindowGeometry(int& x, int& y, int& w, int& h) const {
    if (auto* hdr = Header()) {
        // 0 は未設定とみなす（最初のプロセス起動直後）
        if (hdr->lastWindowW == 0 && hdr->lastWindowH == 0) {
            return false;
        }
        x = hdr->lastWindowX;
        y = hdr->lastWindowY;
        w = hdr->lastWindowW;
        h = hdr->lastWindowH;
        return true;
    }
    return false;
}

SharedHeader* SakuraSharedData::Header() const {
    if (!m_shm) return nullptr;
    return static_cast<SharedHeader*>(m_shm->GetPtr());
}

} // namespace sakura_wx::ipc
