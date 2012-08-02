#include "stdafx.h"
#include "escreencap.h"
#include "libepoc.h"

#define CAPTURE_BMP_FREQUENCY	1000
#define NUM_SENSORS	14
#define SENSOR_AREA 20  // total sensor area is square with side with length 2 x SENSOR_AREA  
int sensor_centerX[] = {276, 238, 288, 263, 227, 252, 288, 374, 410, 361, 386, 423, 399, 361};
int sensor_centerY[] = {147, 184, 186, 210, 247, 320, 359, 149, 184, 186, 210, 246, 320, 358};
#define OFFSET_DELTA_X	0
#define OFFSET_DELTA_Y	0
#define OFFSET_THETA_X	487
#define OFFSET_THETA_Y	338
#define OFFSET_ALPHA_X	0
#define OFFSET_ALPHA_Y	338
#define OFFSET_BETA_X	487
#define OFFSET_BETA_Y	338


// Global structure containing readed screen capture data - USED BY OTHER THREADS
screenCaptureData_t screenThreadData;

// Global structure containing data about pressed keys - USED BY OTHER THREADS
keyboardCaptureData_t keyboardCaptureData;

extern "C" {
//	struct epoc_frame frameThreadData;
	char serialPort[10] = {0};
}

#define ARDUINO_DEFAULT_SERIAL_PORT "\\\\.\\COM22"

// Global variables also available for WT_PassDataToCOM thread
float deltaSum = 0;
float thetaSum = 0;
float alphaSum = 0;
float betaSum = 0;


DWORD deltaNumPixels = 0;
DWORD thetaNumPixels = 0;
DWORD alphaNumPixels = 0;
DWORD betaNumPixels = 0;

using namespace std;

int captureCount = 1;
BYTE *lpbitmap = NULL;	// array for screen capture (will be allocated only once)

DWORD GetPixel(BYTE* image, DWORD width, DWORD height, WORD biBitCount, DWORD pixX, DWORD pixY) {
	const DWORD widthSizeInBytes = ((width * biBitCount + 31) / 32) * 4;
	DWORD offset =  (height - 1 - pixY) * widthSizeInBytes + pixX * biBitCount / 8; // pixX * biBitCount / 8 -> more bytes per pixel 
	return *(DWORD*) (image + offset);
}

float SumPixels(BYTE* image, DWORD width, DWORD height, WORD biBitCount, DWORD x1, DWORD y1, DWORD x2, DWORD y2, DWORD* numPixels = NULL) {
	DWORD pixel = 0;
	float sum = 0;
	if (numPixels) *numPixels = 0;
	for (DWORD row = x1; row < x2; row++) {
		for (DWORD col = y1; col < y2; col++) {
			pixel = GetPixel(image, width, height, biBitCount, row, col);
			// assume grayscale - all components are the same - extract only red component - leftmost byte
			BYTE red = *((BYTE*) &pixel);
			if (red != 0xff) {
				sum += red;
				if (numPixels) *numPixels += 1;
			}
		}
	}
	return sum;
}

float SumPixels(BYTE* image, DWORD width, DWORD height, WORD biBitCount, DWORD centerX, DWORD centerY, DWORD areaSide) {
	return SumPixels(image, width, height, biBitCount, centerX - areaSide, centerY - areaSide, centerX + areaSide, centerY + areaSide);
}

