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
#include "wave.h"
#include "notifierapp.h"

CWaveSession::CWaveSession(CWindowHandle * lpTargetWindow)
{
	ASSERT(lpTargetWindow != NULL);

	m_lpTargetWindow = lpTargetWindow;
	m_szUsername = L"";
	m_szPassword = L"";
	m_lpCookies = NULL;
	m_szAuthKey = L"";
	m_szEmailAddress = L"";
	m_nLoginError = WLE_SUCCESS;
	m_lpRequest = NULL;
	m_lpChannelRequest = NULL;
	m_lpPostRequest = NULL;
	m_nRequesting = WSR_NONE;
	m_nState = WSS_OFFLINE;
	m_nFlushSuspended = 0;

	m_lpReconnectTimer = new CTimer();

	m_lpReconnectTimer->Tick += AddressOf<CWaveSession>(this, &CWaveSession::ReconnectTimer);

	ResetChannelParameters();
}

CWaveSession::~CWaveSession()
{
	if (m_lpCookies != NULL)
	{
		delete m_lpCookies;
	}

	for (TWaveListenerMapIter iter = m_vListeners.begin(); iter != m_vListeners.end(); iter++)
	{
		delete iter->second;
	}

	delete m_lpReconnectTimer;
}

BOOL CWaveSession::Login(wstring szUsername, wstring szPassword)
{
	ASSERT(!szUsername.empty() && !szPassword.empty());

	if (m_nState != WSS_OFFLINE)
	{
		LOG("Not offline");
		return FALSE;
	}

	if (m_lpRequest != NULL)
	{
		LOG("Requesting login while a request is running");
		return FALSE;
	}

	m_nState = WSS_CONNECTING;

	SignalProgress(WCS_BEGIN_LOGON);

	m_szUsername = szUsername;
	m_szPassword = szPassword;

	return Reconnect();
}

BOOL CWaveSession::Reconnect()
{
	if (m_szUsername.empty() || m_szPassword.empty())
	{
		return FALSE;
	}

	if (m_lpRequest != NULL)
	{
		m_vOwnedRequests.push_back(m_lpRequest);
	}

	m_lpRequest = new CCurl(WAVE_URL_CLIENTLOGIN, m_lpTargetWindow);

	m_lpRequest->SetUserAgent(USERAGENT);
	m_lpRequest->SetTimeout(WEB_TIMEOUT_SHORT);
	m_lpRequest->SetIgnoreSSLErrors(TRUE);
	m_lpRequest->SetReader(new CCurlUTF8StringReader());

	wstringstream szPostData;

	szPostData
		<< L"accountType=GOOGLE&Email="
		<< UrlEncode(m_szUsername)
		<< L"&Passwd="
		<< UrlEncode(m_szPassword)
		<< L"&service=wave&source=net.sf.wave-notify";

	m_lpRequest->SetUrlEncodedPostData(szPostData.str());

	m_nRequesting = WSR_AUTH_KEY;

	CNotifierApp::Instance()->QueueRequest(m_lpRequest);

	return TRUE;
}

wstring CWaveSession::GetAuthKeyFromRequest(wstring & szResponse) const
{
	TStringStringMap vMap;

	if (!ParseStringMap(szResponse, vMap))
	{
		return L"";
	}

	TStringStringMapIter pos = vMap.find(L"Auth");

	if (pos == vMap.end())
	{
		return L"";
	}
	else
	{
		return pos->second;
	}
}

void CWaveSession::SignOut()
{
	if (m_nState == WSS_RECONNECTING)
	{
		StopReconnecting();
	}
	else if (m_nState == WSS_ONLINE)
	{
		m_nState = WSS_DISCONNECTING;

		SignalProgress(WCS_BEGIN_SIGNOUT);

		if (m_lpPostRequest != NULL)
		{
			CNotifierApp::Instance()->CancelRequest(m_lpPostRequest);

			// We do not need to flush the request queue anymore.

			m_vOwnedRequests.push_back(m_lpPostRequest);

			m_lpPostRequest = NULL;
		}

		if (m_lpChannelRequest != NULL)
		{
			CNotifierApp::Instance()->CancelRequest(m_lpChannelRequest);

			// We do not need to process the channel response anymore.

			m_vOwnedRequests.push_back(m_lpChannelRequest);

			m_lpChannelRequest = NULL;
		}

		PostSignOutRequest();
	}
}

