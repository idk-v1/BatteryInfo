#define WIN32_LEAN_AND_MEAN
#define NO_STRICT
#define NOMINMAX
#include <Windows.h>
#include <ioapiset.h>
#include <winioctl.h>
#include <Poclass.h>
#include <setupapi.h>
#include <Devguid.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "softdraw/softdraw.h"

static void winErr(const char* label)
{
	DWORD err = GetLastError();
	if (err)
	{
		char* buf = NULL;
		FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 
			NULL, err, 0, &buf, 0, NULL);

		printf("\"%s\" ERROR:(%u) %s\n", label, err, buf);

		LocalFree(buf);
	}
}

static HANDLE openDevice(const char* name)
{
	return CreateFileA(name,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);
}

static uint32_t getBatteryTag(HANDLE hDev)
{
	uint32_t wait = 0;
	uint32_t tag = 0;
	uint32_t numBytes = 0;

	DeviceIoControl(hDev, IOCTL_BATTERY_QUERY_TAG,
		&wait, sizeof(wait),
		&tag, sizeof(tag),
		&numBytes, NULL);

	return tag;
}

static BATTERY_INFORMATION getBatteryInfo(HANDLE hDev)
{

	BATTERY_QUERY_INFORMATION batteryQInfo = { 0 };
	batteryQInfo.InformationLevel = BatteryInformation;
	batteryQInfo.BatteryTag = getBatteryTag(hDev);

	BATTERY_INFORMATION batteryInfo = { 0 };

	uint32_t numBytes = 0;
	BOOL foundDev = DeviceIoControl(hDev, IOCTL_BATTERY_QUERY_INFORMATION,
		&batteryQInfo, sizeof(batteryQInfo),
		&batteryInfo, sizeof(batteryInfo),
		&numBytes, NULL);

	return batteryInfo;
}

static BATTERY_STATUS getBatteryStatus(HANDLE hDev)
{
	BATTERY_WAIT_STATUS wait = { 0 };
	wait.BatteryTag = getBatteryTag(hDev);

	BATTERY_STATUS batteryStatus = { 0 };

	uint32_t numBytes = 0;
	BOOL foundDev = DeviceIoControl(hDev, IOCTL_BATTERY_QUERY_STATUS,
		&wait, sizeof(wait),
		&batteryStatus, sizeof(batteryStatus),
		&numBytes, NULL);

	return batteryStatus;
}

typedef struct BatteryInfo
{
	HANDLE handle;
	uint32_t wear;
	uint32_t capacity;
	uint32_t charge;
	uint8_t isCharging;
} BatteryInfo;


static bool updateBatteryInfo(BatteryInfo* battery)
{
	BATTERY_STATUS batteryStatus = getBatteryStatus(battery->handle);

	bool isCharging = (batteryStatus.PowerState & BATTERY_POWER_ON_LINE) != 0;

	if (battery->charge != batteryStatus.Capacity || battery->isCharging != isCharging)
	{
		battery->isCharging = isCharging;
		battery->charge = batteryStatus.Capacity;
		return true;
	}

	return false;
}

static BatteryInfo initBattery(const char* name)
{
	BatteryInfo battery = { 0 };

	battery.handle = openDevice(name);

	BATTERY_INFORMATION batteryInfo = getBatteryInfo(battery.handle);
	battery.capacity = batteryInfo.FullChargedCapacity;

	battery.wear = batteryInfo.DesignedCapacity;
	battery.wear -= battery.capacity;

	updateBatteryInfo(&battery);

	return battery;
}

static void releaseBattery(BatteryInfo* battery)
{
	CloseHandle(battery->handle);
	memset(battery, 0, sizeof(*battery));
}


static uint32_t getSystrayPos()
{
	// Get taskbar window handle, then get tray window handle
	HWND taskHwnd = FindWindowA("Shell_TrayWnd", NULL);
	HWND trayHwnd = FindWindowExA(taskHwnd, NULL, "TrayNotifyWnd", NULL);

	RECT rect;
	GetWindowRect(trayHwnd, &rect);
	return rect.left;
}

