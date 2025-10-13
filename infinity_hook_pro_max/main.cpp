#include "hook.hpp"
#include "imports.hpp"
#include "main.h"

typedef NTSTATUS(*FNtCreateFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);

// Global variables to store function addresses
FNtCreateFile g_NtCreateFile = 0;
dxgk_submit_command_t OriginalNtGdiDdDDISubmitCommand = NULL;
GetDC_t NtUserGetDC = 0;
PatBlt_t NtGdiPatBlt = 0;
SelectBrush_t NtGdiSelectBrush = 0;
ReleaseDC_t NtUserReleaseDC = 0;
CreateSolidBrush_t NtGdiCreateSolidBrush = 0;
DeleteObjectApp_t NtGdiDeleteObjectApp = 0;
ExtTextOutW_t NtGdiExtTextOutW = 0;
HfontCreate_t NtGdiHfontCreate = 0;
SelectFont_t NtGdiSelectFont = 0;
NtUserGetWindowDisplayAffinity_t OriginalNtUserGetWindowDisplayAffinity = 0;
NtUserSetWindowDisplayAffinity_t OriginalNtUserSetWindowDisplayAffinity = 0;
NtGdiBitBlt_t OriginalNtGdiBitBlt = 0;

// Global array to store hwnd values from multiple processes
#define MAX_HWND_ENTRIES 1000
HWND_ENTRY g_HwndEntries[MAX_HWND_ENTRIES];
ULONG g_HwndEntryCount = 0;
FAST_MUTEX g_HwndArrayMutex;

NTSTATUS MyNtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
	// The caller of NtCreateFile must be running at IRQL = PASSIVE_LEVEL and with special kernel APCs enabled.
	if (KeGetCurrentIrql() != PASSIVE_LEVEL) return g_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
	if (ExGetPreviousMode() == KernelMode) return g_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
	if (PsGetProcessSessionId(IoGetCurrentProcess()) == 0) return g_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);

	//Initialize other system calls using the initSysCalls function
	if (initSysCalls() != STATUS_SUCCESS) {
		return STATUS_UNSUCCESSFUL;
	}

	if (ObjectAttributes &&
		ObjectAttributes->ObjectName &&
		ObjectAttributes->ObjectName->Buffer)
	{
		wchar_t* name = (wchar_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, ObjectAttributes->ObjectName->Length + sizeof(wchar_t), 'VMON');
		if (name)
		{
			RtlZeroMemory(name, ObjectAttributes->ObjectName->Length + sizeof(wchar_t));
			RtlCopyMemory(name, ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length);

			if (wcsstr(name, L"secret"))
			{
				// DbgPrintEx(0, 0, "Call %ws \n", name);

				ExFreePool(name);
				return STATUS_ACCESS_DENIED;
			}

			ExFreePool(name);
		}
	}

	return g_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}



PVOID AllocateVirtualMemory(SIZE_T Size)
{
	PVOID pMem = NULL;
	NTSTATUS statusAlloc = ZwAllocateVirtualMemory(NtCurrentProcess(), &pMem, 0, &Size, MEM_COMMIT, PAGE_READWRITE);
	//kprintf("[+] AllocateVirtualMemory statusAlloc: %x \n", statusAlloc);
	return pMem;
}

VOID FreeVirtualMemory(PVOID VirtualAddress, SIZE_T Size)
{
	if (MmIsAddressValid(VirtualAddress))
	{
		NTSTATUS Status = ZwFreeVirtualMemory(NtCurrentProcess(), &VirtualAddress, &Size, MEM_RELEASE);

		if (!NT_SUCCESS(Status)) {
			kprintf("[-] GDI.cpp Warning : Released memory failed.FreeVirtualMemory Internal Function\r\n");
		}
		return;
	}
	kprintf("[-] GDI.cpp Warning: Released memory does not exist.FreeVirtualMemory Internal Function\r\n");
}

