#include "StdAfx.h"
#include "PT1Manager.h"

CPT1Manager::CPT1Manager(void)
{
	m_cLibrary = NULL;
	m_cBus = NULL;

	TCHAR strExePath[512] = _T("");
	GetModuleFileName(NULL, strExePath, 512);

	TCHAR szPath[_MAX_PATH];	// パス
	TCHAR szDrive[_MAX_DRIVE];
	TCHAR szDir[_MAX_DIR];
	TCHAR szFname[_MAX_FNAME];
	TCHAR szExt[_MAX_EXT];
	_tsplitpath_s( strExePath, szDrive, _MAX_DRIVE, szDir, _MAX_DIR, szFname, _MAX_FNAME, szExt, _MAX_EXT );
	_tmakepath_s(  szPath, _MAX_PATH, szDrive, szDir, NULL, NULL );

	TCHAR tchIni[_MAX_PATH];
	_tcscpy_s(tchIni, MAX_PATH, szPath);
	_tcsncat_s(tchIni, MAX_PATH - _tcslen(tchIni), _T("BonDriver_PT-ST.ini"), _MAX_FNAME);

	m_bUseLNB = GetPrivateProfileInt(_T("SET"), _T("UseLNB"), 0, tchIni);
	m_uiVirtualCount = GetPrivateProfileInt(_T("SET"), _T("DMABuff"), 8, tchIni);
	if( m_uiVirtualCount == 0 ){
		m_uiVirtualCount = 8;
	}
	m_SetChDelay = GetPrivateProfileInt(_T("SET"), _T("SetChDelay"), 0, tchIni);
}

CPT1Manager::~CPT1Manager(void)
{
	FreeSDK();
}

BOOL CPT1Manager::LoadSDK()
{
	if( m_cLibrary == NULL ){
		m_cLibrary = new OS::Library();

		PT::Bus::NewBusFunction function = m_cLibrary->Function();
		if (function == NULL) {
			SAFE_DELETE(m_cLibrary);
			return FALSE;
		}
		status enStatus = function(&m_cBus);
		if( enStatus != PT::STATUS_OK ){
			SAFE_DELETE(m_cLibrary);
			return FALSE;
		}

		//バージョンチェック
		uint version;
		m_cBus->GetVersion(&version);
		if ((version >> 8) != 2) {
			m_cBus->Delete();
			m_cBus = NULL;
			SAFE_DELETE(m_cLibrary);
			return FALSE;
		}
	}
	return TRUE;
}

void CPT1Manager::FreeSDK()
{
	for( int i=0; i<(int)m_EnumDev.size(); i++ ){
		if( m_EnumDev[i]->bOpen == TRUE ){
			m_EnumDev[i]->cDataIO.Stop();
			m_EnumDev[i]->pcDevice->Close();
			m_EnumDev[i]->pcDevice->Delete();
		}
		SAFE_DELETE(m_EnumDev[i]);
	}
	m_EnumDev.clear();

	if( m_cBus != NULL ){
		m_cBus->Delete();
		m_cBus = NULL;
	}
	if( m_cLibrary != NULL ){
		SAFE_DELETE(m_cLibrary);
	}
}

BOOL CPT1Manager::Init()
{
	if( m_cBus == NULL ){
		return FALSE;
	}
	m_EnumDev.clear();

	PT::Bus::DeviceInfo deviceInfo[9];
	uint deviceInfoCount = sizeof(deviceInfo)/sizeof(*deviceInfo);
	m_cBus->Scan(deviceInfo, &deviceInfoCount, 3);

	for (uint i=0; i<deviceInfoCount; i++) {
		if( deviceInfo[i].BadBitCount == 0 ){
			DEV_STATUS* pItem = new DEV_STATUS;
			pItem->stDevInfo = deviceInfo[i];
			m_EnumDev.push_back(pItem);
		}
	}

	return TRUE;
}

void CPT1Manager::UnInit()
{
	FreeSDK();
}

