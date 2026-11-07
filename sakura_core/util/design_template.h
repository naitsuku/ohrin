/*! @file */
/*
2007.10.23 kobake

デザインパターン的なモノを置けると良いなぁ。
ちなみに TSingleInstance はシングルトンパターンとは似て非なるモノですが。
*/
/*
	Copyright (C) 2008, kobake
	Copyright (C) 2018-2022, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_DESIGN_TEMPLATE_BBC57590_CED0_40D0_B719_F5A4522B8A56_H_
#define SAKURA_DESIGN_TEMPLATE_BBC57590_CED0_40D0_B719_F5A4522B8A56_H_
#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "debug/Debug2.h"

#ifdef SAKURA_WINPORT
#include <cstdint>

// [winport] 仮想プロセス単位の記憶域（winport/src/process.cpp）
namespace winport {
//! このスレッドの仮想プロセス
uint32_t CurrentProcessKey();
//! 上流が作るワーカースレッドの先頭で呼ぶ。以後このスレッドは key の仮想プロセスに属する
void AdoptProcess(uint32_t key);
//! 仮想プロセスごとの値
void* ProcessSlot(const void* tag);
void SetProcessSlot(const void* tag, void* value);
//! 仮想プロセスごとの実体。無ければ作り、仮想プロセスの終了時に作成の逆順で破棄する
//! container なら他の実体のデストラクタから触られるものとして、それらの破棄がすべて終わってから破棄する
void* ProcessSingleton(const void* tag, void* (*create)(), void (*destroy)(void*), bool container = false);
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
		// プロセス単位のシングルトンは仮想プロセスの表に持つ
		static thread_local T* cached = nullptr;
		if (!cached) {
			static char tag;
			cached = static_cast<T*>(winport::ProcessSingleton(&tag,
				[]() -> void* { return new T; },
				[](void* p) { delete static_cast<T*>(p); }));
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
				[](void* p) { delete static_cast<std::vector<T*>*>(p); }, true));
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