BYTE* CaptureAnImageInfinite(HWND hWnd, CString fileNameTempl, int captureFrequency, BOOL bCompSensorSubareas) {
	BYTE* bitmap = NULL;
    HDC hdcScreen;
    HDC hdcWindow;
    HDC hdcMemDC = NULL;
    HBITMAP hbmScreen = NULL;
    BITMAP bmpScreen;
	CString fileName;
	screenCaptureData_t screenData;

	RECT rcClient;
	BITMAPFILEHEADER   bmfHeader;    
	BITMAPINFOHEADER   bi;
	DWORD dwBmpSize = 0;

	if (hWnd) {
		// Retrieve the handle to a display device context for the client 
		// area of the window. 
		hdcScreen = GetDC(NULL);
		hdcWindow = GetDC(hWnd);

		// Create a compatible DC which is used in a BitBlt from the window DC
		hdcMemDC = CreateCompatibleDC(hdcWindow); 

		if(!hdcMemDC) {
			goto done;
		}

		// Get the client area for size calculation
		GetClientRect(hWnd, &rcClient);

		//This is the best stretch mode
		SetStretchBltMode(hdcWindow,HALFTONE);

		//The source DC is the entire screen and the destination DC is the current window (HWND)
		if(!StretchBlt(hdcWindow, 
				   0,0, 
				   rcClient.right, rcClient.bottom, 
				   hdcScreen, 
				   0,0,
				   GetSystemMetrics (SM_CXSCREEN),
				   GetSystemMetrics (SM_CYSCREEN),
				   SRCCOPY))
		{
			goto done;
		}
    
		// Create a compatible bitmap from the Window DC
		hbmScreen = CreateCompatibleBitmap(hdcWindow, rcClient.right-rcClient.left, rcClient.bottom-rcClient.top);
    
		if(!hbmScreen) goto done;

		// Select the compatible bitmap into the compatible memory DC.
		SelectObject(hdcMemDC,hbmScreen);

		// Get the BITMAP from the HBITMAP
		GetObject(hbmScreen,sizeof(BITMAP),&bmpScreen);
     
		bi.biSize = sizeof(BITMAPINFOHEADER);    
		bi.biWidth = bmpScreen.bmWidth;    
		bi.biHeight = bmpScreen.bmHeight;  
		bi.biPlanes = 1;    
		bi.biBitCount = 32;    
		bi.biCompression = BI_RGB;    
		bi.biSizeImage = 0;  
		bi.biXPelsPerMeter = 0;    
		bi.biYPelsPerMeter = 0;    
		bi.biClrUsed = 0;    
		bi.biClrImportant = 0;

		dwBmpSize = ((bmpScreen.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmpScreen.bmHeight;

		// Starting with 32-bit Windows, GlobalAlloc and LocalAlloc are implemented as wrapper functions that 
		// call HeapAlloc using a handle to the process's default heap. Therefore, GlobalAlloc and LocalAlloc 
		// have greater overhead than HeapAlloc.
		if (lpbitmap == NULL) lpbitmap = new BYTE[dwBmpSize];  
	
		// 
		// RUN IN LOOP CAPTURE AND PARSE OF MESSAGES 
		//
		while (1) {
			// Bit block transfer into our compatible memory DC.
			if(!BitBlt(hdcMemDC, 
						0,0, 
						rcClient.right-rcClient.left, rcClient.bottom-rcClient.top, 
						hdcWindow, 
						0,0,
						SRCCOPY))
			{
				goto done;
			}

			// Gets the "bits" from the bitmap and copies them into a buffer 
			// which is pointed to by lpbitmap.
			GetDIBits(hdcWindow, hbmScreen, 0,
				(UINT)bmpScreen.bmHeight,
				lpbitmap,
				(BITMAPINFO *)&bi, DIB_RGB_COLORS);



			DWORD elapsed = clock();

			//
			// Sum whole brain for given frequency
			//
			screenData.deltaSum = SumPixels(lpbitmap,  bi.biWidth, bi.biHeight, bi.biBitCount, 191, 87, 456, 384, &deltaNumPixels);
			screenData.thetaSum = SumPixels(lpbitmap,  bi.biWidth, bi.biHeight, bi.biBitCount, 671, 87, 947, 384, &thetaNumPixels);
			screenData.alphaSum = SumPixels(lpbitmap,  bi.biWidth, bi.biHeight, bi.biBitCount, 191, 423, 456, 717, &alphaNumPixels);
			screenData.betaSum = SumPixels(lpbitmap,  bi.biWidth, bi.biHeight, bi.biBitCount, 677, 423, 947, 717, &betaNumPixels);
		

    		//
			// Sum specific areas around sensors
			//
			if (bCompSensorSubareas) {
				// Delta
				for (int i = 0; i < NUM_SENSORS; i++) {
					screenData.channelSumResults[i] = SumPixels(lpbitmap,  bi.biWidth, bi.biHeight, bi.biBitCount, sensor_centerX[i] + OFFSET_DELTA_X, sensor_centerY[i] + OFFSET_DELTA_Y, SENSOR_AREA);
				}
				// Theta
				for (int i = 0; i < NUM_SENSORS; i++) {
					screenData.channelSumResults[i + 14] = SumPixels(lpbitmap,  bi.biWidth, bi.biHeight, bi.biBitCount, sensor_centerX[i] + OFFSET_THETA_X, sensor_centerY[i] + OFFSET_THETA_Y, SENSOR_AREA);
				}
				// Alfa
				for (int i = 0; i < NUM_SENSORS; i++) {
					screenData.channelSumResults[i + 28] = SumPixels(lpbitmap,  bi.biWidth, bi.biHeight, bi.biBitCount, sensor_centerX[i] + OFFSET_ALPHA_X, sensor_centerY[i] + OFFSET_ALPHA_Y, SENSOR_AREA);
				}
				// Beta
				for (int i = 0; i < NUM_SENSORS; i++) {
					screenData.channelSumResults[i + 42] = SumPixels(lpbitmap,  bi.biWidth, bi.biHeight, bi.biBitCount, sensor_centerX[i] + OFFSET_BETA_X, sensor_centerY[i] + OFFSET_BETA_Y, SENSOR_AREA);
				}
			}

			// Copy temporary data into global structure shared between threads
			screenData.isValid = TRUE;
			memcpy(&screenThreadData, &screenData, sizeof(screenData));



			//
			// SAVE IMAGE DIRECTLY INTO FILE, IF REQUIRED
			//
			if (captureCount % captureFrequency == 1) {
				HANDLE hFile = 0;

				fileName.Format("%s_%d_%d.bmp", fileNameTempl, captureCount, elapsed);
				if (captureCount % captureFrequency == 1) {
					// A file is created, this is where we will save the screen capture.
					hFile = CreateFile(fileName,
						GENERIC_WRITE,
						0,
						NULL,
						CREATE_ALWAYS,
						FILE_ATTRIBUTE_NORMAL, NULL);   
				}    
				// Add the size of the headers to the size of the bitmap to get the total file size
				DWORD dwSizeofDIB = dwBmpSize + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
 
				//Offset to where the actual bitmap bits start.
				bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER); 
    
				//Size of the file
				bmfHeader.bfSize = dwSizeofDIB; 
    
				//bfType must always be BM for Bitmaps
				bmfHeader.bfType = 0x4D42; //BM   
 
				DWORD dwBytesWritten = 0;
				WriteFile(hFile, (LPSTR)&bmfHeader, sizeof(BITMAPFILEHEADER), &dwBytesWritten, NULL);
				WriteFile(hFile, (LPSTR)&bi, sizeof(BITMAPINFOHEADER), &dwBytesWritten, NULL);
				WriteFile(hFile, (LPSTR)lpbitmap, dwBmpSize, &dwBytesWritten, NULL);
	   
				CloseHandle(hFile);
			}

			captureCount++;
		}
	}
	return NULL;

done:
    DeleteObject(hbmScreen);
    DeleteObject(hdcMemDC);
    ReleaseDC(NULL,hdcScreen);
    ReleaseDC(hWnd,hdcWindow);

    return bitmap;
}

UINT WT_PassDataToCOM(LPVOID pParam) {
	HANDLE hSerial;

	// If serial port is not given, use default one
	if (strlen(serialPort) == 0) strcpy_s(serialPort, sizeof(serialPort), ARDUINO_DEFAULT_SERIAL_PORT);
	
	// Connect to arduino port
	hSerial = CreateFile(serialPort,
		GENERIC_READ | GENERIC_WRITE,
		0,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		0);

	if (hSerial==INVALID_HANDLE_VALUE){
		if (GetLastError()==ERROR_FILE_NOT_FOUND){
			//serial port does not exist
			fprintf(stderr, "Serial port '%s' not found", serialPort);
		}
		// other error
		fprintf(stderr, "Unknown error with serial port - is Arduino connected?", serialPort);
		return -1;
	}

	DCB dcbSerialParams = {0};
	dcbSerialParams.DCBlength=sizeof(dcbSerialParams);
	if (!GetCommState(hSerial, &dcbSerialParams)) {
		fprintf(stderr, "Failed to call GetCommState()");
		return -1;
	}
	dcbSerialParams.BaudRate=CBR_9600;
	dcbSerialParams.ByteSize=8;
	dcbSerialParams.StopBits=ONESTOPBIT;
	dcbSerialParams.Parity=NOPARITY;
	if (!SetCommState(hSerial, &dcbSerialParams)){
		fprintf(stderr, "Failed to set SetCommState()");
		return -1;
	}
	COMMTIMEOUTS timeouts={0};
	timeouts.ReadIntervalTimeout=50;
	timeouts.ReadTotalTimeoutConstant=50;
	timeouts.ReadTotalTimeoutMultiplier=10;
	timeouts.WriteTotalTimeoutConstant=50;
	timeouts.WriteTotalTimeoutMultiplier=10;
	if(!SetCommTimeouts(hSerial, &timeouts)){
		//error occureed. Inform user
		fprintf(stderr, "Failed to set SetCommTimeouts()");
		return -1;
	}

	while (1) {
		char szBuff[5] = {0};
		DWORD dwBytesWritten = 0;

		if (alphaNumPixels > 0) {
			int value = (int) alphaSum / alphaNumPixels;
			value += - 48;
			value = (int) (value * 1.5);

			fprintf(stderr, "%d\n", value);

			if (value <= 255) {
				szBuff[0] = (BYTE) value;

				if (!WriteFile(hSerial, szBuff, 1, &dwBytesWritten, NULL)){
				}
			}
		}

		//Sleep(50); // actualize values only 20 times per second

		dwBytesWritten = 0;
	}

	CloseHandle(hSerial);
}

UINT WT_ScreenCapture(LPVOID pParam) {
	HWND hWndMap = FindWindow(NULL, "Emotiv EPOC Brain Activity Map");
	HWND hWndControl = FindWindow(NULL, "EPOC Control Panel"); // not used now - can provide info about sensor quality
	
	// isValid will be set to true once data are extracted from screen capture
	screenThreadData.isValid = 0;
	if (!hWndMap) {
		fprintf(stderr, "Sorry, image from 'Emotiv EPOC Brain Activity Map' cannot be obtained. Reading only from raw HID.\n");
	}
	else {
		BYTE* screen = CaptureAnImageInfinite(hWndMap, "capture_map", CAPTURE_BMP_FREQUENCY, TRUE);
	}
	return 0;
}

UINT WT_Keyboard(LPVOID pParam) {
	SHORT key_a_state = 0;
	SHORT key_l_state = 0;
	keyboardCaptureData.keyState = 0;
	// Infinite loop looking for pressed keys
	while (1) {
		key_a_state = GetKeyState('a');
		key_l_state = GetKeyState('l');

		//keyboardCaptureData.keyState = 0;
		//if (key_a_state & KEY_DOWN) keyboardCaptureData.keyState |= FLAG_a_PRESSED;
		//if (key_l_state & KEY_DOWN) keyboardCaptureData.keyState |= FLAG_l_PRESSED;

		keyboardCaptureData.keyState = key_a_state;
		Sleep(100);
	}

	return 0;
}



void StartThread_Arduino() {
	// Start worker thread for Arduino communication
    AfxBeginThread(WT_PassDataToCOM, NULL, THREAD_PRIORITY_LOWEST); 
}
void StartThread_ScreenCapture() {
	// Start worker thread for screen capture
    AfxBeginThread(WT_ScreenCapture, NULL, THREAD_PRIORITY_LOWEST); 
}

void StartThread_Keyboard() {
	// Start worker thread for screen capture
    AfxBeginThread(WT_Keyboard, NULL, THREAD_PRIORITY_LOWEST); 
}