BOOL CPT1Manager::IsFindOpen()
{
	BOOL bFind = FALSE;
	for( int i=0; i<(int)m_EnumDev.size(); i++ ){
		if( m_EnumDev[i]->bOpen == TRUE ){
			bFind = TRUE;
		}
	}
	return bFind;
}

int CPT1Manager::OpenTuner(BOOL bSate)
{
	int iID = -1;
	int iDevID = -1;
	PT::Device::ISDB enISDB;
	uint iTuner = -1;
	//空きを探す
	if( bSate == FALSE ){
		//地デジ
		for( int i=0; i<(int)m_EnumDev.size(); i++ ){
			if( m_EnumDev[i]->bUseT0 == FALSE ){
				iID = (i<<16) | (PT::Device::ISDB_T<<8) | 0;
				iDevID = i;
				enISDB = PT::Device::ISDB_T;
				iTuner = 0;
				break;
			}else if( m_EnumDev[i]->bUseT1 == FALSE ){
				iID = (i<<16) | (PT::Device::ISDB_T<<8) | 1;
				iDevID = i;
				enISDB = PT::Device::ISDB_T;
				iTuner = 1;
				break;
			}
		}
	}else{
		//BS/CS
		for( int i=0; i<(int)m_EnumDev.size(); i++ ){
			if( m_EnumDev[i]->bUseS0 == FALSE ){
				iID = (i<<16) | (PT::Device::ISDB_S<<8) | 0;
				iDevID = i;
				enISDB = PT::Device::ISDB_S;
				iTuner = 0;
				break;
			}else if( m_EnumDev[i]->bUseS1 == FALSE ){
				iID = (i<<16) | (PT::Device::ISDB_S<<8) | 1;
				iDevID = i;
				enISDB = PT::Device::ISDB_S;
				iTuner = 1;
				break;
			}
		}
	}
	if( iID == -1 ){
		return -1;
	}
	status enStatus;
	if( m_EnumDev[iDevID]->bOpen == FALSE ){
		//デバイス初オープン
		enStatus = m_cBus->NewDevice(&m_EnumDev[iDevID]->stDevInfo, &m_EnumDev[iDevID]->pcDevice, NULL);
		if( enStatus != PT::STATUS_OK ){
			return -1;
		}
		for( int i = 0; i < 5; i++ ){
			enStatus = m_EnumDev[iDevID]->pcDevice->Open();
			if( enStatus == PT::STATUS_OK ){
				break;
			}
			m_EnumDev[iDevID]->pcDevice->Close();
			Sleep(10);
		}
		if (enStatus != PT::STATUS_OK){
			m_EnumDev[iDevID]->pcDevice->Delete();
			m_EnumDev[iDevID]->pcDevice = NULL;
			return -1;
		}

		enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerPowerReset(PT::Device::TUNER_POWER_ON_RESET_ENABLE);
		if( enStatus != PT::STATUS_OK ){
			m_EnumDev[iDevID]->pcDevice->Close();
			m_EnumDev[iDevID]->pcDevice->Delete();
			m_EnumDev[iDevID]->pcDevice = NULL;
			return -1;
		}
		Sleep(21);
		enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerPowerReset(PT::Device::TUNER_POWER_ON_RESET_DISABLE);
		if( enStatus != PT::STATUS_OK ){
			m_EnumDev[iDevID]->pcDevice->Close();
			m_EnumDev[iDevID]->pcDevice->Delete();
			m_EnumDev[iDevID]->pcDevice = NULL;
			return -1;
		}
		Sleep(2);
		for( uint i=0; i<2; i++ ){
			enStatus = m_EnumDev[iDevID]->pcDevice->InitTuner(i);
			if( enStatus != PT::STATUS_OK ){
				m_EnumDev[iDevID]->pcDevice->Close();
				m_EnumDev[iDevID]->pcDevice->Delete();
				m_EnumDev[iDevID]->pcDevice = NULL;
				return -1;
			}
		}
		for (uint i=0; i<2; i++) {
			for (uint j=0; j<2; j++) {
				enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerSleep(i, static_cast<PT::Device::ISDB>(j), true);
				if( enStatus != PT::STATUS_OK ){
					m_EnumDev[iDevID]->pcDevice->Close();
					m_EnumDev[iDevID]->pcDevice->Delete();
					m_EnumDev[iDevID]->pcDevice = NULL;
					return -1;
				}
			}
		}
		m_EnumDev[iDevID]->bOpen = TRUE;
		m_EnumDev[iDevID]->cDataIO.SetVirtualCount(m_uiVirtualCount);
		m_EnumDev[iDevID]->cDataIO.SetDevice(m_EnumDev[iDevID]->pcDevice);
		m_EnumDev[iDevID]->cDataIO.Run();
	}
	//スリープから復帰
	enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerSleep(iTuner, enISDB, false);
	if( enStatus != PT::STATUS_OK ){
		return -1;
	}

	if( m_bUseLNB == TRUE && enISDB == PT::Device::ISDB_S){
		m_EnumDev[iDevID]->pcDevice->SetLnbPower(PT::Device::LNB_POWER_15V);
	}

	enStatus = m_EnumDev[iDevID]->pcDevice->SetStreamEnable(iTuner, enISDB, true);
	if( enStatus != PT::STATUS_OK ){
		return -1;
	}

	if( enISDB == PT::Device::ISDB_T ){
		if( iTuner == 0 ){
			m_EnumDev[iDevID]->bUseT0 = TRUE;
		}else{
			m_EnumDev[iDevID]->bUseT1 = TRUE;
		}
		Sleep(10);	// PT1/2のT側はこの後の選局までが早すぎるとエラーになる場合があるので
	}else{
		if( iTuner == 0 ){
			m_EnumDev[iDevID]->bUseS0 = TRUE;
		}else{
			m_EnumDev[iDevID]->bUseS1 = TRUE;
		}
	}
	m_EnumDev[iDevID]->cDataIO.EnableTuner(iID, TRUE);

	return iID;
}

