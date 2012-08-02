/* 
 Source taken from Emokit project from https://github.com/qdot/emokit
 - modified to compile (Visual Studio 2010) and run on Windows machine
 - added support for headsets known consumer headsets
 - modified epoc_open() call to epoc_open(d, EPOC_VID, EPOC_PID, 1)
 - modified to gather information about alfa, beta, theta and delta channels by parsing 
   screen of application Emotiv EPOC Brain Activity Map (Even if you don't have access to raw data, you can get some EEG information)
  * Contours->Gray scale mode is assumed
  * screen is captured 20-50 times per second (depends on speed of postprocessing of captured image)
  * resulting image (BMP) is parsed (overall intesity in given channel, intensity in box around sensor) 
  * parsed data are outputed to stderr (together with raw data from sensors captured at HID device)
 - added support for simple Arduino communication/manipulation 
  * running in separate thread
  * sends selected EEG value over serial link to Arduino
  * value can be both from raw data or from screen captured data (~heavily postprocessed)
  * LED connected to PIN 9 will shine accordingly (see code below)

// BEGIN OF ARDUINO CODE
void setup() {
  Serial.begin(9600);
  pinMode(9, OUTPUT);
}
int alpha = 200;
void loop() {
  analogWrite(9, alpha); 
  if (Serial.available() > 0) {
    int readed = Serial.read();
    if (readed < 255) alpha = readed;
  }
}
// END OF ARDUINO CODE

ARCHITECTURE:
 MAIN PROCESS (emokit_c.cpp): Capture and decrypt traffic from epoc HID device at maximum speed (optimally at 128x/sec). 
	  Writes captured data into provided file (second parameter) or to standard output.
	  Use emokit_c.exe headset_type myRaw.data  to write directly into file myRaw.data
	  Use '1> myRaw.data' at command line to redirect into file myRaw.data from standard output 
 OPTIONAL WORKER THREAD (escreencap.cpp): BRAIN MAP SCREEN CAPTURE. Makes screen capture from EPOC Brain Map software. 
	  Prepares statistics from parsed image and store it into global variable (taken later by main thread)
 	  Writes itself nothing into stdout or stderr
 OPTIONAL WORKER THREAD (escreencap.cpp): ARDUINO. Takes current data from global variables 
      (e.g., alphaSum) and let arduino to react correspondingly (e.g., light LED)
	  Writes itself nothing into stdout or stderr
 OPTIONAL WORKER THREAD (escreencap.cpp): KEYBOARD. Takes current key pressed on keyboard. Used for event synchronization in later processing.
	  Writes itself nothing into stdout or stderr
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>
#include <sstream>
#include <iomanip>

#include "libepoc.h"
#include "escreencap.h"

using namespace std;

extern "C" {
	struct epoc_frame frameThreadData;
}
extern screenCaptureData_t screenThreadData;
extern keyboardCaptureData_t keyboardCaptureData; 


// Global structures used also by threads
char arduino_serialPort[10];

int main(int argc, char **argv)
{
	FILE	*outputFile = NULL;
	enum	headset_type type;
	DWORD	readed;
	char	capture_file[MAX_PATH];
	struct  epoc_frame frame;


	epoc_device* d;
	char data[32];

	if (argc < 2)
	{
		fputs("Error: Missing argument\nExpected: emokit_c.exe [consumer|research|special|newcustomer|consumer] [capture_file] [arduino_com_port] \n", stderr);
		return 1;
	}
 
	if(strcmp(argv[1], "research") == 0)
		type = RESEARCH_HEADSET;
	else if(strcmp(argv[1], "consumer1") == 0)
		type = CONSUMER_HEADSET1;
	else if(strcmp(argv[1], "consumer2") == 0)
		type = CONSUMER_HEADSET2;
	else if(strcmp(argv[1], "consumer3") == 0)
		type = CONSUMER_HEADSET3;
	else if(strcmp(argv[1], "special") == 0)
		type = SPECIAL_HEADSET;
	else {
		fputs("Error: Bad headset type argument\nExpected: emokit_c.exe [consumer1|consumer2|consumer3|research|special] [capture_file] [arduino_com_port]\n", stderr);
		fputs("   example: emokit_c.exe consumer1 eegdata.dat \"\\\\.\\COM22\"\n", stderr);
		return 1;
	}

	capture_file[0] = 0;
	// Get first unused filename
	if (argc <= 2) {
		int index = 1;
		sprintf_s(capture_file, sizeof(capture_file), "eegdata_%d.dat", index);
		while (fopen_s(&outputFile, capture_file, "r") == 0) {
			fclose(outputFile);
			index++;
			sprintf_s(capture_file, sizeof(capture_file), "eegdata_%d.dat", index);
		}

		// now we should have free file name
	}
	else {
		strcpy_s(capture_file, sizeof(capture_file), argv[2]);
	}
	fprintf(stderr, "Info: output file '%s' used\n", capture_file);

	if (fopen_s(&outputFile, capture_file, "w+") == 0) {
		// OK - output file opened
		fprintf(stderr, "Info: output file '%s' opened\n", capture_file);
	}
	else {
		// output file cannot be opened, write to stdout
		fputs("Info: no output file given, outputting to stdout\n", stderr);
	}

	arduino_serialPort[0] = 0;
	if (argc > 3) {
		if (strlen(argv[3]) > 0) strcpy_s(arduino_serialPort, sizeof(arduino_serialPort), argv[3]);
	}

	epoc_init(type);

	d = epoc_create();
	//printf("Current epoc devices connected: %d\n", epoc_get_count(d, EPOC_VID, EPOC_PID));
	if(epoc_open(d, EPOC_VID, EPOC_PID, 1) != 0) {
		fputs("Error: Cannot connect to EPOC hid receiver\n", stderr);
		return 1;
	}

	memset(&screenThreadData, 0, sizeof(screenThreadData));

	// Start thread for interaction with Arduino
	if (strlen(arduino_serialPort) > 0) StartThread_Arduino();
	// Start thread for capturing screen from EPOC BrainMap sw
	StartThread_ScreenCapture();

	StartThread_Keyboard();

	FILE* output = NULL;
	// Assign output device - file if opened, stdout otherwise
	output = (outputFile != NULL) ? outputFile : stdout;

	// Wait 2 seconds before gathering and storing the raw data so all threads can catch up
	Sleep(2000);

	while(1) {
		readed = epoc_read_data(d, (uint8_t *) data);
		if (readed > 0) {

			epoc_get_next_frame(&frame, (unsigned char *) data);

    		// OUTPUT TO STDOUT
  			fprintf(output, "%8d %d %d %d %d  %d %d %d %d %d %d %d %d %d %d %d %d %d %d",\
				clock(), keyboardCaptureData.keyState, frame.battery, frame.gyroX, frame.gyroY,\
				frame.F3, frame.FC6, frame.P7,frame.T8, frame.F7, frame.F8, frame.T7, frame.P8,\
				frame.AF4, frame.F4, frame.AF3, frame.O2, frame.O1, frame.FC5);
//				frame.cq.F3, frame.cq.FC6, frame.cq.P7, frame.cq.T8, frame.cq.F7, frame.cq.F8, frame.cq.T7, frame.cq.P8, frame.cq.AF4,\
//				frame.cq.F4, frame.cq.AF3, frame.cq.O2, frame.cq.O1, frame.cq.FC5);

			if (screenThreadData.isValid) {
				for (int i = 0; i < screenThreadData.numScreenChannels; i++) {
					fprintf(output, " %4d", (int) screenThreadData.channelSumResults[i]);
				}
			}
			fprintf(output, "\n"); 

			fflush(output);
		}
	}

	epoc_close(d);
	epoc_delete(d);
	if (outputFile) fclose(outputFile);
	return 0;
} 