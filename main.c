#define WIN32_LEAN_AND_MEAN
#define NO_STRICT
#define NOMINMAX
#include <Windows.h>
#include <ioapiset.h>
#include <winioctl.h>
#include <Poclass.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>


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

static void releaseDevice(HANDLE* hDev)
{
	CloseHandle(*hDev);
	*hDev = NULL;
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

#define CURSOR(x, y) "\x1B["#y";"#x"H"

#define COLOR(id) "\x1B["#id";1m"
#define DEFCOLOR() "\x1B[;0m"

static void printBatteryInfo(BATTERY_INFORMATION batteryInfo, BATTERY_STATUS batteryStatus,
	uint64_t deltaTime, int32_t deltaCharge)
{
	printf(CURSOR(1, 1));

	printf("Power State:  %s\n",
		(batteryStatus.PowerState & BATTERY_POWER_ON_LINE) ?
		(COLOR(32) "Charging   " DEFCOLOR()) :
		(COLOR(31) "Discharging" DEFCOLOR()));

	printf("Battery Type: %s\n",
		(batteryInfo.Capabilities & BATTERY_IS_SHORT_TERM) ?
		(COLOR(31) "Fail-Safe" DEFCOLOR()) :
		(COLOR(32) "Battery  " DEFCOLOR()));

	printf("\n");


	printf("Wear:         %8.2f%%\n",
		100.f * batteryInfo.FullChargedCapacity / (float)batteryInfo.DesignedCapacity - 100.f);

	printf("New:          %8u\n", 
		batteryInfo.DesignedCapacity);

	printf("Full:         %8u\n", 
		batteryInfo.FullChargedCapacity);

	printf("Charge:       %8u\n",
		batteryStatus.Capacity);

	printf("\n");


	printf("Percent:       %8.3f%%\n", 
		(float)batteryStatus.Capacity / batteryInfo.FullChargedCapacity * 100.f);

	printf("               %+8.3f%%/sec      \n", 
		(float)deltaCharge / (float)batteryInfo.FullChargedCapacity * 100.f / (float)(deltaTime / 1000.f));

	printf("Delta Charge: %+5d\n",
		deltaCharge);

	printf("Delta Time:   %8.2f sec\n",
		deltaTime / 1000.f);
}


static uint64_t timeDiff(uint64_t* last)
{
	uint64_t time = 0;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);

	uint64_t diff = time - *last;
	*last = time;

	return diff;
}


int main(int argc, char** argv)
{
	enableVT();
	disableCursor();

	HANDLE hDev = openDevice("\\\\.\\GLOBALROOT\\Device\\0000002f");
	winErr("Open Device");

	uint64_t lastTime;
	timeDiff(&lastTime);
	uint64_t deltaTime = 1;

	uint32_t lastCharge = getBatteryStatus(hDev).Capacity;
	int32_t deltaCharge = 0;

	while (!(GetKeyState(VK_ESCAPE) & 0x8000))
	{
		BATTERY_INFORMATION batteryInfo = getBatteryInfo(hDev);
		winErr("Get Battery Info");

		BATTERY_STATUS batteryStatus = getBatteryStatus(hDev);
		winErr("Get Battery Status");

		if (lastCharge != batteryStatus.Capacity)
		{
			deltaTime = timeDiff(&lastTime);
			deltaCharge = (int32_t)batteryStatus.Capacity - (int32_t)lastCharge;
			lastCharge = batteryStatus.Capacity;
		}

		printBatteryInfo(batteryInfo, batteryStatus, deltaTime / 10'000, deltaCharge);


		Sleep(50);
	}

	releaseDevice(&hDev);

	return 0;
}