BOOL CPT1Manager::CloseTuner(int iID)
{
	int iDevID = iID>>16;
	PT::Device::ISDB enISDB = (PT::Device::ISDB)((iID&0x0000FF00)>>8);
	uint iTuner = iID&0x000000FF;

	if( (int)m_EnumDev.size() <= iDevID ){
		return FALSE;
	}

	status enStatus;
	enStatus = m_EnumDev[iDevID]->pcDevice->SetStreamEnable(iTuner, enISDB, false);
	if( enStatus != PT::STATUS_OK ){
		return FALSE;
	}

	enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerSleep(iTuner, enISDB, true);
	if( enStatus != PT::STATUS_OK ){
		return FALSE;
	}
	m_EnumDev[iDevID]->cDataIO.EnableTuner(iID, FALSE);

	if( enISDB == PT::Device::ISDB_T ){
		if( iTuner == 0 ){
			m_EnumDev[iDevID]->bUseT0 = FALSE;
		}else{
			m_EnumDev[iDevID]->bUseT1 = FALSE;
		}
	}else{
		if( iTuner == 0 ){
			m_EnumDev[iDevID]->bUseS0 = FALSE;
		}else{
			m_EnumDev[iDevID]->bUseS1 = FALSE;
		}
	}

	if( m_bUseLNB == TRUE && m_EnumDev[iDevID]->bUseS0 == FALSE && m_EnumDev[iDevID]->bUseS1 == FALSE ){
		m_EnumDev[iDevID]->pcDevice->SetLnbPower(PT::Device::LNB_POWER_OFF);
	}

	if( m_EnumDev[iDevID]->bUseT0 == FALSE && 
		m_EnumDev[iDevID]->bUseT1 == FALSE && 
		m_EnumDev[iDevID]->bUseS0 == FALSE && 
		m_EnumDev[iDevID]->bUseS1 == FALSE ){
			//全部使ってなければクローズ
			m_EnumDev[iDevID]->cDataIO.Stop();
			m_EnumDev[iDevID]->pcDevice->SetTransferEnable(false);
			m_EnumDev[iDevID]->pcDevice->SetBufferInfo(NULL);
			m_EnumDev[iDevID]->pcDevice->Close();
			m_EnumDev[iDevID]->pcDevice->Delete();
			m_EnumDev[iDevID]->pcDevice = NULL;
			m_EnumDev[iDevID]->bOpen = FALSE;
	}

	return TRUE;
}

