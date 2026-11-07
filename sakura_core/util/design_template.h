/*! @file */
/*
2007.10.23 kobake

デザインパターン的なモノを置けると良いなぁ。
ちなみに TSingleInstance はシングルトンパターンとは似て非なるモノですが。
*/
/*
	Copyright (C) 2008, kobake
	Copyright (C) 2018-2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_DESIGN_TEMPLATE_BBC57590_CED0_40D0_B719_F5A4522B8A56_H_
#define SAKURA_DESIGN_TEMPLATE_BBC57590_CED0_40D0_B719_F5A4522B8A56_H_
#pragma once

#include <algorithm>
#include <concepts>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "debug/Debug2.h"

#ifdef SAKURA_WINPORT
#include <cstdint>
#include <cstring>
#include <new>

// [winport] 仮想プロセス単位の記憶域（winport/src/process.cpp）
namespace winport {
//! このスレッドの仮想プロセス
uint32_t CurrentProcessKey();
//! 上流が作るワーカースレッドの先頭で呼ぶ。以後このスレッドは key の仮想プロセスに属する
void AdoptProcess(uint32_t key);
//! 仮想プロセスごとの値
void* ProcessSlot(const void* tag);
void SetProcessSlot(const void* tag, void* value);
//! 仮想プロセスごとの実体。無ければ作り、仮想プロセスの終了時に作成の逆順で finalize（デストラクタ）を呼ぶ
//! container なら他の実体のデストラクタから触られるものとして、それらの破棄がすべて終わってから破棄する
//! create は ::operator new の記憶域に作って返す。記憶域は全部の finalize の後に ::operator delete で解放する（関数内 static と同じく、
//! 破棄済みの実体を別のデストラクタが触っても落ちない）
void* ProcessSingleton(const void* tag, void* (*create)(), void (*finalize)(void*), bool container = false);
}  // namespace winport
#endif

// http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml#Copy_Constructors
// A macro to disallow the copy constructor and operator= functions
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  TypeName& operator=(const TypeName&) = delete; \
  TypeName(TypeName&&) = delete;           \
  TypeName& operator=(TypeName&&) = delete;

/*!
	Singletonパターン

	2008.03.03 kobake 作成
*/
template <class T>
class TSingleton{
public:
	//公開インターフェース
	static T* getInstance()
	{
#ifdef SAKURA_WINPORT
		// [winport] 1 スレッド = 1 仮想プロセス（docs/porting/mac_native_plan.md）なので、
		// プロセス単位のシングルトンは仮想プロセスの表に持つ。関数内 static と同じく、コンストラクタの前に
		// 0 で埋める（上流には初期化していないメンバーを 0 のつもりで読むところがある。CEditApp::m_pcGrepAgent など）
		static thread_local T* cached = nullptr;
		if (!cached) {
			static char tag;
			cached = static_cast<T*>(winport::ProcessSingleton(&tag,
				[]() -> void* { void* m = ::operator new(sizeof(T)); std::memset(m, 0, sizeof(T)); return new (m) T; },
				[](void* p) { static_cast<T*>(p)->~T(); }));
		}
		return cached;
#else
		static T instance;
		return &instance;
#endif
	}

protected:
	TSingleton(){}
	DISALLOW_COPY_AND_ASSIGN(TSingleton);
};

/*!
 * @brief サクラエディタの変則Singletonパターン。
 *
 * TSingletonを元に作成。
 * 生成したインスタンスをリセットできるようにしてある。
 */
template <class T>
class TSakuraSingleton {
private:
	using Me = TSakuraSingleton<T>;

#ifdef SAKURA_WINPORT
	// [winport] 1 スレッド = 1 仮想プロセスなので、static では全エディターで 1 つを共有してしまい、
	// あるエディターの resetInstance が他のエディターの CEditApp を壊す。インスタンスは仮想プロセスごとに持つ
	struct Holder {
		std::unique_ptr<T> instance;
		std::recursive_mutex mutex;
	};
	static Holder& GetHolder()
	{
		static thread_local Holder* cached = nullptr;
		if (!cached) {
			static char tag;
			cached = static_cast<Holder*>(winport::ProcessSingleton(&tag,
				[]() -> void* { return new Holder; },
				[](void* p) { static_cast<Holder*>(p)->~Holder(); }));
		}
		return *cached;
	}
	//! 関数内 static だったころ（TSingleton）と同じく、コンストラクタの前に 0 で埋める
	static T* NewZeroed()
	{
		void* m = ::operator new(sizeof(T));
		std::memset(m, 0, sizeof(T));
		return new (m) T;
	}
#define gm_Mutex GetHolder().mutex
#define gm_Instance GetHolder().instance
#else
	//! 生成済みインスタンス
	static inline std::unique_ptr<T> gm_Instance = nullptr;

