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

#define ARRAY(type) typedef struct { type* data; uint64_t size; } type##_Array

ARRAY(HANDLE);

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


// Unfinished
static HANDLE_Array getBatteries()
{
	HANDLE_Array handles = { 0 };

	HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_BATTERY, 
		NULL, NULL,	DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	for (uint64_t i = 0; ; i++)
	{
		SP_DEVICE_INTERFACE_DATA did = { 0 };
		did.cbSize = sizeof(did);

		if (SetupDiEnumDeviceInterfaces(hDevInfo, NULL,
			&GUID_DEVCLASS_BATTERY, i, &did))
		{

			uint32_t reqSize = 0;
			SetupDiGetDeviceInterfaceDetailA(hDevInfo, &did,
				NULL, NULL, &reqSize, NULL);

			PSP_DEVICE_INTERFACE_DETAIL_DATA pdidd = malloc(reqSize);
			if (pdidd)
			{
				pdidd->cbSize = sizeof(pdidd);

				SetupDiGetDeviceInterfaceDetailA(hDevInfo, &did,
					pdidd, reqSize, &reqSize, NULL);

				HANDLE battery = openDevice(pdidd->DevicePath);

				free(pdidd);
			}
		}
		else
			break;
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);

	return handles;
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


static void enableVT()
{
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	uint32_t consoleMode = 0;
	GetConsoleMode(hOut, (DWORD*)&consoleMode);
	consoleMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, consoleMode);
}

static void disableCursor()
{
	printf("\x1B[?25l");
}

static void resetCursor()
{
	printf("\x1B[1;1H");
}

#define ERASE "\x1B[0K"

#define COLOR(id) "\x1B["#id";1m"
#define DEFCOLOR() "\x1B[;0m"

static uint64_t getTimeMS()
{
	uint64_t time = 0;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);

	return time / 10'000;
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


static void draw(sft_window* win, BatteryInfo* battery)
{
	sft_window_fill(win, 0x00000000);

	sft_window_drawTextF(win, 0, 8, 3, 0xFFFFFFFF, "%6.2f%%",
		battery->charge * 100.f / battery->capacity);

	sft_window_drawText(win, "X", 24 * 8, 8, 3, 0xFFFF0000);

	if (battery->isCharging)
		sft_window_drawText(win, "+", 24 * 7, 8, 3, 0xFF00FF00);

	sft_window_display(win);
}


int main(int argc, char** argv)
{
	enableVT();
	disableCursor();

	sft_rect close = { 24 * 8, 8, 24, 24 };

	int offset = 250;

	// TODO: get battery name dynamically
	// for now: 
	// Device Manager -> Batteries -> {battery device} -> Details -> Physical Device Object name
	BatteryInfo battery = initBattery("\\\\.\\GLOBALROOT\\Device\\0000002f");


	sft_init();
	sft_window* win = sft_window_open("", 24 * 9, 32, 
		sft_screenWidth() - (24 * 9) - offset, sft_screenHeight() - 32,
		sft_flag_borderless | sft_flag_noresize | sft_flag_syshide | sft_flag_topmost);

	draw(win, &battery);


	while (sft_window_update(win))
	{
		sft_window_setTopmost(win, true);

		sft_input_update();

		if (sft_colPointRect(close, sft_input_mousePos(win)) && 
			sft_input_clickPressed(sft_click_Left))
			break;

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