BOOL CPT1Manager::SetCh(int iID, unsigned long ulCh, DWORD dwTSID)
{
	int iDevID = iID>>16;
	PT::Device::ISDB enISDB = (PT::Device::ISDB)((iID&0x0000FF00)>>8);
	uint iTuner = iID&0x000000FF;

	if( (int)m_EnumDev.size() <= iDevID ){
		return FALSE;
	}

	status enStatus;
	enStatus = m_EnumDev[iDevID]->pcDevice->SetFrequency(iTuner, enISDB, ulCh, 0);
	if( enStatus != PT::STATUS_OK ){
		return FALSE;
	}
	if( enISDB == PT::Device::ISDB_S ){
		enStatus = m_EnumDev[iDevID]->pcDevice->SetIdS(iTuner, dwTSID);
		if( enStatus != PT::STATUS_OK ){
			return FALSE;
		}
		uint uiGetID=0;
		DWORD dwCount = 0;
		while( dwTSID != uiGetID && dwCount<500){
			enStatus = m_EnumDev[iDevID]->pcDevice->GetIdS(iTuner, &uiGetID);
			if( enStatus != PT::STATUS_OK ){
				return FALSE;
			}
			Sleep(10);
			dwCount++;
		}
	}

	if (m_SetChDelay != 0)
		Sleep(m_SetChDelay);
	m_EnumDev[iDevID]->cDataIO.ClearBuff(iID);

	return TRUE;
}

DWORD CPT1Manager::GetSignal(int iID)
{
	int iDevID = iID>>16;
	PT::Device::ISDB enISDB = (PT::Device::ISDB)((iID&0x0000FF00)>>8);
	uint iTuner = iID&0x000000FF;

	if( (int)m_EnumDev.size() <= iDevID ){
		return FALSE;
	}

	uint cn100;
	uint currentAgc;
	uint maxAgc;

	status enStatus;
	enStatus = m_EnumDev[iDevID]->pcDevice->GetCnAgc(iTuner, enISDB, &cn100, &currentAgc, &maxAgc);
	if( enStatus != PT::STATUS_OK ){
		return 0;
	}

	return cn100;
}

BOOL CPT1Manager::CloseChk()
{
	BOOL bRet = FALSE;
	int iID;
	for(int i=0; i<(int)m_EnumDev.size(); i++ ){
		if( m_EnumDev[i]->bUseT0 == TRUE ){
			iID = (i<<16) | (PT::Device::ISDB_T<<8) | 0;
			if(m_EnumDev[i]->cDataIO.GetOverFlowCount(iID) > 100){
				OutputDebugString(L"T0 OverFlow Close");
				CloseTuner(iID);
			}
		}
		if( m_EnumDev[i]->bUseT1 == TRUE ){
			iID = (i<<16) | (PT::Device::ISDB_T<<8) | 1;
			if(m_EnumDev[i]->cDataIO.GetOverFlowCount(iID) > 100){
				OutputDebugString(L"T1 OverFlow Close");
				CloseTuner(iID);
			}
		}
		if( m_EnumDev[i]->bUseS0 == TRUE ){
			iID = (i<<16) | (PT::Device::ISDB_S<<8) | 0;
			if(m_EnumDev[i]->cDataIO.GetOverFlowCount(iID) > 100){
				OutputDebugString(L"S0 OverFlow Close");
				CloseTuner(iID);
			}
		}
		if( m_EnumDev[i]->bUseS1 == TRUE ){
			iID = (i<<16) | (PT::Device::ISDB_S<<8) | 1;
			if(m_EnumDev[i]->cDataIO.GetOverFlowCount(iID) > 100){
				OutputDebugString(L"S1 OverFlow Close");
				CloseTuner(iID);
			}
		}
	}
	for(int i=0; i<(int)m_EnumDev.size(); i++ ){
		if( m_EnumDev[i]->bOpen == TRUE ){
			bRet = TRUE;
		}
	}
	return bRet;
}

