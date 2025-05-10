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

static void draw(sft_window* win, BatteryInfo* battery)
{
	sft_window_fill(win, 0x00000000);

	sft_window_drawTextF(win, 0, 8, 3, 
		battery->isCharging ? 0xFF00FF00 : 0xFFFFFFFF, "%6.2f%%",
		battery->charge * 100.f / battery->capacity);

	sft_window_drawText(win, "X", 24 * 7.25, 8, 3, 0xFFFF0000);

	sft_window_display(win);
}


int main(int argc, char** argv)
{
	sft_rect close = { 24 * 7.25, 8, 24, 24 };

	// TODO: get battery name dynamically
	// for now: 
	// Device Manager -> Batteries -> {battery device} -> Details -> Physical Device Object name
	BatteryInfo battery = initBattery("\\\\.\\GLOBALROOT\\Device\\0000002f");


	sft_init();
	sft_window* win = sft_window_open("", 24 * 8.25, 32, 
		getSystrayPos() - (24 * 8.25),
		sft_screenHeight() - 32,
		sft_flag_borderless | sft_flag_noresize | sft_flag_syshide | sft_flag_topmost);

	draw(win, &battery);

	bool hoverClose = false;

	while (sft_window_update(win))
	{
		sft_input_update();

		if (hoverClose && sft_input_clickReleased(sft_click_Left))
			break;

		hoverClose = sft_colPointRect(close, sft_input_mousePos(win)) &&
			sft_input_clickState(sft_click_Left);


		sft_window_setTopmost(win, true);
		sft_window_setPos(win, getSystrayPos() - (24 * 8.25), sft_screenHeight() - 32);

		if (updateBatteryInfo(&battery))
		{
			draw(win, &battery);
		}

		sft_sleep(50);
	}
	sft_window_close(win);

	releaseBattery(&battery);

	sft_shutdown();

	return 0;
}