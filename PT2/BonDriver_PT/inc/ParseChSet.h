#pragma once

#include "Util.h"
#include "StringUtil.h"

typedef std::basic_string<_TCHAR> tstring;

#if defined(UNICODE) || defined(_UNICODE)
# define tifstream std::wifstream
//#define locale_japan() {std::locale::global(locale("jananese"));}
#else
# define tifstream std::ifstream
//#define locale_japan() {std::locale::global(locale("ja_JP.UTF-8"));}
#endif

typedef struct _CH_DATA {
	DWORD dwPT1Ch;		// iniで定義されたPT3チャンネル
	ULONG ulTSID;		// iniで定義されたTSID
	tstring tszName;	// iniで定義されたチャンネル名
	//=オペレーターの処理
	_CH_DATA(void) {
		dwPT1Ch = 0;
		ulTSID = 0;
		tszName = _T("");
	};
	~_CH_DATA(void) {
	}
	_CH_DATA& operator= (const _CH_DATA& o) {
		dwPT1Ch = o.dwPT1Ch;
		ulTSID = o.ulTSID;
		tszName = o.tszName;
		return *this;
	}
}CH_DATA;

typedef struct _SPACE_DATA {
	tstring tsKeyword;	// iniで定義されたSpaceXXX のSpaceを取った文字列をセット
	CHAR cSpace;		// iniで定義されたチャンネルスペース 0～X 使用しない物は-1
	UCHAR ucChannelCnt;	// iniで定義されたチャンネル数
	tstring tszName;	// iniで定義されたチューニングスペース名称
	vector<CH_DATA>chVec;
	_SPACE_DATA(void) {
		tsKeyword = _T("");
		cSpace = -1;
		ucChannelCnt = 0;
		tszName = _T("");
	};
	~_SPACE_DATA(void) {
	}
	_SPACE_DATA& operator= (const _SPACE_DATA& o) {
		tsKeyword = o.tsKeyword;
		cSpace = o.cSpace;
		ucChannelCnt = o.ucChannelCnt;
		tszName = o.tszName;
		chVec = o.chVec;
		return *this;
	}
}SPACE_DATA;



class CParseChSet
{
public:
	vector<SPACE_DATA> spaceVec;

public:
	CParseChSet(void);
	~CParseChSet(void);

	//ChSet.txtの読み込みを行う
	//戻り値：
	// TRUE（成功）、FALSE（失敗）
	//引数：
	// file_path		[IN]ChSet.txtのフルパス
	BOOL ParseText(
		LPCTSTR filePath
	);

protected:
	BOOL Parse1Line(tstring parseLine, SPACE_DATA& info);
	BOOL Parse1Line(tstring parseLine, CH_DATA& chInfo);
};