int CPT1Manager::OpenTuner2(BOOL bSate, int iTunerID)
{
	int iID = -1;
	int iDevID = -1;
	PT::Device::ISDB enISDB;
	uint iTuner = -1;
	char log[256];

	//指定チューナーが空いてるか確認
	if( (int)m_EnumDev.size() <= iTunerID/2 ){
		wsprintfA(log, "tuner not found: m_EnumDev.size()[%d] iTunerID[%d]\n", (int)m_EnumDev.size(), iTunerID);
		OutputDebugStringA(log);
		return -1;
	}
	iDevID = iTunerID/2;
	if( bSate == FALSE ){
		//地デジ
		if( iTunerID%2 == 0 ){
			if( m_EnumDev[iDevID]->bUseT0 == FALSE ){
				iID = (iDevID<<16) | (PT::Device::ISDB_T<<8) | 0;
				enISDB = PT::Device::ISDB_T;
				iTuner = 0;
			}
		}else{
			if( m_EnumDev[iDevID]->bUseT1 == FALSE ){
				iID = (iDevID<<16) | (PT::Device::ISDB_T<<8) | 1;
				enISDB = PT::Device::ISDB_T;
				iTuner = 1;
			}
		}
	}else{
		//BS/CS
		if( iTunerID%2 == 0 ){
			if( m_EnumDev[iDevID]->bUseS0 == FALSE ){
				iID = (iDevID<<16) | (PT::Device::ISDB_S<<8) | 0;
				enISDB = PT::Device::ISDB_S;
				iTuner = 0;
			}
		}else{
			if( m_EnumDev[iDevID]->bUseS1 == FALSE ){
				iID = (iDevID<<16) | (PT::Device::ISDB_S<<8) | 1;
				enISDB = PT::Device::ISDB_S;
				iTuner = 1;
			}
		}
	}
	_RPT3(_CRT_WARN, "*** CPT1Manager::OpenTuner2() : iDevID[%d] bSate[%d] iTunerID[%d] ***\n", iDevID, bSate, iTunerID);
	if( iTuner == -1 ){
		OutputDebugStringA("unused tuner not found\n");
		return -1;
	}
	status enStatus;
	if( m_EnumDev[iDevID]->bOpen == FALSE ){
		//デバイス初オープン
		enStatus = m_cBus->NewDevice(&m_EnumDev[iDevID]->stDevInfo, &m_EnumDev[iDevID]->pcDevice, NULL);
		if( enStatus != PT::STATUS_OK ){
			wsprintfA(log, "m_cBus->NewDevice() error : enStatus[0x%x]\n", enStatus);
			OutputDebugStringA(log);
			return -1;
		}
		for( int i = 0; i < 5; i++ ){
			enStatus = m_EnumDev[iDevID]->pcDevice->Open();
			if( enStatus == PT::STATUS_OK ){
				break;
			}
			wsprintfA(log, "%d: pcDevice->Open() error : enStatus[0x%x]\n", i, enStatus);
			OutputDebugStringA(log);
			// PT::STATUS_DEVICE_IS_ALREADY_OPEN_ERRORの場合は考慮しない
			m_EnumDev[iDevID]->pcDevice->Close();
			Sleep(10);	// 保険
		}
		if( enStatus != PT::STATUS_OK ){
			m_EnumDev[iDevID]->pcDevice->Delete();
			m_EnumDev[iDevID]->pcDevice = NULL;
			return -1;
		}

		enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerPowerReset(PT::Device::TUNER_POWER_ON_RESET_ENABLE);
		if( enStatus != PT::STATUS_OK ){
			m_EnumDev[iDevID]->pcDevice->Close();
			m_EnumDev[iDevID]->pcDevice->Delete();
			m_EnumDev[iDevID]->pcDevice = NULL;
			wsprintfA(log, "pcDevice->SetTunerPowerReset(ENABLE) error : enStatus[0x%x]\n", enStatus);
			OutputDebugStringA(log);
			return -1;
		}
		Sleep(20);
		enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerPowerReset(PT::Device::TUNER_POWER_ON_RESET_DISABLE);
		if( enStatus != PT::STATUS_OK ){
			m_EnumDev[iDevID]->pcDevice->Close();
			m_EnumDev[iDevID]->pcDevice->Delete();
			m_EnumDev[iDevID]->pcDevice = NULL;
			wsprintfA(log, "pcDevice->SetTunerPowerReset(DISABLE) error : enStatus[0x%x]\n", enStatus);
			OutputDebugStringA(log);
			return -1;
		}
		Sleep(1);
		for( uint i=0; i<2; i++ ){
			enStatus = m_EnumDev[iDevID]->pcDevice->InitTuner(i);
			if( enStatus != PT::STATUS_OK ){
				m_EnumDev[iDevID]->pcDevice->Close();
				m_EnumDev[iDevID]->pcDevice->Delete();
				m_EnumDev[iDevID]->pcDevice = NULL;
				wsprintfA(log, "pcDevice->InitTuner(%d) error : enStatus[0x%x]\n", i, enStatus);
				OutputDebugStringA(log);
				return -1;
			}
		}
		for (uint i=0; i<2; i++) {
			for (uint j=0; j<2; j++) {
				enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerSleep(i, static_cast<PT::Device::ISDB>(j), true);
				if( enStatus != PT::STATUS_OK ){
					m_EnumDev[iDevID]->pcDevice->Close();
					m_EnumDev[iDevID]->pcDevice->Delete();
					m_EnumDev[iDevID]->pcDevice = NULL;
					wsprintfA(log, "pcDevice->SetTunerSleep(%d, %d, true) error : enStatus[0x%x]\n", i, j, enStatus);
					OutputDebugStringA(log);
					return -1;
				}
			}
		}
		m_EnumDev[iDevID]->bOpen = TRUE;
		m_EnumDev[iDevID]->cDataIO.SetVirtualCount(m_uiVirtualCount);
		m_EnumDev[iDevID]->cDataIO.SetDevice(m_EnumDev[iDevID]->pcDevice);
		m_EnumDev[iDevID]->cDataIO.Run();
	}
	//スリープから復帰
	enStatus = m_EnumDev[iDevID]->pcDevice->SetTunerSleep(iTuner, enISDB, false);
	if( enStatus != PT::STATUS_OK ){
		wsprintfA(log, "pcDevice->SetTunerSleep(%d, %d, false) error : enStatus[0x%x]\n", iTuner, enISDB, enStatus);
		OutputDebugStringA(log);
		return -1;
	}

	if( m_bUseLNB == TRUE && enISDB == PT::Device::ISDB_S){
		m_EnumDev[iDevID]->pcDevice->SetLnbPower(PT::Device::LNB_POWER_15V);
	}

	enStatus = m_EnumDev[iDevID]->pcDevice->SetStreamEnable(iTuner, enISDB, true);
	if( enStatus != PT::STATUS_OK ){
		wsprintfA(log, "pcDevice->SetStreamEnable(%d, %d, true) error : enStatus[0x%x]\n", iTuner, enISDB, enStatus);
		OutputDebugStringA(log);
		return -1;
	}

	if( enISDB == PT::Device::ISDB_T ){
		if( iTuner == 0 ){
			m_EnumDev[iDevID]->bUseT0 = TRUE;
		}else{
			m_EnumDev[iDevID]->bUseT1 = TRUE;
		}
		Sleep(10);	// PT1/2のT側はこの後の選局までが早すぎるとエラーになる場合があるので
	}else{
		if( iTuner == 0 ){
			m_EnumDev[iDevID]->bUseS0 = TRUE;
		}else{
			m_EnumDev[iDevID]->bUseS1 = TRUE;
		}
	}
	m_EnumDev[iDevID]->cDataIO.EnableTuner(iID, TRUE);

	return iID;
}
