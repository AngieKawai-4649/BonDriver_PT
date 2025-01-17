#include "stdafx.h"
#include <Windows.h>
#include <process.h>

#include "EARTH_PT.h"
#include "OS_Library.h"
using namespace EARTH;

#include "BonTuner.h"

static BOOL g_isISDB_S = FALSE;

#define DATA_BUFF_SIZE 188*256
#define MAX_BUFF_COUNT 500

#pragma warning( push )
#pragma warning( disable : 4273 )
extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{
	// 同一プロセスからの複数インスタンス取得禁止
	// (非同期で取得された場合の排他処理がちゃんと出来ていないが放置)
	CBonTuner *p = NULL;
	if (CBonTuner::m_pThis == NULL) {
		p = new CBonTuner;
	}
	return p;
}
#pragma warning( pop )

CBonTuner * CBonTuner::m_pThis = NULL;
HINSTANCE CBonTuner::m_hModule = NULL;
BOOL g_setupCriticalSection = FALSE;


CBonTuner::CBonTuner():
	  m_hOnStreamEvent(NULL)
	, m_LastBuff(NULL)
	, m_dwCurSpace(0xff)
	, m_dwCurChannel(0xff)
	, m_iID(-1)
	, m_CriticalSection{}
	, m_hThread(NULL)
	, m_iTunerID(0)
	, m_tchTunerName{}
	, m_tchPT1CtrlExe{}
	, m_dwSetChDelay(0)
	, m_hStopEvent(NULL)
{
	m_pThis = this;
}

CBonTuner::~CBonTuner()
{
	CloseTuner();

	if (g_setupCriticalSection) {
		::EnterCriticalSection(&m_CriticalSection);
		SAFE_DELETE(m_LastBuff);
		::LeaveCriticalSection(&m_CriticalSection);

		if (m_hStopEvent) {
			::CloseHandle(m_hStopEvent);
		}
		m_hStopEvent = NULL;

		::DeleteCriticalSection(&m_CriticalSection);
		g_setupCriticalSection = FALSE;
	}

	m_pThis = NULL;
}

const BOOL CBonTuner::OpenTuner(void)
{

	m_hStopEvent = _CreateEvent(FALSE, FALSE, NULL);
	if (m_hStopEvent == 0) {
		return FALSE;
	}
	if (!g_setupCriticalSection) {
		__try {
			::InitializeCriticalSection(&m_CriticalSection);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return FALSE;
		}
		g_setupCriticalSection = TRUE;
	}

	TCHAR strExePath[512] = _T("");
	GetModuleFileName(m_hModule, strExePath, 512);

	TCHAR szPath[_MAX_PATH];	// パス
	TCHAR szDrive[_MAX_DRIVE];
	TCHAR szDir[_MAX_DIR];
	TCHAR szFname[_MAX_FNAME];
	TCHAR szExt[_MAX_EXT];
	_tsplitpath_s(strExePath, szDrive, _MAX_DRIVE, szDir, _MAX_DIR, szFname, _MAX_FNAME, szExt, _MAX_EXT);
	_tmakepath_s(szPath, _MAX_PATH, szDrive, szDir, NULL, NULL);

	_tcscpy_s(m_tchPT1CtrlExe, MAX_PATH, szPath);
	_tcsncat_s(m_tchPT1CtrlExe, MAX_PATH - _tcslen(m_tchPT1CtrlExe), _T("PTCtrl.exe"), _MAX_FNAME);

	struct _stat64 s;
	if (_tstat64(m_tchPT1CtrlExe, &s) < 0) {
		return FALSE;
	}


	TCHAR tchIni[_MAX_PATH];
	_tcscpy_s(tchIni, MAX_PATH, szPath);
	_tcsncat_s(tchIni, MAX_PATH - _tcslen(tchIni), _T("BonDriver_PT-ST.ini"), _MAX_FNAME);

	int iPTn = GetPrivateProfileInt(_T("SET"), _T("PTver"), 2, tchIni);
	if ((iPTn != 2) && (iPTn != 1)) {
		iPTn = 2;
	}

	m_dwSetChDelay = GetPrivateProfileInt(_T("SET"), _T("SetChDelay"), 0, tchIni);

	TCHAR tchChSet[_MAX_PATH];
	_tcscpy_s(tchChSet, MAX_PATH, szPath);
	LPCTSTR ttname = (iPTn == 2) ? _T("PT2 ISDB-T") : _T("PT1 ISDB-T");
	LPCTSTR tsname = (iPTn == 2) ? _T("PT2 ISDB-S") : _T("PT1 ISDB-S");

	if (_tcslen(szFname) >= _tcslen(_T("BonDriver_PT-*"))) {
		if (szFname[13] == _T('T')) {
			_tcscpy_s(m_tchTunerName, _MAX_FNAME, ttname);
			_tcsncat_s(tchChSet, MAX_PATH - _tcslen(tchChSet), _T("BonDriver_PT-T.ChSet.txt"), _MAX_FNAME);
		}
		else {
			g_isISDB_S = TRUE;
			_tcscpy_s(m_tchTunerName, _MAX_FNAME, tsname);
			_tcsncat_s(tchChSet, MAX_PATH - _tcslen(tchChSet), _T("BonDriver_PT-S.ChSet.txt"), _MAX_FNAME);
		}
		if (_tcslen(szFname) > _tcslen(_T("BonDriver_PT-*"))) {
			LPTSTR endptr;
			int tid;
			tid = _tcstoul(szFname + _tcslen(_T("BonDriver_PT-*")), &endptr, 10);
			if (*endptr == _T('\0')) {
				m_iTunerID = tid;
				_stprintf_s(m_tchTunerName + _tcslen(m_tchTunerName), _MAX_FNAME - _tcslen(m_tchTunerName), _T(" (%d)"), m_iTunerID);
			}
		}
	}

	if (_tstat64(tchChSet, &s) < 0) {
		return FALSE;
	}

	BOOL bRet = m_chSet.ParseText(tchChSet);
	if (!bRet) {
		return FALSE;
	}

	//イベント
	m_hOnStreamEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);

	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	 bRet = CreateProcess(NULL, m_tchPT1CtrlExe, NULL, NULL, FALSE, GetPriorityClass(GetCurrentProcess()), NULL, NULL, &si, &pi);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	DWORD dwRet;
	if (m_iTunerID >= 0) {
		dwRet = SendOpenTuner2(g_isISDB_S, m_iTunerID, &m_iID);
	}
	else {
		dwRet = SendOpenTuner(g_isISDB_S, &m_iID);
	}

	_RPT3(_CRT_WARN, "*** CBonTuner::OpenTuner() ***\nm_hOnStreamEvent[%p] bRet[%s] dwRet[%u]\n", m_hOnStreamEvent, bRet ? "TRUE" : "FALSE", dwRet);

	if (dwRet != CMD_SUCCESS) {
		return FALSE;
	}

	m_hThread = (HANDLE)_beginthreadex(NULL, 0, RecvThread, (LPVOID)this, CREATE_SUSPENDED, NULL);
	if (m_hThread == 0) {
		return FALSE;
	}
	else {
		ResumeThread(m_hThread);
	}

	return TRUE;
}

