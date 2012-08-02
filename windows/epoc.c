#include "mcrypt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "libepoc.h"

#define KEYSIZE 16 /* 128 bits == 16 bytes */

const unsigned char CONSUMERKEY1[KEYSIZE] =		{0x31,0x00,0x35,0x54,0x38,0x10,0x37,0x42,0x31,0x00,0x35,0x48,0x38,0x00,0x37,0x50};
const unsigned char CONSUMERKEY2[KEYSIZE] =		{0x4D,0x00,0x47,0x54,0x38,0x10,0x31,0x42,0x4D,0x00,0x47,0x48,0x38,0x00,0x31,0x50};
const unsigned char CONSUMERKEY3[KEYSIZE] =		{0x4E,0x00,0x53,0x54,0x38,0x10,0x37,0x42,0x4E,0x00,0x53,0x48,0x38,0x00,0x37,0x50};
const unsigned char RESEARCHKEY[KEYSIZE] =		{0x31,0x00,0x39,0x54,0x38,0x10,0x37,0x42,0x31,0x00,0x39,0x48,0x38,0x00,0x37,0x50};
const unsigned char SPECIALKEY[KEYSIZE]  =		{0x31,0x00,0x35,0x48,0x31,0x00,0x35,0x54,0x38,0x10,0x37,0x42,0x38,0x00,0x37,0x50};


const unsigned char F3_MASK[14] = {10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7}; 
const unsigned char FC6_MASK[14] = {214, 215, 200, 201, 202, 203, 204, 205, 206, 207, 192, 193, 194, 195};
const unsigned char P7_MASK[14] = {84, 85, 86, 87, 72, 73, 74, 75, 76, 77, 78, 79, 64, 65};
const unsigned char T8_MASK[14] = {160, 161, 162, 163, 164, 165, 166, 167, 152, 153, 154, 155, 156, 157};
const unsigned char F7_MASK[14] = {48, 49, 50, 51, 52, 53, 54, 55, 40, 41, 42, 43, 44, 45};
const unsigned char F8_MASK[14] = {178, 179, 180, 181, 182, 183, 168, 169, 170, 171, 172, 173, 174, 175};
const unsigned char T7_MASK[14] = {66, 67, 68, 69, 70, 71, 56, 57, 58, 59, 60, 61, 62, 63};
const unsigned char P8_MASK[14] = {158, 159, 144, 145, 146, 147, 148, 149, 150, 151, 136, 137, 138, 139};
const unsigned char AF4_MASK[14] = {196, 197, 198, 199, 184, 185, 186, 187, 188, 189, 190, 191, 176, 177};
const unsigned char F4_MASK[14] = {216, 217, 218, 219, 220, 221, 222, 223, 208, 209, 210, 211, 212, 213};
const unsigned char AF3_MASK[14] = {46, 47, 32, 33, 34, 35, 36, 37, 38, 39, 24, 25, 26, 27};
const unsigned char O2_MASK[14] = {140, 141, 142, 143, 128, 129, 130, 131, 132, 133, 134, 135, 120, 121};
const unsigned char O1_MASK[14] = {102, 103, 88, 89, 90, 91, 92, 93, 94, 95, 80, 81, 82, 83};
const unsigned char FC5_MASK[14] = {28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9};

const unsigned char FRAME_LENGTH = 32;

MCRYPT td;
unsigned char key[KEYSIZE];
unsigned char *block_buffer;
int blocksize;

unsigned char frame[32];

// Global strcture containing readed raw data - USED BY THREADS
struct epoc_frame frameThreadData;