BOOL extTextOutW(HDC hdc, INT x, INT y, UINT fuOptions, RECT * lprc, LPWSTR lpString, UINT cwc, INT * lpDx)
{
	BOOL		nRet = FALSE;
	PVOID       local_lpString = NULL;
	RECT*       local_lprc = NULL;
	INT*        local_lpDx = NULL;

	if (lprc != NULL)
	{
		SIZE_T Len = sizeof(RECT);
		local_lprc = (RECT *)AllocateVirtualMemory(Len);
		if (local_lprc != NULL)
		{
			__try
			{
				RtlZeroMemory(local_lprc, Len);
				RtlCopyMemory(local_lprc, lprc, Len);
			}
			__except (1)
			{
				kprintf("GDI.cpp Line RtlCopyMemory  Triggers An Error.ExtTextOutW Internal Function\r\n");
				goto $EXIT;
			}
		}
		else
		{
			kprintf("GDI.cpp Line local_lprc = null  Triggers An Error.ExtTextOutW Internal Function\r\n");
			goto $EXIT;
		}
	}

	if (cwc != 0)
	{
		SIZE_T     AllocSize = sizeof(WCHAR)*cwc + 1;
		local_lpString = AllocateVirtualMemory(AllocSize);

		if (local_lpString != NULL)
		{
			__try
			{
				RtlZeroMemory(local_lpString, AllocSize);
				RtlCopyMemory(local_lpString, lpString, AllocSize);
			}
			__except (1)
			{
				kprintf("[-] GDI.cpp Line RtlCopyMemory  Triggers An Error.ExtTextOutW Internal Function\r\n");
				goto $EXIT;
			}
		}
		else
		{
			kprintf("[-] GDI.cpp Line local_lpString = null  Triggers An Error.ExtTextOutW Internal Function\r\n");
			goto $EXIT;
		}
	}

	if (local_lpDx != NULL)
	{
		SIZE_T     AllocSize = sizeof(INT);
		local_lpDx = (INT *)AllocateVirtualMemory(AllocSize);
		if (local_lpDx != NULL)
		{
			__try
			{
				RtlZeroMemory(local_lpString, AllocSize);
				*local_lpDx = *lpDx;
			}
			__except (1)
			{
				kprintf("[-] GDI.cpp Line RtlCopyMemory  Triggers An Error.ExtTextOutW Internal Function\r\n");
				goto $EXIT;
			}
		}
		else
		{
			kprintf("[-] GDI.cpp Line local_lpDx = null  Triggers An Error.ExtTextOutW Internal Function\r\n");
		}
	}

	if (NtGdiExtTextOutW != NULL) {
		nRet = NtGdiExtTextOutW(hdc, x, y, fuOptions, local_lprc, (LPWSTR)local_lpString, cwc, local_lpDx, 0);
	}
	else {
		kprintf("[-] GDI.cpp Line NtGdiExtTextOutW = NULL Triggers An Error.TextOutW Internal Function\r\n");
	}
$EXIT:
	if (lprc != NULL)
	{
		FreeVirtualMemory(lprc, sizeof(RECT));
		lprc = NULL;
	}

	if (local_lpDx != NULL)
	{
		FreeVirtualMemory(local_lpDx, sizeof(INT));
		local_lpDx = NULL;
	}

	if (local_lpString != NULL)
	{
		FreeVirtualMemory(local_lpString, cwc);
		local_lpString = NULL;
	}

	return nRet;
}

BOOL extTextOutA(HDC hdc, INT x, INT y, UINT fuOptions, RECT * lprc, LPCSTR lpString, UINT cch, INT * lpDx)
{
	ANSI_STRING StringA;
	UNICODE_STRING StringU;
	BOOL ret;
	RtlInitAnsiString(&StringA, (LPSTR)lpString);
	RtlAnsiStringToUnicodeString(&StringU, &StringA, TRUE);
	ret = extTextOutW(hdc, x, y, fuOptions, lprc, StringU.Buffer, cch, lpDx);
	RtlFreeUnicodeString(&StringU);
	return ret;
}

PVOID GetProcessPeb(PEPROCESS Eprocess)
{
	PVOID pPeb = PsGetProcessPeb(Eprocess);
	if (pPeb == NULL)
	{
		pPeb = PsGetProcessWow64Process(Eprocess);
		if (pPeb == NULL)
		{
			kprintf("Process.cpp PsGetProcessWow64Process() An Error.GetProcessPeb Internal Function");
		}
	}
	return pPeb;
}

BOOL GdiGetHandleUserData(HGDIOBJ hGdiObj, DWORD ObjectType, PVOID * UserData)
{
	PEPROCESS Eprocess = IoGetCurrentProcess();
	PVOID ppeb = GetProcessPeb(Eprocess);
	//kprintf("[+] ppeb: %p \n",ppeb);
	if (ppeb == NULL) {
		kprintf("SetTextColor.cpp GetProcessPeb() An Error.GdiGetHandleUserData() Internal Function\r\n");
	}
	GDICELL* Entry = (GDICELL*)*(LPVOID*)((ULONG64)ppeb + 0xF8);//GdiSharedHandleTable
	if (Entry == NULL) {
		kprintf("SetTextColor.cpp Entry == NULL An Error.GdiGetHandleUserData() Internal Function\r\n");
		return FALSE;
	}
	Entry = Entry + GDI_HANDLE_GET_INDEX(hGdiObj);
	if (Entry == NULL) {
		kprintf("SetTextColor.cpp  Entry + GDI_HANDLE_GET_INDEX(hGdiObj) == NULL An Error.GdiGetHandleUserData() Internal Function\r\n");
		return FALSE;
	}

	if (MmIsAddressValid(Entry->pUserAddress) != TRUE) {
		kprintf("SetTextColor.cpp  MmIsAddressValid(Entry->pUserAddress) != TRUE An Error.GdiGetHandleUserData() Internal Function\r\n");
		return FALSE;
	}

	*UserData = Entry->pUserAddress;
	//kprintf("[+] UserData: %p \n", *UserData);
	//kprintf("[+] Entry->pUserAddress: %p \n", Entry->pUserAddress);

	return TRUE;
}