void CBonTuner::CloseTuner(void)
{
	if( m_hThread != NULL ){
		::SetEvent(m_hStopEvent);
		// スレッド終了待ち
		if ( ::WaitForSingleObject(m_hThread, 15000) == WAIT_TIMEOUT ){
			::TerminateThread(m_hThread, 0xffffffff);
		}
		CloseHandle(m_hThread);
		m_hThread = NULL;
	}

	m_dwCurSpace = 0xFF;
	m_dwCurChannel = 0xFF;

	::CloseHandle(m_hOnStreamEvent);
	m_hOnStreamEvent = NULL;

	if( m_iID != -1 ){
		SendCloseTuner(m_iID);
		m_iID = -1;
	}

	if (g_setupCriticalSection) {
		//バッファ解放
		::EnterCriticalSection(&m_CriticalSection);
		while (!m_TsBuff.empty()) {
			TS_DATA* p = m_TsBuff.front();
			m_TsBuff.pop_front();
			delete p;
		}
		::LeaveCriticalSection(&m_CriticalSection);
	}
}

const BOOL CBonTuner::SetChannel(const BYTE bCh)
{
	return TRUE;
}

const float CBonTuner::GetSignalLevel(void)
{
	if( m_iID == -1 ){
		return 0;
	}
	DWORD dwCn100;
	if( SendGetSignal(m_iID, &dwCn100) == CMD_SUCCESS ){
		return ((float)dwCn100) / 100.0f;
	}else{
		return 0;
	}
}

const DWORD CBonTuner::WaitTsStream(const DWORD dwTimeOut)
{
	if( m_hOnStreamEvent == NULL ){
		return WAIT_ABANDONED;
	}
	// イベントがシグナル状態になるのを待つ
	const DWORD dwRet = ::WaitForSingleObject(m_hOnStreamEvent, (dwTimeOut)? dwTimeOut : INFINITE);

	switch(dwRet){
		case WAIT_ABANDONED :
			// チューナが閉じられた
			return WAIT_ABANDONED;

		case WAIT_OBJECT_0 :
		case WAIT_TIMEOUT :
			// ストリーム取得可能
			return dwRet;

		case WAIT_FAILED :
		default:
			// 例外
			return WAIT_FAILED;
	}
}

const DWORD CBonTuner::GetReadyCount(void)
{
	DWORD dwCount = 0;
	::EnterCriticalSection(&m_CriticalSection);
	dwCount = (DWORD)m_TsBuff.size();
	::LeaveCriticalSection(&m_CriticalSection);
	return dwCount;
}