int epoc_init(enum headset_type type) {
    if(type == RESEARCH_HEADSET)
        memcpy(key, RESEARCHKEY, KEYSIZE);
    else if(type == CONSUMER_HEADSET1)
        memcpy(key, CONSUMERKEY1, KEYSIZE);
    else if(type == SPECIAL_HEADSET)
        memcpy(key, SPECIALKEY, KEYSIZE);
    else if(type == CONSUMER_HEADSET2)
        memcpy(key, CONSUMERKEY2, KEYSIZE);
    else if(type == CONSUMER_HEADSET3)
        memcpy(key, CONSUMERKEY3, KEYSIZE);
    
    //libmcrypt initialization
    td = mcrypt_module_open(MCRYPT_RIJNDAEL_128, NULL, MCRYPT_ECB, NULL);
    blocksize = mcrypt_enc_get_block_size(td); //should return a 16bits blocksize
    
    block_buffer = (unsigned char*) malloc(blocksize);

    mcrypt_generic_init( td, key, KEYSIZE, NULL);
	return 0;
}

int epoc_deinit() {
    mcrypt_generic_deinit (td);
    mcrypt_module_close(td);
	return 0;
}

int get_level(unsigned char frame[32], const unsigned char bits[14]) {
    char i;
    char b,o;
    int level = 0;
    
    for (i = 13; i >= 0; --i) {
        level <<= 1;
        b = (bits[i] / 8) + 1;
        o = bits[i] % 8;
        
        level |= (frame[b] >> o) & 1;
    }
    return level;
}

int epoc_get_next_raw(unsigned char frame[32], unsigned char raw_frame[32]) {
    //Two blocks of 16 bytes must be read.
    if (memcpy (block_buffer, raw_frame, blocksize)) {
        mdecrypt_generic (td, block_buffer, blocksize);
        memcpy(frame, block_buffer, 16);
    }
    else {
        return -1;
    }
    
    if (memcpy(block_buffer, raw_frame + blocksize, blocksize)) {
        mdecrypt_generic (td, block_buffer, blocksize);
        memcpy(frame + 16, block_buffer, 16);
    }
    else {
        return -1;
    }
    return 0;
}

int epoc_get_next_frame(struct epoc_frame* frame, unsigned char* raw_data) {

	int i = 0;
	unsigned char raw_frame[32];

	memset(raw_frame, 0, 32);
	
    epoc_get_next_raw(raw_frame, raw_data);

    frame->F3 = get_level(raw_frame, F3_MASK);
    frame->FC6 = get_level(raw_frame, FC6_MASK);
    frame->P7 = get_level(raw_frame, P7_MASK);
    frame->T8 = get_level(raw_frame, T8_MASK);
    frame->F7 = get_level(raw_frame, F7_MASK);
    frame->F8 = get_level(raw_frame, F8_MASK);
    frame->T7 = get_level(raw_frame, T7_MASK);
    frame->P8 = get_level(raw_frame, P8_MASK);
    frame->AF4 = get_level(raw_frame, AF4_MASK);
    frame->F4 = get_level(raw_frame, F4_MASK);
    frame->AF3 = get_level(raw_frame, AF3_MASK);
    frame->O2 = get_level(raw_frame, O2_MASK);
    frame->O1 = get_level(raw_frame, O1_MASK);
    frame->FC5 = get_level(raw_frame, FC5_MASK);
    
    //TODO!
    frame->gyroX = raw_frame[29] - 102;
    frame->gyroY = raw_frame[30] - 104;
    
    frame->battery = 0;

	memcpy(&frameThreadData, frame, sizeof(struct epoc_frame));

	return 0;
}

void epoc_print_key_bits(const BYTE* key, const char* keyName) {
	unsigned int keyByte1 = 0;
	int i = 0;

	printf("%s: ", keyName);
	for (keyByte1 = 0; keyByte1 < KEYSIZE; keyByte1++) {
		for (i = 0; i < 8; i++) printf("%d", (key[keyByte1] & (int) pow((double)2,i)) ? 1 : 0);
		printf(" ");	
	}
}

int epoc_print_keys() {
	epoc_print_key_bits(CONSUMERKEY1, "CONSUMERKEY1");
	epoc_print_key_bits(CONSUMERKEY2, "CONSUMERKEY2");
	epoc_print_key_bits(CONSUMERKEY3, "CONSUMERKEY3");
	epoc_print_key_bits(RESEARCHKEY, "RESEARCHKEY");
	epoc_print_key_bits(SPECIALKEY, "SPECIALKEY");

	return 0;
}
