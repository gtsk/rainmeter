/*
  Copyright (C) 2004 Kimmo Pekkola

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "StdAfx.h"
#include "TrayWindow.h"
#include "Measure.h"
#include "resource.h"
#include "Litestep.h"
#include "Rainmeter.h"
#include "DialogAbout.h"
#include "DialogManage.h"
#include "System.h"
#include "Error.h"
#include "RainmeterQuery.h"
#include "resource.h"
#include "../Version.h"

#define RAINMETER_OFFICIAL		L"http://rainmeter.net/cms/"
#define RAINMETER_HELP			L"http://docs.rainmeter.net/"

#define ZPOS_FLAGS	(SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING)

enum TIMER
{
	TIMER_ADDTRAYICON = 1,
	TIMER_TRAYMEASURE = 3
};
enum INTERVAL
{
	INTERVAL_ADDTRAYICON = 3000,
	INTERVAL_TRAYMEASURE = 1000
};

const UINT WM_TASKBARCREATED = ::RegisterWindowMessage(L"TaskbarCreated");

extern Rainmeter* g_Rainmeter;

using namespace Gdiplus;

TrayWindow::TrayWindow() :
	m_Icon(),
	m_Measure(),
	m_MeterType(TRAY_METER_TYPE_HISTOGRAM),
	m_Color1(0, 100, 0),
	m_Color2(0, 255, 0),
	m_Bitmap(),
	m_Values(),
	m_Pos(),
	m_Notification(TRAY_NOTIFICATION_NONE),
	m_TrayContextMenuEnabled(true),
	m_IconEnabled(true)
{
}

TrayWindow::~TrayWindow()
{
	KillTimer(m_Window, TIMER_ADDTRAYICON);
	KillTimer(m_Window, TIMER_TRAYMEASURE);
	RemoveTrayIcon();

	delete m_Bitmap;
	delete m_Measure;

	for (size_t i = 0, isize = m_Icons.size(); i < isize; ++i)
	{
		DestroyIcon(m_Icons[i]);
	}
	m_Icons.clear();

	if (m_Window) DestroyWindow(m_Window);
}

void TrayWindow::Initialize()
{
	WNDCLASS wc = {0};
	wc.lpfnWndProc = (WNDPROC)WndProc;
	wc.hInstance = g_Rainmeter->GetInstance();
	wc.lpszClassName = L"RainmeterTrayClass";
	wc.hIcon = GetIcon(IDI_RAINMETER);

	RegisterClass(&wc);

	m_Window = CreateWindowEx(
		WS_EX_TOOLWINDOW,
		L"RainmeterTrayClass",
		NULL,
		WS_POPUP | WS_DISABLED,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		wc.hInstance,
		this);

	SetWindowPos(m_Window, HWND_BOTTOM, 0, 0, 0, 0, ZPOS_FLAGS);
}

bool TrayWindow::AddTrayIcon()
{
	NOTIFYICONDATA tnid = {sizeof(NOTIFYICONDATA)};
	tnid.hWnd = m_Window;
	tnid.uID = IDI_TRAY;
	tnid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	tnid.uCallbackMessage = WM_TRAY_NOTIFYICON;
	tnid.hIcon = m_Icon;
	wcsncpy_s(tnid.szTip, APPNAME, _TRUNCATE);

	return (Shell_NotifyIcon(NIM_ADD, &tnid) || GetLastError() != ERROR_TIMEOUT);
}

bool TrayWindow::IsTrayIconReady()
{
	NOTIFYICONDATA tnid = {sizeof(NOTIFYICONDATA)};
	tnid.hWnd = m_Window;
	tnid.uID = IDI_TRAY;

	return Shell_NotifyIcon(NIM_MODIFY, &tnid);
}

void TrayWindow::TryAddTrayIcon()
{
	if (IsTrayIconReady())
	{
		ModifyTrayIcon(0);
		return;
	}

	if (m_Icon)
	{
		DestroyIcon(m_Icon);
		m_Icon = NULL;
	}

	m_Icon = CreateTrayIcon(0);

	if (!AddTrayIcon())
	{
		SetTimer(m_Window, TIMER_ADDTRAYICON, INTERVAL_ADDTRAYICON, NULL);
	}
}

void TrayWindow::CheckTrayIcon()
{
	if (IsTrayIconReady() || AddTrayIcon())
	{
		KillTimer(m_Window, TIMER_ADDTRAYICON);
	}
}

void TrayWindow::RemoveTrayIcon()
{
	NOTIFYICONDATA tnid = {sizeof(NOTIFYICONDATA)};
	tnid.hWnd = m_Window;
	tnid.uID = IDI_TRAY;
	tnid.uFlags = 0;

	Shell_NotifyIcon(NIM_DELETE, &tnid);

	if (m_Icon)
	{
		DestroyIcon(m_Icon);
		m_Icon = NULL;
	}
}

void TrayWindow::ModifyTrayIcon(double value)
{
	if (m_Icon)
	{
		DestroyIcon(m_Icon);
		m_Icon = NULL;
	}

	m_Icon = CreateTrayIcon(value);

	NOTIFYICONDATA tnid = {sizeof(NOTIFYICONDATA)};
	tnid.hWnd = m_Window;
	tnid.uID = IDI_TRAY;
	tnid.uFlags = NIF_ICON;
	tnid.hIcon = m_Icon;

	Shell_NotifyIcon(NIM_MODIFY, &tnid);
}

HICON TrayWindow::CreateTrayIcon(double value)
{
	if (m_Measure != NULL)
	{
		if (m_MeterType == TRAY_METER_TYPE_HISTOGRAM)
		{
			m_Values[m_Pos] = value;
			m_Pos = (m_Pos + 1) % TRAYICON_SIZE;

			Bitmap trayBitmap(TRAYICON_SIZE, TRAYICON_SIZE);
			Graphics graphics(&trayBitmap);
			graphics.SetSmoothingMode(SmoothingModeAntiAlias);

			Point points[TRAYICON_SIZE + 2];
			points[0].X = 0;
			points[0].Y = TRAYICON_SIZE;
			points[TRAYICON_SIZE + 1].X = TRAYICON_SIZE - 1;
			points[TRAYICON_SIZE + 1].Y = TRAYICON_SIZE;

			for (int i = 0; i < TRAYICON_SIZE; ++i)
			{
				points[i + 1].X = i;
				points[i + 1].Y = (int)(TRAYICON_SIZE * (1.0 - m_Values[(m_Pos + i) % TRAYICON_SIZE]));
			}

			SolidBrush brush(m_Color1);
			graphics.FillRectangle(&brush, 0, 0, TRAYICON_SIZE, TRAYICON_SIZE);

			SolidBrush brush2(m_Color2);
			graphics.FillPolygon(&brush2, points, TRAYICON_SIZE + 2);

			HICON icon = NULL;
			trayBitmap.GetHICON(&icon);
			return icon;
		}
		else if (m_MeterType == TRAY_METER_TYPE_BITMAP && (m_Bitmap || !m_Icons.empty()))
		{
			if (!m_Icons.empty())
			{
				size_t frame = 0;
				size_t frameCount = m_Icons.size();

				// Select the correct frame linearly
				frame = (size_t)(value * frameCount);
				frame = min((frameCount - 1), frame);

				return CopyIcon(m_Icons[frame]);
			}
			else
			{
				int frame = 0;
				int frameCount = 0;
				int newX, newY;

				if (m_Bitmap->GetWidth() > m_Bitmap->GetHeight())
				{
					frameCount = m_Bitmap->GetWidth() / TRAYICON_SIZE;
				}
				else
				{
					frameCount = m_Bitmap->GetHeight() / TRAYICON_SIZE;
				}

				// Select the correct frame linearly
				frame = (int)(value * frameCount);
				frame = min((frameCount - 1), frame);

				if (m_Bitmap->GetWidth() > m_Bitmap->GetHeight())
				{
					newX = frame * TRAYICON_SIZE;
					newY = 0;
				}
				else
				{
					newX = 0;
					newY = frame * TRAYICON_SIZE;
				}

				Bitmap trayBitmap(TRAYICON_SIZE, TRAYICON_SIZE);
				Graphics graphics(&trayBitmap);
				graphics.SetSmoothingMode(SmoothingModeAntiAlias);

				// Blit the image
				Rect r(0, 0, TRAYICON_SIZE, TRAYICON_SIZE);
				graphics.DrawImage(m_Bitmap, r, newX, newY, TRAYICON_SIZE, TRAYICON_SIZE, UnitPixel);

				HICON icon = NULL;
				trayBitmap.GetHICON(&icon);
				return icon;
			}
		}
	}

	// Return the default icon if there is no valid measure
	return GetIcon(IDI_TRAY);
}

void TrayWindow::ShowNotification(TRAY_NOTIFICATION id, const WCHAR* title, const WCHAR* text)
{
	if (m_Notification == TRAY_NOTIFICATION_NONE)
	{
		NOTIFYICONDATA nid = {sizeof(NOTIFYICONDATA)};
		nid.hWnd = m_Window;
		nid.uID = IDI_TRAY;
		nid.uFlags = NIF_INFO;
		nid.uTimeout = 30000;
		nid.dwInfoFlags = NIIF_USER;
		wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
		wcsncpy_s(nid.szInfo, text, _TRUNCATE);

		if (Platform::IsAtLeastWin7())
		{
			nid.dwInfoFlags |= NIIF_LARGE_ICON;
			nid.hBalloonIcon = GetIcon(IDI_RAINMETER, true);
		}

		if (Shell_NotifyIcon(NIM_MODIFY, &nid))
		{
			m_Notification = id;
		}
	}
}

void TrayWindow::ShowWelcomeNotification()
{
	ShowNotification(TRAY_NOTIFICATION_WELCOME, GetString(ID_STR_WELCOME), GetString(ID_STR_CLICKTOMANAGE));
}

void TrayWindow::ShowUpdateNotification(const WCHAR* newVersion)
{
	std::wstring text = GetFormattedString(ID_STR_CLICKTODOWNLOAD, newVersion);
	ShowNotification(TRAY_NOTIFICATION_UPDATE, GetString(ID_STR_UPDATEAVAILABLE), text.c_str());
}

void TrayWindow::SetTrayIcon(bool enabled)
{
	enabled ? TryAddTrayIcon() : RemoveTrayIcon();
	m_IconEnabled = enabled;

	// Save to Rainmeter.ini.
	const std::wstring& iniFile = g_Rainmeter->GetIniFile();
	WritePrivateProfileString(L"Rainmeter", L"TrayIcon", enabled ? NULL : L"0", iniFile.c_str());
}

void TrayWindow::ReadOptions(ConfigParser& parser)
{
	// Clear old Settings
	KillTimer(m_Window, TIMER_ADDTRAYICON);
	KillTimer(m_Window, TIMER_TRAYMEASURE);

	delete m_Measure;
	m_Measure = NULL;

	delete m_Bitmap;
	m_Bitmap = NULL;

	std::vector<HICON>::const_iterator iter = m_Icons.begin();
	for ( ; iter != m_Icons.end(); ++iter)
	{
		DestroyIcon((*iter));
	}
	m_Icons.clear();

	m_MeterType = TRAY_METER_TYPE_NONE;

	// Read tray settings
	m_IconEnabled = 0!=parser.ReadInt(L"Rainmeter", L"TrayIcon", 1);
	if (m_IconEnabled)
	{
		const std::wstring& measureName = parser.ReadString(L"TrayMeasure", L"Measure", L"");

		if (!measureName.empty())
		{
			ConfigParser* oldParser = g_Rainmeter->GetCurrentParser();
			g_Rainmeter->SetCurrentParser(&parser);

			m_Measure = Measure::Create(measureName.c_str(), NULL, L"TrayMeasure");
			if (m_Measure)
			{
				m_Measure->ReadOptions(parser);
			}

			g_Rainmeter->SetCurrentParser(oldParser);
		}

		const WCHAR* type = parser.ReadString(L"TrayMeasure", L"TrayMeter", m_Measure ? L"HISTOGRAM" : L"NONE").c_str();
		if (_wcsicmp(type, L"NONE") == 0)
		{
			// Use main icon
		}
		else if (_wcsicmp(type, L"HISTOGRAM") == 0)
		{
			m_MeterType = TRAY_METER_TYPE_HISTOGRAM;
			m_Color1 = parser.ReadColor(L"TrayMeasure", L"TrayColor1", Color::MakeARGB(255, 0, 100, 0));
			m_Color2 = parser.ReadColor(L"TrayMeasure", L"TrayColor2", Color::MakeARGB(255, 0, 255, 0));
		}
		else if (_wcsicmp(type, L"BITMAP") == 0)
		{
			m_MeterType = TRAY_METER_TYPE_BITMAP;

			std::wstring imageName = parser.ReadString(L"TrayMeasure", L"TrayBitmap", L"");

			// Load the bitmaps if defined
			if (!imageName.empty())
			{
				imageName.insert(0, g_Rainmeter->GetSkinPath());
				const WCHAR* imagePath = imageName.c_str();
				if (_wcsicmp(imagePath + (imageName.size() - 4), L".ico") == 0)
				{
					int count = 1;
					HICON hIcon = NULL;

					// Load the icons
					do
					{
						WCHAR buffer[MAX_PATH];
						_snwprintf_s(buffer, _TRUNCATE, imagePath, count++);

						hIcon = (HICON)LoadImage(NULL, buffer, IMAGE_ICON, TRAYICON_SIZE, TRAYICON_SIZE, LR_LOADFROMFILE);
						if (hIcon) m_Icons.push_back(hIcon);
						if (wcscmp(imagePath, buffer) == 0) break;
					}
					while(hIcon != NULL);
				}

				if (m_Icons.empty())
				{
					// No icons found so load as bitmap
					delete m_Bitmap;
					m_Bitmap = new Bitmap(imagePath);
					Status status = m_Bitmap->GetLastStatus();
					if (Ok != status)
					{
						delete m_Bitmap;
						m_Bitmap = NULL;
						LogWarningF(L"Bitmap image not found: %s", imagePath);
					}
				}
			}
		}
		else
		{
			LogErrorF(L"No such TrayMeter: %s", type);
		}

		TryAddTrayIcon();

		if (m_Measure)
		{
			SetTimer(m_Window, TIMER_TRAYMEASURE, INTERVAL_TRAYMEASURE, NULL);  // Update the tray once per sec
		}
	}
	else
	{
		RemoveTrayIcon();
	}
}

LRESULT CALLBACK TrayWindow::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	TrayWindow* tray = g_Rainmeter->GetTrayWindow();

	switch (uMsg)
	{
	case WM_COMMAND:
		switch (wParam)
		{
		case IDM_MANAGE:
			DialogManage::Open();
			break;

		case IDM_ABOUT:
			DialogAbout::Open();
			break;

		case IDM_SHOW_HELP:
			CommandHandler::RunFile(RAINMETER_HELP);
			break;

		case IDM_NEW_VERSION:
			CommandHandler::RunFile(RAINMETER_OFFICIAL);
			break;

		case IDM_REFRESH:
			PostMessage(g_Rainmeter->GetWindow(), WM_RAINMETER_DELAYED_REFRESH_ALL, (WPARAM)NULL, (LPARAM)NULL);
			break;

		case IDM_SHOWLOGFILE:
			g_Rainmeter->ShowLogFile();
			break;

		case IDM_STARTLOG:
			Logger::GetInstance().StartLogFile();
			break;

		case IDM_STOPLOG:
			Logger::GetInstance().StopLogFile();
			break;

		case IDM_DELETELOGFILE:
			Logger::GetInstance().DeleteLogFile();
			break;

		case IDM_DEBUGLOG:
			g_Rainmeter->SetDebug(!g_Rainmeter->GetDebug());
			break;

		case IDM_DISABLEDRAG:
			g_Rainmeter->SetDisableDragging(!g_Rainmeter->GetDisableDragging());
			break;

		case IDM_EDITCONFIG:
			g_Rainmeter->EditSettings();
			break;

		case IDM_QUIT:
			PostQuitMessage(0);
			break;

		case IDM_OPENSKINSFOLDER:
			g_Rainmeter->OpenSkinFolder();
			break;

		default:
			{
				UINT mID = wParam & 0x0FFFF;

				if (mID >= ID_THEME_FIRST && mID <= ID_THEME_LAST)
				{
					int pos = mID - ID_THEME_FIRST;

					const std::vector<std::wstring>& layouts = g_Rainmeter->GetAllLayouts();
					if (pos >= 0 && pos < (int)layouts.size())
					{
						g_Rainmeter->LoadLayout(layouts[pos]);
					}
				}
				else if (mID >= ID_CONFIG_FIRST && mID <= ID_CONFIG_LAST)
				{
					std::pair<int, int> indexes = g_Rainmeter->GetMeterWindowIndex(mID);
					if (indexes.first != -1 && indexes.second != -1)
					{
						g_Rainmeter->ToggleSkin(indexes.first, indexes.second);
					}
				}
				else
				{
					// Forward the message to correct window
					int index = (int)(wParam >> 16);
					const std::map<std::wstring, MeterWindow*>& windows = g_Rainmeter->GetAllMeterWindows();

					if (index < (int)windows.size())
					{
						std::map<std::wstring, MeterWindow*>::const_iterator iter = windows.begin();
						for ( ; iter != windows.end(); ++iter)
						{
							--index;
							if (index < 0)
							{
								MeterWindow* meterWindow = (*iter).second;
								SendMessage(meterWindow->GetWindow(), WM_COMMAND, mID, NULL);
								break;
							}
						}
					}
				}
			}
			break;
		}
		break;	// Don't send WM_COMMANDS any further

	case WM_TRAY_NOTIFYICON:
		{
			UINT uMouseMsg = (UINT)lParam;
			LPCWSTR bang;

			// Check TrayExecute actions
			switch (uMouseMsg)
			{
			case WM_MBUTTONDOWN:
				bang = g_Rainmeter->GetTrayExecuteM().c_str();
				break;

			case WM_RBUTTONDOWN:
				bang = g_Rainmeter->GetTrayExecuteR().c_str();
				break;

			case WM_MBUTTONDBLCLK:
				bang = g_Rainmeter->GetTrayExecuteDM().c_str();
				break;

			case WM_RBUTTONDBLCLK:
				bang = g_Rainmeter->GetTrayExecuteDR().c_str();
				break;

			default:
				bang = L"";
				break;
			}

			if (*bang &&
				!IsCtrlKeyDown())   // Ctrl is pressed, so only run default action
			{
				g_Rainmeter->ExecuteCommand(bang, NULL);
				tray->m_TrayContextMenuEnabled = (uMouseMsg != WM_RBUTTONDOWN);
				break;
			}

			// Run default UI action
			switch (uMouseMsg)
			{
			case WM_RBUTTONDOWN:
				tray->m_TrayContextMenuEnabled = true;
				break;

			case WM_RBUTTONUP:
				if (tray->m_TrayContextMenuEnabled)
				{
					POINT pos = System::GetCursorPosition();
					g_Rainmeter->ShowContextMenu(pos, NULL);
				}
				break;

			case WM_LBUTTONUP:
			case WM_LBUTTONDBLCLK:
				DialogManage::Open();
				break;

			case NIN_BALLOONUSERCLICK:
				if (tray->m_Notification == TRAY_NOTIFICATION_WELCOME)
				{
					DialogManage::Open();
				}
				else if (tray->m_Notification == TRAY_NOTIFICATION_UPDATE)
				{
					CommandHandler::RunFile(RAINMETER_OFFICIAL);
				}
				tray->m_Notification = TRAY_NOTIFICATION_NONE;
				break;

			case NIN_BALLOONHIDE:
			case NIN_BALLOONTIMEOUT:
				tray->m_Notification = TRAY_NOTIFICATION_NONE;
				break;
			}
		}
		break;

	case WM_QUERY_RAINMETER:
		if (IsWindow((HWND)lParam))
		{
			auto sendCopyData = [&](const std::wstring& data)
			{
				COPYDATASTRUCT cds;
				cds.dwData = wParam;
				cds.cbData = (DWORD)((data.length() + 1) * sizeof(WCHAR));
				cds.lpData = (PVOID)data.c_str();
				SendMessage((HWND)lParam, WM_COPYDATA, (WPARAM)hWnd, (LPARAM)&cds);
			};

			switch (wParam)
			{
			case RAINMETER_QUERY_ID_SKINS_PATH:
				sendCopyData(g_Rainmeter->GetSkinPath());
				return 0;

			case RAINMETER_QUERY_ID_SETTINGS_PATH:
				sendCopyData(g_Rainmeter->GetSettingsPath());
				return 0;

			case RAINMETER_QUERY_ID_PLUGINS_PATH:
				sendCopyData(g_Rainmeter->GetPluginPath());
				return 0;

			case RAINMETER_QUERY_ID_PROGRAM_PATH:
				sendCopyData(g_Rainmeter->GetPath());
				return 0;

			case RAINMETER_QUERY_ID_LOG_PATH:
				sendCopyData(Logger::GetInstance().GetLogFilePath());
				return 0;

			case RAINMETER_QUERY_ID_CONFIG_EDITOR:
				sendCopyData(g_Rainmeter->GetSkinEditor());
				return 0;

			case RAINMETER_QUERY_ID_IS_DEBUGGING:
				{
					BOOL debug = g_Rainmeter->GetDebug();
					SendMessage((HWND)lParam, WM_QUERY_RAINMETER_RETURN, (WPARAM)hWnd, (LPARAM)debug);
				}
				return 0;
			}
		}
		return 1;

	case WM_COPYDATA:
		{
			COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
			if (cds->dwData == RAINMETER_QUERY_ID_SKIN_WINDOWHANDLE)
			{
				LPCWSTR folderPath = (LPCWSTR)cds->lpData;
				MeterWindow* mw = g_Rainmeter->GetMeterWindow(folderPath);
				return (mw) ? (LRESULT)mw->GetWindow() : NULL;
			}
		}
		return 1;

	case WM_TIMER:
		if (wParam == TIMER_TRAYMEASURE)
		{
			if (tray->m_Measure)
			{
				tray->m_Measure->Update();
				tray->ModifyTrayIcon(tray->m_Measure->GetRelativeValue());
			}
		}
		else if (wParam == TIMER_ADDTRAYICON)
		{
			tray->CheckTrayIcon();
		}
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	default:
		if (uMsg == WM_TASKBARCREATED)
		{
			if (tray->IsTrayIconEnabled())
			{
				tray->RemoveTrayIcon();
				tray->TryAddTrayIcon();
			}
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	return 0;
}