const BOOL CBonTuner::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = NULL;

	if(GetTsStream(&pSrc, pdwSize, pdwRemain)){
		if(*pdwSize){
			::CopyMemory(pDst, pSrc, *pdwSize);
		}
		return TRUE;
	}
	
	return FALSE;
}

const BOOL CBonTuner::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BOOL bRet;
	::EnterCriticalSection(&m_CriticalSection);
	if( m_TsBuff.size() != 0 ){
		delete m_LastBuff;
		m_LastBuff = m_TsBuff.front();
		m_TsBuff.pop_front();
		*pdwSize = m_LastBuff->dwSize;
		*ppDst = m_LastBuff->pbBuff;
		*pdwRemain = (DWORD)m_TsBuff.size();
		bRet = TRUE;
	}else{
		*pdwSize = 0;
		*pdwRemain = 0;
		bRet = FALSE;
	}
	::LeaveCriticalSection(&m_CriticalSection);
	return bRet;
}

void CBonTuner::PurgeTsStream(void)
{
	//バッファ解放
	::EnterCriticalSection(&m_CriticalSection);
	while (!m_TsBuff.empty()){
		TS_DATA *p = m_TsBuff.front();
		m_TsBuff.pop_front();
		delete p;
	}
	::LeaveCriticalSection(&m_CriticalSection);
}

LPCTSTR CBonTuner::GetTunerName(void)
{
	return m_tchTunerName;
}

const BOOL CBonTuner::IsTunerOpening(void)
{
	return FALSE;
}

LPCTSTR CBonTuner::EnumTuningSpace(const DWORD dwSpace)
{
	vector<SPACE_DATA>::iterator itr = find_if(m_chSet.spaceVec.begin(), m_chSet.spaceVec.end(), [dwSpace](const SPACE_DATA& s) {return(s.cSpace == dwSpace); });
	return((itr == m_chSet.spaceVec.end()) ? NULL : itr->tszName.c_str());
}

LPCTSTR CBonTuner::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	vector<SPACE_DATA>::iterator itr = find_if(m_chSet.spaceVec.begin(), m_chSet.spaceVec.end(), [dwSpace](const SPACE_DATA& s) {return(s.cSpace == dwSpace); });
	if (itr == m_chSet.spaceVec.end()) {
		return NULL;
	}
	else {
		return((itr->chVec.size() <= dwChannel) ? NULL : itr->chVec[dwChannel].tszName.c_str());
	}
}

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	vector<SPACE_DATA>::iterator itr = find_if(m_chSet.spaceVec.begin(), m_chSet.spaceVec.end(), [dwSpace](const SPACE_DATA& s) {return(s.cSpace == dwSpace); });
	if (itr == m_chSet.spaceVec.end()) {
		return FALSE;
	}

	if (itr->chVec.size() <= dwChannel) {
		return FALSE;
	}

	if (m_iID == -1) {
		return FALSE;
	}

	if ((SendSetCh(m_iID, itr->chVec[dwChannel].dwPT1Ch, itr->chVec[dwChannel].ulTSID)) != CMD_SUCCESS) {
		return FALSE;
	}

	if (m_dwSetChDelay) {
		Sleep(m_dwSetChDelay);
	}

	PurgeTsStream();

	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;
	return TRUE;
}

const DWORD CBonTuner::GetCurSpace(void)
{
	return m_dwCurSpace;
}

const DWORD CBonTuner::GetCurChannel(void)
{
	return m_dwCurChannel;
}

void CBonTuner::Release()
{
	delete this;
}

UINT WINAPI CBonTuner::RecvThread(LPVOID pParam)
{
	CBonTuner* pSys = (CBonTuner*)pParam;

	tstring strEvent;
	tstring strPipe;
	Format(strEvent, _T("%s%d"), CMD_PT1_DATA_EVENT_WAIT_CONNECT, pSys->m_iID);
	Format(strPipe, _T("%s%d"), CMD_PT1_DATA_PIPE, pSys->m_iID);

	while (1) {
		if (::WaitForSingleObject( pSys->m_hStopEvent, 0 ) != WAIT_TIMEOUT) {
			//中止
			break;
		}
		DWORD dwSize;
		BYTE *pbBuff = NULL;
		if ((SendSendData(pSys->m_iID, &pbBuff, &dwSize, strEvent, strPipe) == CMD_SUCCESS) && (dwSize != 0)) {
			TS_DATA *pData = new TS_DATA(pbBuff, dwSize);
			::EnterCriticalSection(&pSys->m_CriticalSection);
			while (pSys->m_TsBuff.size() > MAX_BUFF_COUNT) {
				TS_DATA *p = pSys->m_TsBuff.front();
				pSys->m_TsBuff.pop_front();
				delete p;
			}
			pSys->m_TsBuff.push_back(pData);
			::LeaveCriticalSection(&pSys->m_CriticalSection);
			::SetEvent(pSys->m_hOnStreamEvent);
		}else{
			::Sleep(5);
		}
	}

	return 0;
}
