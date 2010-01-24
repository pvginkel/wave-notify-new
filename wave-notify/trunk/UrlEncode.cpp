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

typedef enum
{
	UrlEncodeModeNormal,
	UrlEncodeModePath
} UrlEncodeMode;

static wstring UrlEncode(wstring szSource, UrlEncodeMode nMode);

wstring UrlEncode(wstring szSource)
{
	return UrlEncode(szSource, UrlEncodeModeNormal);
}

wstring UrlEncodePath(wstring szSource)
{
	return UrlEncode(szSource, UrlEncodeModePath);
}

static wstring UrlEncode(wstring szSource, UrlEncodeMode nMode)
{
	string szSourceA = ConvertToMultiByte(szSource, CP_UTF8);
	size_t nLength = 0;
	ostringstream szResultA;

	for (string::const_iterator iter = szSourceA.begin(); iter < szSourceA.end(); iter++)
	{
		BOOL fParsed = FALSE;

		switch (*iter)
		{
		case '-':
		case '_':
		case '.':
			szResultA << *iter;
			fParsed = TRUE;
			break;

		case ' ':
			if (nMode == UrlEncodeModeNormal)
			{
				szResultA << '+';
				fParsed = TRUE;
				break;
			}
			break;

		case ':':
		case '/':
			if (nMode == UrlEncodeModePath)
			{
				szResultA << *iter;
				fParsed = TRUE;
			}
			break;

		case '\\':
			if (nMode == UrlEncodeModePath)
			{
				szResultA << '/';
				fParsed = TRUE;
			}
			break;
		}

		if (!fParsed)
		{
			if (isalnum(*iter))
			{
				szResultA << *iter;
			}
			else
			{
				szResultA
					<< '%'
					<< setbase(16)
					<< setfill('0')
					<< setw(2)
					<< setiosflags(ios_base::uppercase)
					<< (int)(unsigned char)*iter
					<< resetiosflags(ios_base::uppercase);
			}
		}
	}

	return ConvertToWideChar(szResultA.str());
}
