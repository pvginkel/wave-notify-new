/*
 * This file is part of Google Wave Notifier.
 *
 * Google Wave Notifier is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Google Wave Notifier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Google Wave Notifier.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "stdafx.h"
#include "include.h"

CNotifierApp::CNotifierApp(HINSTANCE hInstance, wstring szCmdLine)
	: CApp(hInstance, szCmdLine)
{
	m_hNotifyIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYICON));
	m_hNotifyIconGray = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYICON_GRAY));
	m_hNotifyIconGray1 = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYICON_GRAY_1));
	m_hNotifyIconGray2 = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYICON_GRAY_2));
	m_hNotifyIconGray3 = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYICON_GRAY_3));
	m_hNotifyIconUnread = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYICON_UNREAD));
	m_hMainIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAINICON));
	m_hMainIconSmall = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAINICON_SMALL));

	m_uWmTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");

	m_hCursorArrow = LoadCursor(NULL, IDC_ARROW);
	m_hCursorHand = LoadCursor(NULL, IDC_HAND);

	m_lpWindow = new CAppWindow();

	m_lpSession = NULL;

	m_lpCurlCache = new CCurlCache();

	SyncProxySettings();

	CSettings vSettings(FALSE);

	if (!vSettings.GetPlaySoundOnNewWave(m_fPlaySoundOnNewWave))
	{
		m_fPlaySoundOnNewWave = TRUE;
	}

	if (!vSettings.GetBrowser(m_szBrowser))
	{
		m_szBrowser = CBrowser::BrowserDefault;
	}
}

CNotifierApp::~CNotifierApp()
{
	DestroyIcon(m_hNotifyIcon);
	DestroyIcon(m_hNotifyIconGray);
	DestroyIcon(m_hNotifyIconGray1);
	DestroyIcon(m_hNotifyIconGray2);
	DestroyIcon(m_hNotifyIconGray3);
	DestroyIcon(m_hNotifyIconUnread);
	DestroyIcon(m_hMainIcon);
	DestroyIcon(m_hMainIconSmall);

	DestroyCursor(m_hCursorArrow);
	DestroyCursor(m_hCursorHand);

	if (m_lpSession != NULL)
	{
		delete m_lpSession;
	}

	if (CPopupWindow::Instance() != NULL)
	{
		CPopupWindow::Instance()->CancelAll();
	}

	CCurl::SetProxySettings(NULL);

	delete m_lpCurlCache;
}

BOOL CNotifierApp::Initialise()
{
	return TRUE;
}

INT CNotifierApp::Execute()
{
	m_lpWindow->Create();

	return CApp::Execute();
}

void CNotifierApp::Login()
{
	if (m_lpSession == NULL)
	{
		m_lpSession = CreateWaveSessionFromRegistry();
	}

	if (m_lpSession != NULL && !m_lpSession->IsLoggedIn())
	{
		m_lpSession->Login();
	}

	if (m_lpSession == NULL || !m_lpSession->IsLoggedIn())
	{
		if (m_lpSession != NULL)
		{
			delete m_lpSession;
			m_lpSession = NULL;
		}

		PromptForCredentials();
	}
	else
	{
		ProcessLoggedIn();
	}
}

CWaveSession * CNotifierApp::CreateWaveSessionFromRegistry()
{
	CSettings settings(FALSE);

	CWaveSession * lpSession = NULL;

	wstring szUsername;
	wstring szPassword;

	if (
		settings.GetGoogleUsername(szUsername) &&
		settings.GetGooglePassword(szPassword)
	) {
		if (!szUsername.empty() && !szPassword.empty())
		{
			lpSession = new CWaveSession(szUsername, szPassword);
		}
	}

	return lpSession;
}

void CNotifierApp::PromptForCredentials()
{
	(new CLoginDialog())->Create(DT_LOGIN, CNotifierApp::Instance()->GetAppWindow());
}

void CNotifierApp::SignOut()
{
	if (m_lpSession != NULL && m_lpSession->IsLoggedIn())
	{
		m_lpSession->StopListener();
		m_lpSession->SignOut();

		SetWaveSession(NULL);
	}
}

void CNotifierApp::ProcessUnreadWavesNotifyIcon(INT nUnreadWaves)
{
	m_lpWindow->GetNotifyIcon()->SetIcon(nUnreadWaves > 0 ? m_hNotifyIconUnread : m_hNotifyIcon);
}

void CNotifierApp::ProcessLoggedIn()
{
	BOOL fIsLoggedIn = IsLoggedIn();

	m_lpWindow->GetNotifyIcon()->SetIcon(fIsLoggedIn ? m_hNotifyIcon : m_hNotifyIconGray);

	if (fIsLoggedIn)
	{
		m_lpSession->StartListener();
	}
}

void CNotifierApp::SetStartWithWindows(BOOL fValue)
{
	if (fValue)
	{
		CreateShortcut();
	}
	else
	{
		if (!m_szShortcutTargetPath.empty())
		{
			DeleteFile(m_szShortcutTargetPath.c_str());
		}
	}
}

void CNotifierApp::SyncProxySettings()
{
	CSettings settings(FALSE);
	CCurlProxySettings * lpProxySettings = NULL;

	BOOL fHaveSettings;
	wstring szHost;
	DWORD dwPort;
	BOOL fAuthenticated;
	wstring szUsername;
	wstring szPassword;

	BOOL fSuccess =
		settings.GetProxyHaveSettings(fHaveSettings) &&
		settings.GetProxyHost(szHost) &&
		settings.GetProxyPort(dwPort) &&
		settings.GetProxyAuthenticated(fAuthenticated) &&
		settings.GetProxyUsername(szUsername) &&
		settings.GetProxyPassword(szPassword);

	if (fSuccess && fHaveSettings)
	{
		if (fAuthenticated)
		{
			lpProxySettings = new CCurlProxySettings(szHost, dwPort, szUsername, szPassword);
		}
		else
		{
			lpProxySettings = new CCurlProxySettings(szHost, dwPort);
		}
	}

	CCurl::SetProxySettings(lpProxySettings);
}

void CNotifierApp::Restart()
{
	wstring szPath(GetModuleFileNameEx());

	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	memset(&si, 0, sizeof(STARTUPINFO));
	memset(&pi, 0, sizeof(PROCESS_INFORMATION));

	si.cb = sizeof(PROCESS_INFORMATION);

	LPWSTR szMutableCommandLine = (LPWSTR)malloc(sizeof(WCHAR) * (szPath.size() + 1));

	wcscpy(szMutableCommandLine, szPath.c_str());

	BOOL fSuccess = CreateProcess(
		szPath.c_str(),
		szMutableCommandLine,
		NULL,
		NULL,
		FALSE,
		CREATE_DEFAULT_ERROR_MODE,
		NULL,
		NULL,
		&si,
		&pi);

	free(szMutableCommandLine);
}

void CNotifierApp::DetectStartWithWindowsSetting()
{
	m_szShortcutTargetPath = L"";

	WCHAR szPath[MAX_PATH];
	wstring szModulePath(GetLongPathName(GetModuleFileNameEx()));

	if (!SHGetSpecialFolderPath(NULL, szPath, CSIDL_STARTUP , FALSE))
	{
		return;
	}

	wstring szFindPath(szPath);

	szFindPath += L"\\*.lnk";

	WIN32_FIND_DATA wfd;

	HANDLE hFind = FindFirstFile(szFindPath.c_str(), &wfd);

	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				continue;
			}

			wstring szFilename(szPath);

			szFilename += L"\\";
			szFilename += wfd.cFileName;

			if (DetectShortcut(szModulePath, szFilename))
			{
				m_szShortcutTargetPath = szFilename;
				break;
			}
		}
		while (FindNextFile(hFind, &wfd) != 0);
	}

	FindClose(hFind);

	CSettings(TRUE).SetStartWithWindows(!m_szShortcutTargetPath.empty());
}

BOOL CNotifierApp::DetectShortcut(const wstring & szModulePath, const wstring & szFilename)
{
	HRESULT hr;
	IShellLink * lpShellLink = NULL;
	IPersistFile * lpPersistFile = NULL;
	BOOL fSuccess = FALSE;
	wstring szLongTargetPath;

	hr = CoCreateInstance(
		CLSID_ShellLink,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IShellLink,
		reinterpret_cast<void**>(&lpShellLink)
	);

	if (!SUCCEEDED(hr))
	{
		goto __end;
	}

	hr = lpShellLink->QueryInterface(
		IID_IPersistFile,
		reinterpret_cast<void**>(&lpPersistFile)
	);

	if (!SUCCEEDED(hr))
	{
		goto __end;
	}

	lpPersistFile->Load(szFilename.c_str(), STGM_READ | STGM_SHARE_DENY_NONE);

	WCHAR szTargetPath[MAX_PATH];
	WIN32_FIND_DATA wfd;

	lpShellLink->GetPath(szTargetPath, MAX_PATH, &wfd, 0);

	szLongTargetPath = ExpandEnvironmentStrings(GetLongPathName(szTargetPath));

	fSuccess = _wcsicmp(szModulePath.c_str(), szLongTargetPath.c_str()) == 0;

__end:
	if (lpPersistFile != NULL)
	{
		lpPersistFile->Release();
	}

	if (lpShellLink != NULL)
	{
		lpShellLink->Release();
	}

	return fSuccess;
}

void CNotifierApp::CreateShortcut()
{
	HRESULT hr;
	IShellLink * lpShellLink = NULL;
	IPersistFile * lpPersistFile = NULL;
	WCHAR szStartupPath[MAX_PATH];
	wstring szModuleFilename(GetModuleFileNameEx());
	wstring szModulePath(GetDirname(szModuleFilename));

	if (!SHGetSpecialFolderPath(NULL, szStartupPath, CSIDL_STARTUP , FALSE))
	{
		return;
	}

	wstring szTargetPath(szStartupPath);

	szTargetPath += L"\\Google Wave Notifier.lnk";

	hr = CoCreateInstance(
		CLSID_ShellLink,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IShellLink,
		reinterpret_cast<void**>(&lpShellLink)
	);

	if (!SUCCEEDED(hr))
	{
		goto __end;
	}

	lpShellLink->SetIconLocation(szModuleFilename.c_str(), 0);
	lpShellLink->SetPath(szModuleFilename.c_str());
	lpShellLink->SetWorkingDirectory(szModulePath.c_str());

	hr = lpShellLink->QueryInterface(
		IID_IPersistFile,
		reinterpret_cast<void**>(&lpPersistFile)
	);

	if (!SUCCEEDED(hr))
	{
		goto __end;
	}

	lpPersistFile->Save(szTargetPath.c_str(), FALSE);

__end:
	if (lpPersistFile != NULL)
	{
		lpPersistFile->Release();
	}

	if (lpShellLink != NULL)
	{
		lpShellLink->Release();
	}
}

void CNotifierApp::OpenUrl(wstring szUrl)
{
	CBrowser::OpenUrl(m_szBrowser, szUrl);
}
