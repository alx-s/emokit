#ifndef ESCREENCAP_H
#define ESCREENCAP_H

typedef struct {
	int  isValid;
	float deltaSum;
	float thetaSum;
	float alphaSum;
	float betaSum;
	static const int numScreenChannels = 56;
	float channelSumResults[numScreenChannels];
} screenCaptureData_t;


const SHORT KEY_DOWN = (SHORT) 0x8000;
const int FLAG_a_PRESSED = 0x00000001;
const int FLAG_l_PRESSED = 0x00000002;

typedef struct {
	int keyState;
} keyboardCaptureData_t;

#ifndef __cplusplus
extern "C" {
#endif
	void StartThread_Arduino();
	void StartThread_ScreenCapture();
	void StartThread_Keyboard();

#ifndef __cplusplus
}
#endif

#endif