PDC_ATTR GdiGetDcAttr(HDC hdc)
{

	GDILOOBJTYPE eDcObjType;
	PDC_ATTR pdcattr;

	/* Check DC object type */
	eDcObjType = (GDILOOBJTYPE)GDI_HANDLE_GET_TYPE(hdc); //ok
	//kprintf("[+] eDcObjType: %x \n", eDcObjType);

	if ((eDcObjType != GDILoObjType_LO_DC_TYPE) &&
		(eDcObjType != GDILoObjType_LO_ALTDC_TYPE))
	{
		return NULL;
	}
	if (!GdiGetHandleUserData((HGDIOBJ)hdc, eDcObjType, (PVOID*)&pdcattr))
	{
		return NULL;
	}
	return pdcattr;


}
COLORREF SetTextColor(HDC hdc, COLORREF crColor)
{
	PDC_ATTR pdcattr;
	COLORREF crOldColor;
	pdcattr = GdiGetDcAttr(hdc);
	kprintf("[+] pdcattr pointer: %p \n", pdcattr);
	kprintf("[+] pdcattr crForegroundClr: %x \n", pdcattr->crForegroundClr);
	kprintf("[+] pdcattr ulForegroundClr: %x \n", pdcattr->ulForegroundClr);
	kprintf("[+] pdcattr iGraphicsMode: %i \n", pdcattr->iGraphicsMode);
	kprintf("[+] pdcattr crBackgroundClr: %x \n", pdcattr->crBackgroundClr);
	kprintf("[+] pdcattr ulBackgroundClr: %x \n", pdcattr->ulBackgroundClr);
	kprintf("[+] pdcattr crBrushClr: %x \n", pdcattr->crBrushClr);
	kprintf("[+] pdcattr dwLayout: %x \n", pdcattr->dwLayout);
	if (pdcattr == NULL)
	{
		return CLR_INVALID;
	}

	crOldColor = (COLORREF)pdcattr->ulForegroundClr;
	pdcattr->ulForegroundClr = (ULONG)crColor;

	if (pdcattr->crForegroundClr != crColor)
	{
		//kprintf("[+] They are not the same color\n");
		pdcattr->ulDirty_ |= (DIRTY_TEXT | DIRTY_LINE | DIRTY_FILL);
		pdcattr->crForegroundClr = crColor;
	}

	return crOldColor;
}
int SetBkMode(
	_In_ HDC hdc,
	_In_ int iBkMode)
{
	PDC_ATTR pdcattr;
	INT iOldMode;

	/* Get the DC attribute */
	pdcattr = GdiGetDcAttr(hdc);
	if (pdcattr == NULL)
	{
		return 0;
	}

	iOldMode = pdcattr->lBkMode;
	pdcattr->jBkMode = iBkMode; // Processed
	pdcattr->lBkMode = iBkMode; // Raw

	return iOldMode;
}

bool FrameRect(HDC hDC, CONST RECT *lprc, HBRUSH hbr)
{
	HBRUSH oldbrush = NULL;
	RECT r = *lprc;

	if ((r.right <= r.left) || (r.bottom <= r.top)) return false;
	//if (!(oldbrush = NtGdiSelectBrush(hDC, hbr))) return false;
	oldbrush = NtGdiSelectBrush(hDC, hbr);
	NtGdiPatBlt(hDC, r.left, r.top, 1, r.bottom - r.top, PATCOPY);
	NtGdiPatBlt(hDC, r.right - 1, r.top, 1, r.bottom - r.top, PATCOPY);
	NtGdiPatBlt(hDC, r.left, r.top, r.right - r.left, 1, PATCOPY);
	NtGdiPatBlt(hDC, r.left, r.bottom - 1, r.right - r.left, 1, PATCOPY);

	if (oldbrush)
		NtGdiSelectBrush(hDC, oldbrush);
	return true;
}

bool FillRect(HDC hDC, CONST RECT *lprc, HBRUSH hbr)
{
	BOOL Ret;
	HBRUSH prevhbr = NULL;

	/* Select brush if specified */
	/*if (hbr)
	{
		/* Handle system colors
		/*if (hbr <= (HBRUSH)(COLOR_MENUBAR + 1))
			hbr = GetSysColorBrush(PtrToUlong(hbr) - 1);

		prevhbr = NtGdiSelectBrush(hDC, hbr);
		if (prevhbr == NULL)
			return (INT)FALSE;
	}*/
	prevhbr = NtGdiSelectBrush(hDC, hbr);
	Ret = NtGdiPatBlt(hDC, lprc->left, lprc->top, lprc->right - lprc->left,
		lprc->bottom - lprc->top, PATCOPY);

	/* Select old brush */
	if (prevhbr)
		NtGdiSelectBrush(hDC, prevhbr);

	return Ret;
}
bool FillRect(HDC hDC, int x, int y, int w, int h, HBRUSH hbr)
{
	BOOL Ret;
	HBRUSH prevhbr = NULL;

	RECT lprc = { x, y, x + w, y + h };
	prevhbr = NtGdiSelectBrush(hDC, hbr);
	Ret = NtGdiPatBlt(hDC, lprc.left, lprc.top, lprc.right - lprc.left,
		lprc.bottom - lprc.top, PATCOPY);

	/* Select old brush */
	if (prevhbr)
		NtGdiSelectBrush(hDC, prevhbr);

	return Ret;
}
bool DrawBorderBox(HDC hDC, int x, int y, int w, int h, int thickness, HBRUSH hbr)
{
	BOOL Ret;
	HBRUSH prevhbr = NULL;
	FillRect(hDC, x, y, w, thickness, hbr); //Top horiz line
	FillRect(hDC, x, y, thickness, h, hbr); //Left vertical line
	FillRect(hDC, (x + w), y, thickness, h, hbr); //right vertical line
	FillRect(hDC, x, y + h, w + thickness, thickness, hbr); //bottom horiz line
	return true;
}