void CWaveSession::PostSignOutRequest()
{
	if (m_lpRequest != NULL)
	{
		m_vOwnedRequests.push_back(m_lpRequest);
	}

	m_lpRequest = new CCurl(WAVE_URL_LOGOUT, m_lpTargetWindow);

	m_lpRequest->SetUserAgent(USERAGENT);
	m_lpRequest->SetTimeout(WEB_TIMEOUT_SHORT);
	m_lpRequest->SetIgnoreSSLErrors(TRUE);
	m_lpRequest->SetCookies(m_lpCookies);
	m_lpRequest->SetAutoRedirect(TRUE);

	m_nRequesting = WSR_SIGN_OUT;

	CNotifierApp::Instance()->QueueRequest(m_lpRequest);

	if (m_lpCookies != NULL)
	{
		delete m_lpCookies;

		m_lpCookies = NULL;
	}
}

wstring CWaveSession::GetSessionData(wstring & szResponse) const
{
	INT nBegin = 0;

	// Walk all script blocks

	for (;;)
	{
		// Find the start of the script block

		nBegin = szResponse.find(L"<script", nBegin);

		if (nBegin != wstring::npos)
		{
			nBegin = szResponse.find(L'>', nBegin);
		}

		if (nBegin == wstring::npos)
		{
			break;
		}

		nBegin++;

		// Find the end of the script block

		INT nEnd = szResponse.find(L"</script", nBegin);

		if (nEnd == wstring::npos)
		{
			break;
		}

		// Does the current script block contain the __session token?

		INT nSessionBegin = szResponse.find(L"__session", nBegin);

		if (nSessionBegin != wstring::npos && nSessionBegin < nEnd)
		{
			// The session block is always one single line

			INT nSessionEnd = szResponse.find(L'\n', nSessionBegin);

			ASSERT(nSessionEnd != wstring::npos);

			// Return the session block

			return szResponse.substr(nSessionBegin, nSessionEnd - nSessionBegin);
		}

		// Skip over the end of the script block

		nBegin = nEnd;
	}

	return L"";
}

wstring CWaveSession::GetInboxUrl() const
{
	return Format(WAVE_URL_INBOX, UrlEncode(m_szAuthKey).c_str());
}

wstring CWaveSession::GetWaveUrl(wstring szWaveId) const
{
	ASSERT(!szWaveId.empty());

	return Format(WAVE_URL_WAVE, UrlEncode(m_szAuthKey).c_str(), UrlEncode(UrlEncode(szWaveId)).c_str());
}

wstring CWaveSession::GetKeyFromSessionResponse(wstring szKey, wstring & szResponse) const
{
	// TODO: Extend the Json library to include this functionality.

	ASSERT(!szKey.empty());

	INT nOffset = 0;

	while (nOffset != string::npos)
	{
		nOffset = szResponse.find(szKey + L":'", nOffset);

		if (nOffset != string::npos)
		{
			CHAR cBefore = nOffset == 0 ? '\0' : szResponse[nOffset - 1];

			nOffset += szKey.length() + 1;

			if (!iswalnum(cBefore) && cBefore != '_')
			{
				break;
			}
		}
	}

	if (nOffset != string::npos)
	{
		nOffset = szResponse.find(L'\'', nOffset) + 1;
	}

	if (nOffset != string::npos)
	{
		INT nEnd = szResponse.find(L'\'', nOffset);

		if (nEnd != string::npos)
		{
			return szResponse.substr(nOffset, nEnd - nOffset);
		}
	}

	return L"";
}

void CWaveSession::SetCookies(CCurlCookies * lpCookies)
{
	CHECK_PTR(lpCookies);

	if (m_lpCookies != NULL)
	{
		delete m_lpCookies;
	}

	m_lpCookies = lpCookies;
}

void CWaveSession::AddProgressTarget(CWindowHandle * lpSignalWindow)
{
	ASSERT(lpSignalWindow != NULL);

	m_vSignalWindows.push_back(lpSignalWindow);
}

