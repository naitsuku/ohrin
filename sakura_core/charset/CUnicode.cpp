/*! @file */
/*
	Copyright (C) 2018-2022, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/

#include "StdAfx.h"
#include "CUnicode.h"
#include "codechecker.h"
#include "mem/CMemory.h"

#ifdef SAKURA_WINPORT
// [winport] wchar_t は 4 バイト（中身は UTF-16 コード単位）なので、
// UTF-16LE/BE のバイト列と CNativeW の間で 2 バイト ⇔ 1 要素の変換が要る。
EConvertResult CUnicode::_UnicodeToUnicode_in( const CMemory& cSrc, CNativeW* pDstMem, const bool bBigEndian )
{
	const size_t nSrcLen = cSrc.GetRawLength();
	const auto pSrc = reinterpret_cast<const unsigned char*>(cSrc.GetRawPtr());
	// cSrc と出力先が同じメモリのことがあるので、先に変換結果を作る
	std::wstring units;
	units.reserve(nSrcLen / 2 + 1);
	for (size_t i = 0; i + 1 < nSrcLen; i += 2) {
		units += wchar_t(bBigEndian ? (pSrc[i] << 8 | pSrc[i + 1]) : (pSrc[i + 1] << 8 | pSrc[i]));
	}
	EConvertResult res = RESULT_COMPLETE;
	if (nSrcLen % 2 == 1) {
		// 奇数長は末尾に 0 バイトを補った 1 単位として扱う（Windows 版と同じ）
		const unsigned last = pSrc[nSrcLen - 1];
		units += wchar_t(bBigEndian ? (last << 8) : last);
		res = RESULT_LOSESOME;
	}
	pDstMem->SetString(units.data(), units.size());
	return res;
}

EConvertResult CUnicode::_UnicodeToUnicode_out( const CNativeW& cSrc, CMemory* pDstMem, const bool bBigEndian )
{
	const size_t nLen = cSrc.GetStringLength();
	const wchar_t* p = cSrc.GetStringPtr();
	std::string bytes(nLen * 2, '\0');
	for (size_t i = 0; i < nLen; ++i) {
		const auto u = static_cast<unsigned>(p[i]) & 0xFFFF;
		bytes[i * 2 + (bBigEndian ? 0 : 1)] = char(u >> 8);
		bytes[i * 2 + (bBigEndian ? 1 : 0)] = char(u & 0xFF);
	}
	pDstMem->SetRawData(bytes.data(), bytes.size());
	return RESULT_COMPLETE;
}
#else
EConvertResult CUnicode::_UnicodeToUnicode_in( const CMemory& cSrc, CNativeW* pDstMem, const bool bBigEndian )
{
	// ソース取得
	int nSrcLen = cSrc.GetRawLength();
	const unsigned char* pSrc = reinterpret_cast<const unsigned char*>( cSrc.GetRawPtr() );
	CMemory* pDstMem2 = pDstMem->_GetMemory();

	EConvertResult res = RESULT_COMPLETE;
	bool bCopy = false;
	if( nSrcLen % 2 == 1 ){
		// 不足分の最終1バイトとして 0x00 を補う。
		pDstMem2->AllocBuffer( nSrcLen + 1 );
		unsigned char* pDst  = reinterpret_cast<unsigned char*>( pDstMem2->GetRawPtr() );
		if( pDstMem2->GetRawPtr() != nullptr ){
			if( &cSrc != pDstMem2 ){
				pDstMem2->SetRawDataHoldBuffer(pSrc, nSrcLen);
				bCopy = true;
			}
			pDst[nSrcLen] = 0;
			pDstMem2->_SetRawLength(nSrcLen + 1);
			res = RESULT_LOSESOME;
		}else{
			return RESULT_FAILURE;
		}
	}

	if( bBigEndian ){
		if( &cSrc != pDstMem2 && !bCopy ){
			pDstMem2->SetRawDataHoldBuffer( cSrc );
		}
		pDstMem2->SwapHLByte();  // UnicodeBe -> Unicode
	}else if( !bCopy ){
		pDstMem2->SetRawDataHoldBuffer(pSrc, nSrcLen);
	}
	return res;
}

EConvertResult CUnicode::_UnicodeToUnicode_out( const CNativeW& cSrc, CMemory* pDstMem, const bool bBigEndian )
{
	if( bBigEndian == true ){
		if( cSrc._GetMemory() != pDstMem ){
			pDstMem->SetRawDataHoldBuffer( *(cSrc._GetMemory()) );
		}
		pDstMem->SwapHLByte();   // Unicode -> UnicodeBe
	}else{
		if( cSrc._GetMemory() != pDstMem ){
			pDstMem->SetRawDataHoldBuffer(*(cSrc._GetMemory()));
		}else{
			// 何もしない
		}
	}

	return RESULT_COMPLETE;   // 何もしない
}
#endif  // SAKURA_WINPORT