static void draw(sft_window* win, sft_rect switchRect, sft_rect closeRect, BatteryInfo* battery, uint8_t drawMode)
{
	printf("Draw Mode: %u\n", (uint32_t)drawMode);

	sft_window_fill(win, 0x00000000);

	switch (drawMode)
	{
	case 0:
		sft_window_drawTextF(win, 0, closeRect.y, 3,
			battery->isCharging ? 0xFF00FF00 : 0xFFFFFFFF, "%6.2f%%",
			battery->charge * 100.f / battery->capacity);
		break;

	case 1:
		// Needed slightly more space, draw seperately slightly overlapped
		sft_window_drawTextF(win, 8, closeRect.y + 10, 2,
			battery->isCharging ? 0xFF00FF00 : 0xFFFFFFFF, "%10u",
			battery->capacity);
		sft_window_drawTextF(win, 8, closeRect.y - 4, 2,
			battery->isCharging ? 0xFF00FF00 : 0xFFFFFFFF, "%10u",
			battery->charge);
		break;
	}

	sft_window_drawChar(win, 'X', closeRect.x, closeRect.y, 3, 0xFFFF0000);
	
	sft_window_drawChar(win, sft_key_Down, switchRect.x, switchRect.y + 3, 3, 0xFF7F7F7F);
	sft_window_drawChar(win, sft_key_Up, switchRect.x, switchRect.y - 1, 3, 0xFFBFBFBF);

	sft_window_display(win);
}

#define MODINC(var, mod) (var) = ((var) + 1) % (mod)
#define MODDEC(var, mod) (var) = ((var) + (mod) - 1) % (mod)


int main(int argc, char** argv)
{
	// TODO: get battery name dynamically
	// for now: 
	// Device Manager -> Batteries -> {battery device} -> Details -> Physical Device Object name
	BatteryInfo battery = initBattery("\\\\.\\GLOBALROOT\\Device\\0000002f");

	sft_init();

	sft_rect winRect;
	winRect.w = 24 * 9.25;
	winRect.h = 32;
	winRect.x = getSystrayPos() - winRect.w;
	winRect.y = sft_screenHeight() - winRect.h;

	sft_rect closeRect;
	closeRect.w = 24;
	closeRect.h = 24;
	closeRect.x = winRect.w - closeRect.w;
	closeRect.y = 8;

	sft_rect switchRectUp;
	switchRectUp.w = 24;
	switchRectUp.h = 14;
	switchRectUp.x = closeRect.x - switchRectUp.w;
	switchRectUp.y = 4;

	sft_rect switchRectDown;
	switchRectDown.w = 24;
	switchRectDown.h = 14;
	switchRectDown.x = closeRect.x - switchRectDown.w;
	switchRectDown.y = switchRectUp.y + switchRectUp.h;

	uint8_t drawMode = 0;

	bool hoverSwitchUp = false;
	bool hoverSwitchDown = false;
	bool hoverClose = false;


	sft_window* win = sft_window_open("",
		winRect.w, winRect.h, winRect.x, winRect.y,
		sft_flag_borderless | sft_flag_noresize | sft_flag_syshide | sft_flag_topmost);

	draw(win, switchRectUp, closeRect, &battery, drawMode);


	while (sft_window_update(win))
	{
		sft_input_update();

		winRect.x = getSystrayPos() - winRect.w;
		winRect.y = sft_screenHeight() - winRect.h;

		sft_window_setTopmost(win, true);
		sft_window_setPos(win, winRect.x, winRect.y);



		if (hoverClose && sft_input_clickReleased(sft_click_Left))
			break;
		hoverClose = sft_colPointRect(closeRect, sft_input_mousePos(win)) &&
			sft_input_clickState(sft_click_Left);

		if (hoverSwitchUp && sft_input_clickReleased(sft_click_Left))
		{
			MODINC(drawMode, 3);
			draw(win, switchRectUp, closeRect, &battery, drawMode);
		}
		hoverSwitchUp = sft_colPointRect(switchRectUp, sft_input_mousePos(win)) &&
			sft_input_clickState(sft_click_Left);

		if (hoverSwitchDown && sft_input_clickReleased(sft_click_Left))
		{
			MODDEC(drawMode, 3);
			draw(win, switchRectUp, closeRect, &battery, drawMode);
		}
		hoverSwitchDown = sft_colPointRect(switchRectDown, sft_input_mousePos(win)) &&
			sft_input_clickState(sft_click_Left);



		if (updateBatteryInfo(&battery))
			draw(win, switchRectUp, closeRect, &battery, drawMode);

		sft_sleep(50);
	}


	releaseBattery(&battery);

	sft_window_close(win);
	sft_shutdown();

	return 0;
}