void CWaveSession::RemoveProgressTarget(CWindowHandle * lpSignalWindow)
{
	ASSERT(lpSignalWindow != NULL);

	TWindowHandleVectorIter pos = find(m_vSignalWindows.begin(), m_vSignalWindows.end(), lpSignalWindow);

	if (pos == m_vSignalWindows.end())
	{
		LOG("Could not remove target window; not registered");
	}
	else
	{
		m_vSignalWindows.erase(pos);
	}
}

BOOL CWaveSession::ProcessCurlResponse(CCurl * lpCurl)
{
	// Do we understand the response?

	if (lpCurl == NULL)
	{
		return FALSE;
	}

	// Is this the response from the post request?

	if (lpCurl == m_lpPostRequest)
	{
		ProcessPostResponse();

		return TRUE;
	}

	// Is this response from the channel listener?

	if (lpCurl == m_lpChannelRequest)
	{
		ProcessChannelResponse();

		return TRUE;
	}

	// Is this a login request?

	if (lpCurl == m_lpRequest)
	{
		switch (m_nRequesting)
		{
		case WSR_AUTH_KEY:
			ProcessAuthKeyResponse();
			break;

		case WSR_COOKIE:
			ProcessCookieResponse();
			break;

		case WSR_SESSION_DETAILS:
			ProcessSessionDetailsResponse();
			break;

		case WSR_SID:
			ProcessSIDResponse();
			break;

		case WSR_SIGN_OUT:
			ProcessSignOutResponse();
			break;

		default:
			LOG1("Illegal requesting state %d", m_nRequesting);

			CCurl::Destroy(m_lpRequest);

			m_lpRequest = NULL;
			m_nRequesting = WSR_NONE;

			break;
		}

		return TRUE;
	}

	// Is this an owned request?

	TCurlVectorIter pos = find(m_vOwnedRequests.begin(), m_vOwnedRequests.end(), lpCurl);

	if (pos != m_vOwnedRequests.end())
	{
		CCurl::Destroy(lpCurl);

		m_vOwnedRequests.erase(pos);

		return TRUE;
	}

	// This is not one of our request.

	return FALSE;
}

void CWaveSession::ProcessAuthKeyResponse()
{
	ASSERT(m_lpRequest != NULL);

	CCurlUTF8StringReader * lpReader = (CCurlUTF8StringReader *)m_lpRequest->GetReader();

	ASSERT(lpReader != NULL);

	if (m_lpRequest->GetResult() != CURLE_OK)
	{
		m_nLoginError = WLE_NETWORK;
	}
	else
	{
		m_szAuthKey = GetAuthKeyFromRequest(lpReader->GetString());

		m_nLoginError = m_szAuthKey.empty() ? WLE_AUTHENTICATE : WLE_SUCCESS;
	}
	
	delete lpReader;
	delete m_lpRequest;

	m_lpRequest = NULL;
	m_nRequesting = WSR_NONE;

	if (m_nLoginError == WLE_SUCCESS)
	{
		SignalProgress(WCS_GOT_KEY);

		PostAuthCookieRequest();
	}
	else if (m_nState == WSS_RECONNECTING)
	{
		NextReconnect();
	}
	else
	{
		m_nState = WSS_OFFLINE;

		SignalProgress(WCS_FAILED);
	}
}

void CWaveSession::ProcessCookieResponse()
{
	ASSERT(m_lpRequest != NULL);

	if (m_lpRequest->GetResult() != CURLE_OK)
	{
		m_nLoginError = WLE_NETWORK;
	}
	else
	{
		SetCookies(m_lpRequest->GetCookies());

		m_nLoginError = m_lpCookies == NULL ? WLE_AUTHENTICATE : WLE_SUCCESS;
	}
	
	delete m_lpRequest;

	m_lpRequest = NULL;
	m_nRequesting = WSR_NONE;

	if (m_nLoginError == WLE_SUCCESS)
	{
		SignalProgress(WCS_GOT_COOKIE);

		PostSessionDetailsRequest();
	}
	else if (m_nState == WSS_RECONNECTING)
	{
		NextReconnect();
	}
	else
	{
		m_nState = WSS_OFFLINE;

		SignalProgress(WCS_FAILED);
	}
}