	//! インスタンス生成用の排他制御用ミューテックス
	static inline std::mutex gm_Mutex;
#endif

public:
	/*!
	 * @brief インスタンスポインタを取得する
	 *
	 * @returns インスタンスポインタ
	 * @note インスタンスが未生成の場合は生成する
	 */
	static T* getInstance()
	{
		static_assert(
			std::default_initializable<T> && !std::is_array_v<T>,
			"T must be publicly default-initializable and must not be an array"
		);

		std::unique_lock lock{ gm_Mutex };

		if (!gm_Instance) {
#ifdef SAKURA_WINPORT
			gm_Instance.reset(NewZeroed());
#else
			gm_Instance = std::make_unique<T>();
#endif
		}

		return gm_Instance.get();
	}

	/*!
	 * @brief インスタンスを破棄する
	 *
	 * @note 取得したポインタを破棄後に使用しないこと
	 */
	static void resetInstance()
	{
		std::unique_lock lock{ gm_Mutex };

		gm_Instance.reset();
	}

	/*!
	 * @brief インスタンスを設定する
	 *
	 * @note テスト用にモックを注入して疑似DIを実現する
	 * @note 古い設計と根本的に考え方が異なるため、注意。
	 */
	template <class U>
	static void setInstance()
	{
		static_assert(
			std::has_virtual_destructor_v<T> &&
			std::is_nothrow_destructible_v<T>,
			"T must have a virtual noexcept destructor"
		);

		std::unique_lock lock{ gm_Mutex };

		gm_Instance = std::make_unique<U>();
	}

#ifdef SAKURA_WINPORT
#undef gm_Mutex
#undef gm_Instance
#endif

protected:
	TSakuraSingleton() = default;

public:
	TSakuraSingleton(const Me&) = delete;
	Me& operator = (const Me&) = delete;

	TSakuraSingleton(Me&&) = delete;
	Me& operator = (Me&&) = delete;
};

/*!
	複数インスタンスを生成しようとしたときのエラー
 */
class multi_instance_not_allowed : public std::domain_error {
public:
	multi_instance_not_allowed()
		: std::domain_error("multi instance not allowed.") {}
};

/*!
	シングルインスタンス

	1プロセスあたりのインスタンス数を制限するためのテンプレート。
	シングルインスタンスは複数インスタンスを生成しないクラスに適用する。
	シングルインスタンスの生成済みのインスタンスはstaticメソッドから取得できる。
	インスタンス自動生成は行わないので、インスタンス生成は手動で行うこと。

	デザインパターンの「シングルトン」とは関係ないので、派生クラスは「状態」を持って良い。

	@date 2007.10.23 kobake 作成
 */
template <class T>
class TSingleInstance {
private:
	using Me = TSingleInstance<T>;

#ifdef SAKURA_WINPORT
	static thread_local T* gm_instance;	//!< [winport] 仮想プロセスの最初のスレッド用
	static inline char gm_tag = 0;		//!< [winport] 仮想プロセスの表のキー
#else
	static T* gm_instance;				//!< シングルインスタンスを保持するポインタ
#endif

public:
	/*!
		作成済みのインスタンスを取得する

		@returns 作成済みのインスタンス
		@retval nullptr インスタンスが未生成
	 */
#ifdef SAKURA_WINPORT
	// [winport] 同じ仮想プロセスの別スレッド（ワーカーなど）は表から引く
	[[nodiscard]] static T* getInstance() noexcept { return gm_instance ? gm_instance : static_cast<T*>(winport::ProcessSlot(&gm_tag)); }
#else
	[[nodiscard]] static T* getInstance() noexcept { return gm_instance; }
#endif