HFONT
CreateFontIndirectExW(const ENUMLOGFONTEXDVW *elfexd)
{
	/* Msdn: Note, this function ignores the elfDesignVector member in
			 ENUMLOGFONTEXDV.
	 */
	if (elfexd)
	{
		return NtGdiHfontCreate((PENUMLOGFONTEXDVW)elfexd, 0, 0, 0, NULL);
	}
	else {
		kprintf("[-] CreateFontIndirectExW elfexd is missing");
		return NULL;
	}
}
HFONT
CreateFontIndirectW(
	CONST LOGFONTW      *lplf
)
{
	if (lplf)
	{
		ENUMLOGFONTEXDVW Logfont;

		RtlCopyMemory(&Logfont.elfEnumLogfontEx.elfLogFont, lplf, sizeof(LOGFONTW));
		// Need something other than just cleaning memory here.
		// Guess? Use caller data to determine the rest.
		RtlZeroMemory(&Logfont.elfEnumLogfontEx.elfFullName,
			sizeof(Logfont.elfEnumLogfontEx.elfFullName));
		RtlZeroMemory(&Logfont.elfEnumLogfontEx.elfStyle,
			sizeof(Logfont.elfEnumLogfontEx.elfStyle));
		RtlZeroMemory(&Logfont.elfEnumLogfontEx.elfScript,
			sizeof(Logfont.elfEnumLogfontEx.elfScript));

		Logfont.elfDesignVector.dvNumAxes = 0; // No more than MM_MAX_NUMAXES

		RtlZeroMemory(&Logfont.elfDesignVector, sizeof(DESIGNVECTOR));

		return CreateFontIndirectExW(&Logfont);
	}
	else {
		kprintf("[-] CreateFontIndirectW lplf is missing");
		return NULL;
	}
}

HFONT CreateFontW(int nHeight,
	int nWidth,
	int nEscapement,
	int nOrientation,
	int nWeight,
	DWORD   fnItalic,
	DWORD   fdwUnderline,
	DWORD   fdwStrikeOut,
	DWORD   fdwCharSet,
	DWORD   fdwOutputPrecision,
	DWORD   fdwClipPrecision,
	DWORD   fdwQuality,
	DWORD   fdwPitchAndFamily,
	LPCWSTR lpszFace) {
	LOGFONTW logfont;
	logfont.lfHeight = nHeight;
	logfont.lfWidth = nWidth;
	logfont.lfEscapement = nEscapement;
	logfont.lfOrientation = nOrientation;
	logfont.lfWeight = nWeight;
	logfont.lfItalic = (BYTE)fnItalic;
	logfont.lfUnderline = (BYTE)fdwUnderline;
	logfont.lfStrikeOut = (BYTE)fdwStrikeOut;
	logfont.lfCharSet = (BYTE)fdwCharSet;
	logfont.lfOutPrecision = (BYTE)fdwOutputPrecision;
	logfont.lfClipPrecision = (BYTE)fdwClipPrecision;
	logfont.lfQuality = (BYTE)fdwQuality;
	logfont.lfPitchAndFamily = (BYTE)fdwPitchAndFamily;
	if (NULL != lpszFace)
	{
		int Size = sizeof(logfont.lfFaceName) / sizeof(WCHAR);
		wcsncpy((wchar_t *)logfont.lfFaceName, lpszFace, Size - 1);
		/* Be 101% sure to have '\0' at end of string */
		logfont.lfFaceName[Size - 1] = '\0';
	}
	else
	{
		logfont.lfFaceName[0] = L'\0';
	}
	return CreateFontIndirectW(&logfont);
}

int
GetBkMode(HDC hdc)
{
	PDC_ATTR pdcattr;

	/* Get the DC attribute */
	pdcattr = GdiGetDcAttr(hdc);
	if (pdcattr == NULL)
	{
		/* Don't set LastError here! */
		return 0;
	}

	return pdcattr->lBkMode;
}

COLORREF
SetBkColor(
	_In_ HDC hdc,
	_In_ COLORREF crColor)
{
	PDC_ATTR pdcattr;
	COLORREF crOldColor;

	/* Get the DC attribute */
	pdcattr = GdiGetDcAttr(hdc);
	if (pdcattr == NULL)
	{
		return CLR_INVALID;
	}

	/* Get old color and store the new */
	crOldColor = pdcattr->ulBackgroundClr;
	pdcattr->ulBackgroundClr = crColor;

	if (pdcattr->crBackgroundClr != crColor)
	{
		pdcattr->ulDirty_ |= (DIRTY_BACKGROUND | DIRTY_LINE | DIRTY_FILL);
		pdcattr->crBackgroundClr = crColor;
	}
	return crOldColor;
}

COLORREF
GetBkColor(
	_In_ HDC hdc)
{
	PDC_ATTR pdcattr;

	/* Get the DC attribute */
	pdcattr = GdiGetDcAttr(hdc);
	if (pdcattr == NULL)
	{
		/* Don't set LastError here! */
		return CLR_INVALID;
	}

	return pdcattr->ulBackgroundClr;
}