void CWaveSession::ProcessSessionDetailsResponse()
{
	ASSERT(m_lpRequest != NULL);

	CCurlUTF8StringReader * lpReader = (CCurlUTF8StringReader *)m_lpRequest->GetReader();

	ASSERT(lpReader != NULL);

	if (m_lpRequest->GetResult() != CURLE_OK)
	{
		m_nLoginError = WLE_NETWORK;
	}
	else
	{
		wstring szResponse(lpReader->GetString());
		wstring szSessionData(GetSessionData(szResponse));

		if (szSessionData.empty())
		{
			m_nLoginError = WLE_AUTHENTICATE;
		}
		else
		{
			m_szEmailAddress = GetKeyFromSessionResponse(L"username", szSessionData);
			m_szSessionID = GetKeyFromSessionResponse(L"sessionid", szSessionData);
			m_szProfileID = GetKeyFromSessionResponse(L"id", szSessionData);

			if (
				!m_szEmailAddress.empty() &&
				!m_szSessionID.empty() &&
				!m_szProfileID.empty()
			) {
				m_nLoginError = WLE_SUCCESS;
			}
			else
			{
				m_nLoginError = WLE_AUTHENTICATE;
			}
		}
	}
	
	delete lpReader;
	delete m_lpRequest;

	m_lpRequest = NULL;
	m_nRequesting = WSR_NONE;

	if (m_nLoginError == WLE_SUCCESS)
	{
		m_nState = WSS_ONLINE;

		SignalProgress(WCS_LOGGED_ON);

		ResetChannelParameters();

		PostSIDRequest();
	}
	else if (m_nState == WSS_RECONNECTING)
	{
		NextReconnect();
	}
	else
	{
		m_nState = WSS_OFFLINE;

		SignalProgress(WCS_FAILED);
	}
}

void CWaveSession::ProcessSIDResponse()
{
	ASSERT(m_lpRequest != NULL);

	CCurlUTF8StringReader * lpReader = (CCurlUTF8StringReader *)m_lpRequest->GetReader();

	ASSERT(lpReader != NULL);
	
	BOOL fSuccess = FALSE;

	if (m_lpRequest->GetResult() == CURLE_OK)
	{
		SetCookies(m_lpRequest->GetCookies());

		wstring szChannelResponse(ExtractChannelResponse(lpReader->GetString()));

		if (!szChannelResponse.empty())
		{
			fSuccess = ParseChannelResponse(szChannelResponse);
		}
	}
	
	delete lpReader;
	delete m_lpRequest;

	m_lpRequest = NULL;
	m_nRequesting = WSR_NONE;

	if (fSuccess)
	{
		SignalProgress(WCS_CONNECTED);

		PostChannelRequest();

		FlushRequestQueue();
	}
	else
	{
		InitiateReconnect();
	}
}

wstring CWaveSession::ExtractChannelResponse(wstring szResponse)
{
	if (!szResponse.empty())
	{
		DWORD dwPos = szResponse.find(L'\n');

		if (dwPos != wstring::npos && dwPos > 0)
		{
			DWORD dwLength = _wtol(szResponse.substr(0, dwPos).c_str());

			if (szResponse.length() >= dwPos + 1 + dwLength)
			{
				return szResponse.substr(dwPos + 1, dwLength);
			}
		}
	}

	return L"";
}

void CWaveSession::ProcessSignOutResponse()
{
	m_nState = WSS_OFFLINE;

	SignalProgress(WCS_SIGNED_OUT);

	if (m_lpCookies != NULL)
	{
		delete m_lpCookies;
	}

	m_lpCookies = NULL;

	ASSERT(m_lpRequest != NULL);

	delete m_lpRequest;

	m_lpRequest = NULL;
	m_nRequesting = WSR_NONE;
}

void CWaveSession::ProcessPostResponse()
{
	ASSERT(m_lpPostRequest != NULL);

	delete m_lpPostRequest;
	
	m_lpPostRequest = NULL;

	FlushRequestQueue();
}

void CWaveSession::ProcessChannelResponse()
{
	ASSERT(m_lpChannelRequest != NULL);

	CWaveReader * lpReader = (CWaveReader *)m_lpChannelRequest->GetReader();

	ASSERT(lpReader != NULL);
	
	BOOL fSuccess = FALSE;

	fSuccess =
		m_nState == WSS_ONLINE &&
		(
			m_lpChannelRequest->GetResult() == CURLE_OK ||
			m_lpChannelRequest->GetResult() == CURLE_OPERATION_TIMEOUTED
		);

	if (fSuccess)
	{
		SetCookies(m_lpChannelRequest->GetCookies());
	}
	
	delete lpReader;
	delete m_lpChannelRequest;

	m_lpChannelRequest = NULL;
	m_nRequesting = WSR_NONE;

	if (fSuccess)
	{
		// Continue the channel by posting the next request.

		PostChannelRequest();
	}
	else if (m_nState == WSS_ONLINE)
	{
		// Only automatically reconnect when we're online.

		InitiateReconnect();
	}
}