	TSingleInstance(const Me&) = delete;
	Me& operator = (const Me&) = delete;
	TSingleInstance(Me&&) noexcept = delete;
	Me& operator = (Me&&) noexcept = delete;

protected:
	/*!
		コンストラクタ

		staticメンバにインスタンスを記録する。
	 */
	TSingleInstance()
	{
		if (gm_instance != nullptr) {
			throw multi_instance_not_allowed();
		}
		gm_instance = static_cast<T*>(this);
#ifdef SAKURA_WINPORT
		winport::SetProcessSlot(&gm_tag, gm_instance);
#endif
	}

	/*!
		デストラクタ

		staticメンバのポインタをクリアする。
	 */
	virtual ~TSingleInstance() noexcept
	{
#ifdef SAKURA_WINPORT
		winport::SetProcessSlot(&gm_tag, nullptr);
#endif
		gm_instance = nullptr;
	}
};

/*!
	シングルインスタンスを保持するポインタ

	1プロセスあたり1つのインスタンスだけを許可する機構で、
	TSingleInstance<T>以外からはアクセスさせない。
 */
#ifdef SAKURA_WINPORT
template <class T>
thread_local T* TSingleInstance<T>::gm_instance = nullptr;
#else
template <class T>
T* TSingleInstance<T>::gm_instance = nullptr;
#endif

/*!
	インスタンスホルダー

	プロセス内で生成されたインスタンスを記録するためのテンプレート。
	インスタンスホルダーは複数インスタンスを生成するクラスに適用する。
	インスタンスホルダーの生成済みのインスタンスはstaticメソッドから取得できる。
	インスタンス自動生成は行わないので、インスタンス生成は手動で行うこと。

	デザインパターンの「シングルトン」とは関係ないので、派生クラスは「状態」を持って良い。
 */
template <class T>
class TInstanceHolder {
private:
	using Me = TInstanceHolder<T>;

#ifdef SAKURA_WINPORT
	//! [winport] 仮想プロセスごとの動的配列
	[[nodiscard]] static std::vector<T*>& Table()
	{
		static thread_local std::vector<T*>* cached = nullptr;
		if (!cached) {
			static char tag;
			cached = static_cast<std::vector<T*>*>(winport::ProcessSingleton(&tag,
				[]() -> void* { return new std::vector<T*>; },
				[](void* p) { using V = std::vector<T*>; static_cast<V*>(p)->~V(); }, true));
		}
		return *cached;
	}
#else
	static std::vector<T*> gm_table;	//!< インスタンスを保持する動的配列
	[[nodiscard]] static std::vector<T*>& Table() { return gm_table; }
#endif

public:
	/*!
		作成済みのインスタンス数を取得する

		@returns 作成済みのインスタンス数
	 */
	[[nodiscard]] static size_t GetInstanceCount() noexcept { return Table().size(); }

	/*!
		作成済みのインスタンスを取得する

		@param [in]index
		@returns 作成済みのインスタンス
		@retval nullptr インスタンスが未生成
	 */
	[[nodiscard]] static T* GetInstance(size_t index) noexcept
	{
		auto& table = Table();
		if (table.size() <= index || table.empty()) {
			return nullptr;
		}
		return table[index];
	}

	/*!
		作成済みのインスタンスを取得する

		@returns 作成済みのインスタンス
		@retval nullptr インスタンスが未生成
	 */
	[[nodiscard]] static T* getInstance() noexcept { return GetInstance(0); }

	TInstanceHolder(const Me&) = delete;
	Me& operator = (const Me&) = delete;
	TInstanceHolder(Me&&) noexcept = delete;
	Me& operator = (Me&&) noexcept = delete;

protected:
	/*!
		コンストラクタ

		staticメンバにインスタンスを記録する。
	 */
	TInstanceHolder()
	{
		Table().push_back(static_cast<T*>(this));
	}

	/*!
		デストラクタ

		staticメンバからこのインスタンスのポインタを除去する。
	 */
	virtual ~TInstanceHolder() noexcept
	{
		auto& table = Table();
		if (const auto it = std::find(table.cbegin(), table.cend(), this);
			it != table.cend())
		{
			table.erase(it);
		}
	}
};

/*!
	インスタンスを保持する動的配列

	プロセスで生成したインスタンスを記録する機構で、
	TInstanceHolder<T>以外からはアクセスさせない。
 */
#ifndef SAKURA_WINPORT
template <class T>
std::vector<T*> TInstanceHolder<T>::gm_table;
#endif

#endif /* SAKURA_DESIGN_TEMPLATE_BBC57590_CED0_40D0_B719_F5A4522B8A56_H_ */
