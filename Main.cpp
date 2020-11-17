#pragma comment(lib,"vfw32.lib")

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

#include <Windows.h>
#include <Vfw.h>

#define not_a_reinterpret_cast reinterpret_cast

HWND window;
HWND capture;
HHOOK keyboardHook;

bool captureWindowState = 1;
bool recordingState = 0;

struct CamDevice {
	std::string name;
	std::string description;
};

auto askDevice(const std::vector<CamDevice>& devices) {
	if (devices.size() == 1) return 0;
	std::string buffer;
	do {
		std::cout << "Enter device index to use [1-" << devices.size() << "]: ";
		try {
			buffer.clear();
			std::getline(std::cin, buffer);
			if (auto value = std::stoi(buffer); value > 0 && value <= devices.size())
				return value - 1;
		} catch (...) { }
		std::cout << "E :: Invalid value\n";
	} while (true);
}

auto getCamDevices() {
	std::vector<CamDevice> devices;
	auto nameBuffer = std::make_unique<char[]>(256);
	auto descriptionBuffer = std::make_unique<char[]>(256);
	for (int i = 0; i < 10; i++) {
		if (capGetDriverDescriptionA(i, nameBuffer.get(), 255, descriptionBuffer.get(), 255)) {
			devices.push_back({
				std::string(nameBuffer.get()),
				std::string(descriptionBuffer.get())
			});
		}
	}
	return std::move(devices);
}

auto getFileStamp() {
	return std::to_wstring(time(nullptr));
}

auto photoCapture() {
	if (recordingState) {
		capGrabFrameNoStop(capture);
	}
	else {
		capCaptureSingleFrameOpen(capture);
		capCaptureSingleFrame(capture);
		capCaptureSingleFrameClose(capture);
	}

	std::wstring fileName(std::wstring(L"capture-") + getFileStamp() + std::wstring(L".bmp"));
	capFileSaveDIB(capture, fileName.c_str());
	return fileName;
}

auto recordStart() {
	CAPTUREPARMS params;
	capCaptureGetSetup(capture, &params, sizeof(params));

	params.fMakeUserHitOKToCapture = false;
	params.fYield = true;
	params.dwAudioBufferSize = 64;
	params.fCaptureAudio = true;
	params.fAbortLeftMouse = false;
	params.fAbortRightMouse = false;
	params.dwRequestMicroSecPerFrame = 15000;
	capCaptureSetSetup(capture, &params, sizeof(params));

	std::wstring fileName(std::wstring(L"record-") + getFileStamp() + std::wstring(L".avi"));
	capFileSetCaptureFile(capture, fileName.c_str());
	capCaptureSequence(capture);
	recordingState = 1;
	return fileName;
}

auto recordStop() {
	capCaptureAbort(capture);
	capFileSetCaptureFile(capture, nullptr);
	recordingState = 0;
}

LRESULT __stdcall onKeyboard(int a, WPARAM b, LPARAM args) {
	auto* event = not_a_reinterpret_cast<KBDLLHOOKSTRUCT*>(args);
	DWORD code = ((event->scanCode & 0xff) << 8) | (event->vkCode & 0xff);
	
	if (!(event->flags & 0x80)) {
		if (code == 0x011b) {
			DestroyWindow(capture);
			TerminateProcess(GetCurrentProcess(), 0);
			return -1;
		}
		if (code == 0x1950) {
			std::wcout << "Capture saved as " << photoCapture() << "\n";
			return -1;
		}
		if (code == 0x2e43) {
			if (recordingState) {
				recordStop();
				std::wcout << "Recording is finished" << std::endl;;
			}
			else {
				auto fileName = recordStart();
				std::wcout << "Recording saved as " << fileName << "\n";
			}
			return -1;
		}
		if (code == 0x2348) {
			ShowWindow(capture, captureWindowState ? SW_HIDE : SW_NORMAL);
			ShowWindow(window, captureWindowState ? SW_HIDE : SW_NORMAL);

			captureWindowState = !captureWindowState;
			return -1;
		}
	}
	return CallNextHookEx(keyboardHook, a, b, args);
}

int main() {
	const auto devices = getCamDevices();
	if (devices.empty()) {
		std::cerr << "No camera devices found.\n";
		return 1;
	}

	std::cout << "Camera devices: \n";
	for (int i = 0; i < devices.size(); i++) {
		std::cout << (i + 1) << " : " << devices[i].name << "\n"
			<< "\t" << devices[i].description << "\n";
	}

	const auto index = askDevice(devices);
	std::cout << "\n\nWorking with " << devices[index].name << "\n\n";

	window = GetForegroundWindow();
	capture = capCreateCaptureWindowA("capture", WS_VISIBLE, -1, -1, 640, 480, window, 0);
	if (!capture) {
		std::cout << "E :: Failed to create capture windows : " << GetLastError() << "\n";
		return 1;
	}

	auto start = time(nullptr);
	std::cout << "Connecting to driver...\n";
	while (!capDriverConnect(capture, index)) {
		if (time(nullptr) - start > 20) {
			std::cout << "E :: Driver connection timed out : 20 seconds\n";
			DestroyWindow(capture);
			return 1;
		}
	}
	std::cout << "Connection established\n";

	keyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, onKeyboard, GetModuleHandle(nullptr), 0);

	capPreviewScale(capture, true);	
	capPreviewRate(capture, 30);		
	capPreview(capture, true);

	MSG message = { 0 };
	while (GetMessage(&message, nullptr, 0, 0)) {
		TranslateMessage(&message);
		DispatchMessage(&message);
	}
	capDriverDisconnect(capture);
	DestroyWindow(capture);
}