void CWaveSession::SignalProgress(WAVE_CONNECTION_STATE nStatus)
{
	CHECK_ENUM(nStatus, WCS_MAX);

	for (TWindowHandleVectorIter iter = m_vSignalWindows.begin(); iter != m_vSignalWindows.end(); iter++)
	{
		ASSERT(*iter != NULL);

		(*iter)->PostMessage(WM_WAVE_CONNECTION_STATE, nStatus, m_nLoginError);
	}
}

void CWaveSession::PostAuthCookieRequest()
{
	if (m_lpRequest != NULL)
	{
		m_vOwnedRequests.push_back(m_lpRequest);
	}

	m_lpRequest = new CCurl(
		Format(WAVE_URL_AUTH, UrlEncode(m_szAuthKey).c_str()),
		m_lpTargetWindow
	);

	m_lpRequest->SetUserAgent(USERAGENT);
	m_lpRequest->SetTimeout(WEB_TIMEOUT_SHORT);
	m_lpRequest->SetIgnoreSSLErrors(TRUE);

	m_nRequesting = WSR_COOKIE;

	CNotifierApp::Instance()->QueueRequest(m_lpRequest);
}

void CWaveSession::PostSessionDetailsRequest()
{
	if (m_lpRequest != NULL)
	{
		m_vOwnedRequests.push_back(m_lpRequest);
	}

	m_lpRequest = new CCurl(WAVE_URL_WAVES, m_lpTargetWindow);

	m_lpRequest->SetUserAgent(USERAGENT);
	m_lpRequest->SetTimeout(WEB_TIMEOUT_SHORT);
	m_lpRequest->SetIgnoreSSLErrors(TRUE);
	m_lpRequest->SetCookies(m_lpCookies);
	m_lpRequest->SetReader(new CCurlUTF8StringReader());

	m_nRequesting = WSR_SESSION_DETAILS;

	CNotifierApp::Instance()->QueueRequest(m_lpRequest);
}

void CWaveSession::PostSIDRequest()
{
	if (m_lpRequest != NULL)
	{
		m_vOwnedRequests.push_back(m_lpRequest);
	}

	m_lpRequest = new CCurl(
		Format(WAVE_URL_SESSIONID, m_nRID++, BuildHash().c_str()),
		m_lpTargetWindow
	);

	m_lpRequest->SetUserAgent(USERAGENT);
	m_lpRequest->SetTimeout(WEB_TIMEOUT_SHORT);
	m_lpRequest->SetIgnoreSSLErrors(TRUE);
	m_lpRequest->SetCookies(GetCookies());
	m_lpRequest->SetReader(new CCurlUTF8StringReader());

	m_lpRequest->SetUrlEncodedPostData(L"count=0");

	m_nRequesting = WSR_SID;

	CNotifierApp::Instance()->QueueRequest(m_lpRequest);
}

void CWaveSession::PostChannelRequest()
{
	if (m_lpRequest != NULL)
	{
		m_vOwnedRequests.push_back(m_lpChannelRequest);
	}

	m_lpChannelRequest = new CCurl(
		Format(WAVE_URL_CHANNEL, m_szSID.c_str(), m_nAID, BuildHash().c_str()),
		m_lpTargetWindow
	);

	m_lpChannelRequest->SetUserAgent(USERAGENT);
	m_lpChannelRequest->SetTimeout(WEB_TIMEOUT_CHANNEL);
	m_lpChannelRequest->SetIgnoreSSLErrors(TRUE);
	m_lpChannelRequest->SetCookies(GetCookies());
	m_lpChannelRequest->SetReader(new CWaveReader(this));

	CNotifierApp::Instance()->QueueRequest(m_lpChannelRequest);
}

void CWaveSession::ResetChannelParameters()
{
	m_szSID = L"";
	m_nAID = 0;
	m_nRID = Rand(10000, 90000);
	m_nNextRequestID = 0;
	m_nNextListenerID = 0;
}