// Helper function to add hwnd entry to the global array
NTSTATUS AddHwndEntry(HWND hwnd, DWORD dwFlags, HANDLE processId)
{
	NTSTATUS status = STATUS_SUCCESS;
	
	// Acquire the fast mutex
	ExAcquireFastMutex(&g_HwndArrayMutex);
	
	// Check if we have space in the array
	if (g_HwndEntryCount < MAX_HWND_ENTRIES)
	{
		// Find an empty slot or add to the end
		ULONG index = g_HwndEntryCount;
		
		// Initialize the new entry
		g_HwndEntries[index].hwnd = hwnd;
		g_HwndEntries[index].dwFlags = dwFlags;
		g_HwndEntries[index].ProcessId = processId;
		KeQuerySystemTime(&g_HwndEntries[index].Timestamp);
		g_HwndEntries[index].IsValid = TRUE;
		
		g_HwndEntryCount++;
		
		kprintf("[+] Added hwnd %p (dwFlags: 0x%08X) from process %p to array at index %lu\n", 
			hwnd, dwFlags, processId, index);
	}
	else
	{
		kprintf("[!] Hwnd array is full, cannot add hwnd %p\n", hwnd);
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	
	// Release the fast mutex
	ExReleaseFastMutex(&g_HwndArrayMutex);
	
	return status;
}

int64_t __fastcall DetourNtUserSetWindowDisplayAffinity(HWND hwnd, DWORD dwFlags)
{
	kprintf("[+] NtUserSetWindowDisplayAffinity: Sucessfully hooked \n");
	kprintf("[+] NtUserSetWindowDisplayAffinity: hwnd %p \n", hwnd);
	kprintf("[+] NtUserSetWindowDisplayAffinity: dwFlags %i \n", dwFlags);
	
	if(dwFlags != 0x00000000)
	{
		// Get the current process ID
		HANDLE currentProcessId = PsGetCurrentProcessId();
		
		// Save the hwnd and dwFlags values to our global array
		NTSTATUS status = AddHwndEntry(hwnd, dwFlags, currentProcessId);
		if (NT_SUCCESS(status))
		{
			kprintf("[+] Successfully saved hwnd %p (dwFlags: 0x%08X) from process %p\n", 
				hwnd, dwFlags, currentProcessId);
		}
		else
		{
			kprintf("[!] Failed to save hwnd %p: 0x%08X\n", hwnd, status);
		}
	}
	
	// Always set dwFlags to 0 to bypass the protection
	dwFlags = 0x00000000;
	return OriginalNtUserSetWindowDisplayAffinity(hwnd, dwFlags);
}

// Helper function to find hwnd in array and return its dwFlags
DWORD FindHwndInArray(HWND hwnd)
{
	DWORD dwFlags = 0;
	
	// Acquire the fast mutex
	ExAcquireFastMutex(&g_HwndArrayMutex);
	
	// Search through the array for the hwnd
	for (ULONG i = 0; i < g_HwndEntryCount; i++)
	{
		if (g_HwndEntries[i].IsValid && g_HwndEntries[i].hwnd == hwnd)
		{
			dwFlags = g_HwndEntries[i].dwFlags;
			kprintf("[+] Found hwnd %p in array at index %lu, returning dwFlags: 0x%08X\n", 
				hwnd, i, dwFlags);
			break;
		}
	}
	
	// Release the fast mutex
	ExReleaseFastMutex(&g_HwndArrayMutex);
	
	return dwFlags;
}

int64_t __fastcall DetourNtUserGetWindowDisplayAffinity(HWND hwnd)
{
	kprintf("[+] NtUserGetWindowDisplayAffinity: Successfully hooked \n");
	kprintf("[+] NtUserGetWindowDisplayAffinity: hwnd %p \n", hwnd);
	
	// Check if hwnd exists in our array
	DWORD savedDwFlags = FindHwndInArray(hwnd);
	
	if (savedDwFlags != 0)
	{
		// Return the saved dwFlags value instead of calling the original function
		kprintf("[+] Returning saved dwFlags: 0x%08X for hwnd %p\n", savedDwFlags, hwnd);
		return savedDwFlags;
	}
	else
	{
		// hwnd not found in array, call the original function
		kprintf("[+] hwnd %p not found in array, calling original function\n", hwnd);
		return OriginalNtUserGetWindowDisplayAffinity(hwnd);
	}
}

BOOL __fastcall DetourNtGdiBitBlt(HDC hdcDest, INT xDest, INT yDest, INT cxDest, INT cyDest, HDC hdcSrc, INT xSrc, INT ySrc, DWORD dwRop)
{
	// The caller of NtGdiBitBlt must be running at IRQL = PASSIVE_LEVEL and with special kernel APCs enabled.
	if (KeGetCurrentIrql() != PASSIVE_LEVEL) return OriginalNtGdiBitBlt(hdcDest, xDest, yDest, cxDest, cyDest, hdcSrc, xSrc, ySrc, dwRop);
	if (ExGetPreviousMode() == KernelMode) return OriginalNtGdiBitBlt(hdcDest, xDest, yDest, cxDest, cyDest, hdcSrc, xSrc, ySrc, dwRop);
	if (PsGetProcessSessionId(IoGetCurrentProcess()) == 0) return OriginalNtGdiBitBlt(hdcDest, xDest, yDest, cxDest, cyDest, hdcSrc, xSrc, ySrc, dwRop);

	// Get process information
	PEPROCESS current_process = IoGetCurrentProcess();
	char* process_name = PsGetProcessImageFileName(current_process);
	HANDLE currentProcessId = PsGetCurrentProcessId();
	
	// Skip critical system processes that might cause BSOD
	if (process_name != NULL) {
		// Skip Windows system processes
		if (_stricmp(process_name, "csrss.exe") == 0 ||
			_stricmp(process_name, "winlogon.exe") == 0 ||
			_stricmp(process_name, "services.exe") == 0 ||
			_stricmp(process_name, "lsass.exe") == 0 ||
			_stricmp(process_name, "smss.exe") == 0 ||
			_stricmp(process_name, "wininit.exe") == 0 ||
			_stricmp(process_name, "dwm.exe") == 0 ||
			_stricmp(process_name, "explorer.exe") == 0 ||
			_stricmp(process_name, "system") == 0 ||
			_stricmp(process_name, "svchost.exe") == 0 ||
			_stricmp(process_name, "win32k.sys") == 0 ||
			_stricmp(process_name, "win32kbase.sys") == 0 ||
			_stricmp(process_name, "win32kfull.sys") == 0) {
			return OriginalNtGdiBitBlt(hdcDest, xDest, yDest, cxDest, cyDest, hdcSrc, xSrc, ySrc, dwRop);
		}
	}

	kprintf("[+] NtGdiBitBlt: Successfully hooked \n");
	kprintf("[+] NtGdiBitBlt: hdcDest %p \n", hdcDest);
	kprintf("[+] NtGdiBitBlt: xDest %i, yDest %i, cxDest %i, cyDest %i \n", xDest, yDest, cxDest, cyDest);
	kprintf("[+] NtGdiBitBlt: hdcSrc %p \n", hdcSrc);
	kprintf("[+] NtGdiBitBlt: xSrc %i, ySrc %i \n", xSrc, ySrc);
	kprintf("[+] NtGdiBitBlt: dwRop 0x%08X \n", dwRop);
	kprintf("[+] NtGdiBitBlt: Process ID %p \n", currentProcessId);
	kprintf("[+] NtGdiBitBlt: Process %s \n", process_name);
	
	// Call the original function
	return OriginalNtGdiBitBlt(hdcDest, xDest, yDest, cxDest, cyDest, hdcSrc, xSrc, ySrc, dwRop);
}

int64_t __fastcall DetourNtGdiDdDDISubmitCommand(
	D3DKMT_SUBMITCOMMAND* data)
{
	kprintf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ \n");
	kprintf("[+] NtGdiDdDDISubmitCommand: Sucessfully hooked \n");
	kprintf("[+] NtGdiDdDDISubmitCommand: Commands %p \n", data->Commands);
	kprintf("[+] NtGdiDdDDISubmitCommand: CommandLength %i \n", data->CommandLength);
	kprintf("[+] NtGdiDdDDISubmitCommand: SUBMITCOMMANDFLAGS %i %i %i \n", data->Flags.NullRendering, data->Flags.PresentRedirected, data->Flags.Reserved);
	kprintf("[+] NtGdiDdDDISubmitCommand: BroadcastContextCount %i \n", data->BroadcastContextCount);
	kprintf("[+] NtGdiDdDDISubmitCommand: PrivateDriverDataSize %i \n", data->PrivateDriverDataSize);
	kprintf("[+] NtGdiDdDDISubmitCommand: NumPrimaries %i \n", data->NumPrimaries);
	kprintf("[+] NtGdiDdDDISubmitCommand: NumHistoryBuffers %i \n", data->NumHistoryBuffers);
	//get process name
	PEPROCESS current_process = IoGetCurrentProcess();
	int pid = (int)PsGetProcessId(current_process);
	kprintf("[+] NtGdiDdDDISubmitCommand: Process ID %i \n", pid);

	//UNICODE_STRING process_name = getProcessNameByPid(pid);
	//kprintf("[+] NtGdiDdDDISubmitCommand: Process %wZ \n", process_name);
	char* process_name = PsGetProcessImageFileName(current_process);
	kprintf("[+] NtGdiDdDDISubmitCommand: Process %s \n", process_name);

	//Get Context
	HDC hdc = NtUserGetDC(NULL);
	if (hdc == NULL) {
		kprintf("[-] NtGdiDdDDISubmitCommand: The handle is NULL\n");
		return OriginalNtGdiDdDDISubmitCommand(data);
	}
	else {
		kprintf("[+] NtGdiDdDDISubmitCommand: The handle have something %p\n", hdc);
	}

	//Draw shit
	HBRUSH brush = NtGdiCreateSolidBrush(RGB(255, 255, 255), NULL);
	kprintf("[+] NtGdiDdDDISubmitCommand: Brush %p\n", brush);
	HBRUSH brush1 = NtGdiCreateSolidBrush(RGB(0, 255, 0), NULL);
	kprintf("[+] NtGdiDdDDISubmitCommand: Brush %p\n", brush1);

	RECT rect = { 0, 0, 200, 200 };

	bool drawFillRect = FillRect(hdc, &rect, brush);
	if (drawFillRect) {
		kprintf("[+] NtGdiDdDDISubmitCommand: drawFillRect Sucess\n");
	}
	else {
		kprintf("[-] NtGdiDdDDISubmitCommand: drawFillRect oof \n");
	}
	bool borderSucess = DrawBorderBox(hdc, 0, 200, 100, 200, 3, brush1);
	if (borderSucess) {
		kprintf("[+] NtGdiDdDDISubmitCommand: border Sucess \n");
	}
	else {
		kprintf("[-] NtGdiDdDDISubmitCommand: border oof \n");
	}
	//DEFAULT_CHARSET
	HFONT font = CreateFontW(65, 0, 0, 0, FW_NORMAL, 0, 0, 0, CHINESEBIG5_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, L"Arial");//L"Arial"
	kprintf("[+] NtGdiDdDDISubmitCommand: font %p\n", font);
	HFONT oldfont = NtGdiSelectFont(hdc, font);

	bool textSucess = extTextOutW(hdc, 0, 500, 0, NULL, L"习大大万岁万万岁!", 9, 0);
	if (textSucess) {
		kprintf("[+] NtGdiDdDDISubmitCommand: text Sucess\n");
	}
	else {
		kprintf("[-] NtGdiDdDDISubmitCommand: text oof \n");
	}

	//clean up
	bool deleted = NtGdiDeleteObjectApp(brush);
	kprintf("[+] NtGdiDdDDISubmitCommand: deleted %i\n", deleted);
	bool deleted1 = NtGdiDeleteObjectApp(brush1);
	kprintf("[+] NtGdiDdDDISubmitCommand: deleted %i\n", deleted1);
	NtGdiSelectFont(hdc, oldfont);
	bool deleted2 = NtGdiDeleteObjectApp(font);
	kprintf("[+] NtGdiDdDDISubmitCommand: deleted %i\n", deleted2);
	int releaseStatus = NtUserReleaseDC(hdc);
	kprintf("[+] NtGdiDdDDISubmitCommand: ReleaseStatus %i \n", releaseStatus);
	kprintf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ \n");
	return OriginalNtGdiDdDDISubmitCommand(data);
}

void __fastcall ssdt_call_back(unsigned long ssdt_index, void** ssdt_address)
{
	// https://hfiref0x.github.io/
	UNREFERENCED_PARAMETER(ssdt_index);

	if (*ssdt_address == g_NtCreateFile) *ssdt_address = MyNtCreateFile;
	else if (ssdt_index == NtUserSetWindowDisplayAffinity_SYSCALL_INDEX)
	{
		OriginalNtUserSetWindowDisplayAffinity = (NtUserSetWindowDisplayAffinity_t)*ssdt_address;
		*ssdt_address = DetourNtUserSetWindowDisplayAffinity;
	}
	else if (ssdt_index == NtUserGetWindowDisplayAffinity_SYSCALL_INDEX)
	{
		OriginalNtUserGetWindowDisplayAffinity = (NtUserGetWindowDisplayAffinity_t)*ssdt_address;
		*ssdt_address = DetourNtUserGetWindowDisplayAffinity;
	}
	else if (ssdt_index == NtGdiBitBlt_SYSCALL_INDEX)
	{
		OriginalNtGdiBitBlt = (NtGdiBitBlt_t)*ssdt_address;
		*ssdt_address = DetourNtGdiBitBlt;
	}
	//else if (ssdt_index == NTGDIDDDDISUBMMITCOMMAND_SYSCALL_INDEX) 
	//{
	//	OriginalNtGdiDdDDISubmitCommand = (dxgk_submit_command_t)*ssdt_address;
	//	*ssdt_address = DetourNtGdiDdDDISubmitCommand;
	//}
}

VOID DriverUnload(PDRIVER_OBJECT driver)
{
	UNREFERENCED_PARAMETER(driver);

	k_hook::stop();

	// Here we need to make sure that the execution point of the system is no
	// longer in the current driver
	// The 10-second sleep method here can be improved
	LARGE_INTEGER integer{ 0 };
	integer.QuadPart = -10000;
	integer.QuadPart *= 10000;
	KeDelayExecutionThread(KernelMode, FALSE, &integer);
}

EXTERN_C
NTSTATUS
DriverEntry(
	PDRIVER_OBJECT driver,
	PUNICODE_STRING registe)
{
	UNREFERENCED_PARAMETER(driver);
	UNREFERENCED_PARAMETER(registe);

	// Resolve NtCreateFile using MmGetSystemRoutineAddress
	UNICODE_STRING str;
	WCHAR name[256]{ L"NtCreateFile" };
	RtlInitUnicodeString(&str, name);
	g_NtCreateFile = (FNtCreateFile)MmGetSystemRoutineAddress(&str);
	DbgPrintEx(0, 0, "[+] SysCall: NtCreateFile module_export: 0x%p \n", g_NtCreateFile);
	
	driver->DriverUnload = DriverUnload;

	// Initialize the hwnd array mutex
	ExInitializeFastMutex(&g_HwndArrayMutex);
	
	// Initialize hwnd entries array
	RtlZeroMemory(g_HwndEntries, sizeof(g_HwndEntries));
	g_HwndEntryCount = 0;

	// Initialize and start hook
	return k_hook::initialize(ssdt_call_back) && k_hook::start() ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS initSysCalls() {
	if (NtUserGetDC == NULL) {
		NtUserGetDC = (GetDC_t)get_system_module_export(L"win32kbase.sys", "NtUserGetDC");
		if (NtUserGetDC) {
			DbgPrintEx(0, 0, "[+] SysCall: NtUserGetDC module_export: 0x%p \n", NtUserGetDC);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtUserGetDC (win32kbase.sys not loaded)\n");
		}
	} 

	if (NtGdiPatBlt == NULL) {
		NtGdiPatBlt = (PatBlt_t)get_system_module_export(L"win32kfull.sys", "NtGdiPatBlt");
		if (NtGdiPatBlt) {
			DbgPrintEx(0, 0, "[+] SysCall: NtGdiPatBlt module_export: 0x%p \n", NtGdiPatBlt);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtGdiPatBlt (win32kfull.sys not loaded)\n");
		}
	}

	if (NtGdiSelectBrush == NULL) {
		NtGdiSelectBrush = (SelectBrush_t)get_system_module_export(L"win32kbase.sys", "GreSelectBrush");
		if (NtGdiSelectBrush) {
			DbgPrintEx(0, 0, "[+] SysCall: NtGdiSelectBrush module_export: 0x%p \n", NtGdiSelectBrush);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtGdiSelectBrush (win32kbase.sys not loaded)\n");
		}
	} 

	if (NtUserReleaseDC == NULL) {
		NtUserReleaseDC = (ReleaseDC_t)get_system_module_export(L"win32kbase.sys", "NtUserReleaseDC");
		if (NtUserReleaseDC) {
			DbgPrintEx(0, 0, "[+] SysCall: NtUserReleaseDC module_export: 0x%p \n", NtUserReleaseDC);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtUserReleaseDC (win32kbase.sys not loaded)\n");
		}
	} 

	if (NtGdiCreateSolidBrush == NULL) {
		NtGdiCreateSolidBrush = (CreateSolidBrush_t)get_system_module_export(L"win32kfull.sys", "NtGdiCreateSolidBrush");
		if (NtGdiCreateSolidBrush) {
			DbgPrintEx(0, 0, "[+] SysCall: NtGdiCreateSolidBrush module_export: 0x%p \n", NtGdiCreateSolidBrush);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtGdiCreateSolidBrush (win32kfull.sys not loaded)\n");
		}
	} 

	if (NtGdiDeleteObjectApp == NULL) {
		NtGdiDeleteObjectApp = (DeleteObjectApp_t)get_system_module_export(L"win32kbase.sys", "NtGdiDeleteObjectApp");
		if (NtGdiDeleteObjectApp) {
			DbgPrintEx(0, 0, "[+] SysCall: NtGdiDeleteObjectApp module_export: 0x%p \n", NtGdiDeleteObjectApp);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtGdiDeleteObjectApp (win32kbase.sys not loaded)\n");
		}
	} 

	if (NtGdiExtTextOutW == NULL) {
		NtGdiExtTextOutW = (ExtTextOutW_t)get_system_module_export(L"win32kfull.sys", "NtGdiExtTextOutW");
		if (NtGdiExtTextOutW) {
			DbgPrintEx(0, 0, "[+] SysCall: NtGdiExtTextOutW module_export: 0x%p \n", NtGdiExtTextOutW);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtGdiExtTextOutW (win32kfull.sys not loaded)\n");
		}
	}

	if (NtGdiHfontCreate == NULL) {
		NtGdiHfontCreate = (HfontCreate_t)get_system_module_export(L"win32kfull.sys", "hfontCreate");
		if (NtGdiHfontCreate) {
			DbgPrintEx(0, 0, "[+] SysCall: NtGdiHfontCreate module_export: 0x%p \n", NtGdiHfontCreate);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtGdiHfontCreate (win32kfull.sys not loaded)\n");
		}
	}

	if (NtGdiSelectFont == NULL) {
		NtGdiSelectFont = (SelectFont_t)get_system_module_export(L"win32kfull.sys", "NtGdiSelectFont");
		if (NtGdiSelectFont) {
			DbgPrintEx(0, 0, "[+] SysCall: NtGdiSelectFont module_export: 0x%p \n", NtGdiSelectFont);
		} else {
			DbgPrintEx(0, 0, "[-] Failed to resolve NtGdiSelectFont (win32kfull.sys not loaded)\n");
		}
	}

	// Count successful resolutions
	int successCount = 0;
	if (NtUserGetDC) successCount++;
	if (NtGdiPatBlt) successCount++;
	if (NtGdiSelectBrush) successCount++;
	if (NtUserReleaseDC) successCount++;
	if (NtGdiCreateSolidBrush) successCount++;
	if (NtGdiDeleteObjectApp) successCount++;
	if (NtGdiExtTextOutW) successCount++;
	if (NtGdiHfontCreate) successCount++;
	if (NtGdiSelectFont) successCount++;

	//DbgPrintEx(0, 0, "[+] initSysCalls: Successfully resolved %d/9 functions\n", successCount);

	// Always return SUCCESS to prevent driver loading failure
	return STATUS_SUCCESS;
}