BOOL CWaveSession::ParseChannelResponse(wstring szResponse)
{
	Json::Reader vReader;
	Json::Value vRoot;

	if (
		!vReader.parse(szResponse, vRoot) &&
		!vRoot.isArray()
	) {
		LOG("Could not parse json");

		return FALSE;
	}

	BOOL fSuccess = TRUE;

	for (DWORD i = 0; i < vRoot.size(); i++)
	{
		fSuccess = FALSE;

		Json::Value & vItem = vRoot[i];

		if (!vItem.isArray())
		{
			LOG("Could not parse json");

			break;
		}

		Json::Value & vAID = vItem[0u];
		Json::Value & vContent = vItem[1];

		if (
			!vAID.isIntegral() ||
			!vContent.isArray()
		) {
			LOG("Could not parse json");

			break;
		}

		Json::Value & vType = vContent[0u];

		if (!vType.isString())
		{
			LOG("Could not parse json");

			break;
		}

		INT nAID = vAID.asInt();
		wstring szType = vType.asString();

		if (szType == L"c")
		{
			if (vContent.size() > 1)
			{
				Json::Value & vSID = vContent[1];

				if (vSID.isString())
				{
					m_szSID = vSID.asString();

					fSuccess = TRUE;
				}
				else
				{
					LOG("Could not parse json");
				}
			}
		}
		else if (szType == L"wfe")
		{
			if (vContent.size() > 1)
			{
				Json::Value & vPayload = vContent[1];

				if (vPayload.isString())
				{
					CWaveResponse * lpResponse =
						ParseWfeResponse(vPayload.asString(), fSuccess);

					if (fSuccess && lpResponse != NULL)
					{
						ReportReceived(lpResponse);
					}
				}
				else
				{
					LOG("Could not parse json");
				}
			}
		}
		else
		{
			// There are noop's being sent, but everything else is also
			// ignored.

			fSuccess = TRUE;
		}

		if (fSuccess)
		{
			m_nAID = nAID;
		}
		else
		{
			break;
		}
	}

	return fSuccess;
}

void CWaveSession::InitiateReconnect()
{
	m_lpReconnectTimer->SetInterval(TIMER_RECONNECT_INTERVAL_INITIAL);
	m_lpReconnectTimer->SetRunning(TRUE);

	m_nState = WSS_RECONNECTING;

	SignalProgress(WCS_RECONNECTING);
}

wstring CWaveSession::BuildHash()
{
	wstring szResult;

	szResult.reserve(12);

	for (INT i = 0; i < 12; i++)
	{
		szResult += *(WAVE_HASH_POOL + Rand(0, _ARRAYSIZE(WAVE_HASH_POOL) - 1));
	}

	return szResult;
}

CWaveResponse * CWaveSession::ParseWfeResponse(wstring szResponse, BOOL & fSuccess)
{
	Json::Reader vReader;
	Json::Value vRoot;

	if (!vReader.parse(szResponse, vRoot))
	{
		fSuccess = FALSE;
		return NULL;
	}

	fSuccess = TRUE;

	return CWaveResponse::Parse(vRoot);
}

void CWaveSession::PostRequests()
{
	// This method is responsible for deleting the requests because requests
	// may later be stored. Responses can be linked to a request and when this
	// becomes necessary, this function will take the requests for later retrieval.
	// Now, we just delete them.

	wstringstream szPostData;

	ASSERT(m_vRequestQueue.size() > 0);

	szPostData << L"count=" << m_vRequestQueue.size();

	INT nOffset = 0;

	for (TWaveRequestVectorConstIter iter = m_vRequestQueue.begin(); iter != m_vRequestQueue.end(); iter++)
	{
		szPostData << L"&req" << nOffset << L"_key=" << UrlEncode(SerializeRequest(*iter));

		nOffset++;
	}

	// Post the JSON to the channel.

	wstring szUrl = Format(WAVE_URL_CHANNEL_POST, m_szSID.c_str(), m_nRID++, BuildHash().c_str());

	if (m_lpPostRequest != NULL)
	{
		m_vOwnedRequests.push_back(m_lpPostRequest);
	}

	m_lpPostRequest = new CCurl(szUrl, m_lpTargetWindow);

	m_lpPostRequest->SetUserAgent(USERAGENT);
	m_lpPostRequest->SetTimeout(WEB_TIMEOUT_SHORT);
	m_lpPostRequest->SetIgnoreSSLErrors(TRUE);
	m_lpPostRequest->SetCookies(GetCookies());

	m_lpPostRequest->SetUrlEncodedPostData(szPostData.str());

	CNotifierApp::Instance()->QueueRequest(m_lpPostRequest);

	for (TWaveRequestVectorIter iter1 = m_vRequestQueue.begin(); iter1 != m_vRequestQueue.end(); iter1++)
	{
		ASSERT(*iter1 != NULL);

		(*iter1)->RequestCompleted();

		delete *iter1;
	}

	m_vRequestQueue.clear();
}

wstring CWaveSession::SerializeRequest(CWaveRequest * lpRequest)
{
	ASSERT(lpRequest != NULL);

	// Prepare the request basics.

	Json::Value vRoot(Json::objectValue);

	vRoot[L"a"] = Json::Value(GetSessionID());
	vRoot[L"r"] = Json::Value(Format(L"%x", m_nNextRequestID++));
	vRoot[L"t"] = Json::Value(lpRequest->GetType());
	vRoot[L"p"] = Json::Value(Json::objectValue);

	// Let the request object write the parameters.

	lpRequest->CreateRequest(vRoot[L"p"]);

	// Get the encoded JSON data.

	Json::FastWriter vWriter;

	return vWriter.write(vRoot);
}

void CWaveSession::AddListener(CWaveListener * lpListener)
{
	ASSERT(lpListener != NULL);

	m_vListeners[lpListener->GetID()] = lpListener;
}

CWaveListener * CWaveSession::CreateListener(wstring szSearchString)
{
	ASSERT(!szSearchString.empty());

	return new CWaveListener(
		Format(L"%s%d", GetSessionID().c_str(), m_nNextListenerID++),
		szSearchString
	);
}

BOOL CWaveSession::RemoveListener(wstring szID)
{
	ASSERT(!szID.empty());

	TWaveListenerMapIter pos = m_vListeners.find(szID);

	BOOL fResult = pos != m_vListeners.end();

	if (fResult)
	{
		m_vListeners.erase(pos);

		delete pos->second;
	}

	return fResult;
}

void CWaveSession::ReconnectTimer()
{
	m_lpReconnectTimer->SetRunning(FALSE);

	Reconnect();
}

void CWaveSession::NextReconnect()
{
	INT nNextInterval = m_lpReconnectTimer->GetInterval() * 2;

	m_lpReconnectTimer->SetInterval(
		nNextInterval > TIMER_RECONNECT_INTERVAL_MAX ? TIMER_RECONNECT_INTERVAL_MAX : nNextInterval
	);

	m_lpReconnectTimer->SetRunning(TRUE);
}

void CWaveSession::StopReconnecting()
{
	if (m_nState != WSS_RECONNECTING)
	{
		LOG("StopReconnecting called while not reconnecting");
	}
	else
	{
		if (m_lpCookies != NULL)
		{
			delete m_lpCookies;
		}

		m_lpCookies = NULL;

		m_nState = WSS_OFFLINE;

		SignalProgress(WCS_SIGNED_OUT);

		m_lpReconnectTimer->SetRunning(FALSE);
	}
}

void CWaveSession::QueueRequest(CWaveRequest * lpRequest)
{
	m_vRequestQueue.push_back(lpRequest);
}

void CWaveSession::FlushRequestQueue()
{
	if (
		m_nFlushSuspended == 0 &&
		!m_szSID.empty() &&
		m_lpPostRequest == NULL &&
		m_vRequestQueue.size() > 0
	) {
		PostRequests();
	}
}

void CWaveSession::SuspendRequestFlush()
{
	m_nFlushSuspended++;
}

void CWaveSession::ReleaseRequestFlush()
{
	m_nFlushSuspended--;

	ASSERT(m_nFlushSuspended >= 0);

	if (m_nFlushSuspended == 0)
	{
		FlushRequestQueue();
	}
}

void CWaveSession::ForceReconnect()
{
	if (m_nState == WSS_ONLINE && m_lpChannelRequest != NULL)
	{
		CNotifierApp::Instance()->CancelRequest(m_lpChannelRequest);
	}
}
