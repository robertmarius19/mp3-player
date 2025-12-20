#include <TFT_eSPI.h> 
#include "Arduino.h"
#include "Audio.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#define SD_CS          22   
#define TFT_CS_PIN     15  

#define I2S_DOUT       27
#define I2S_BCLK       26
#define I2S_LRC        25

#define Pin_vol_up     39
#define Pin_vol_down   36
#define Pin_mute       35
#define Pin_previous   15
#define Pin_pause      33
#define Pin_next       2

#define TFT_BL          5 

TFT_eSPI tft = TFT_eSPI(); 
Audio audio;


struct Music_info {
    String name;
    int length;
    int runtime;
    int volume;
    int m;
    int s;
} music_info = {"", 0, 0, 0, 0, 0};

String file_list[30];
int file_num = 0;
int file_index = 0;
bool paused = false;
uint32_t button_time = 0;

void setup() {
    Serial.begin(115200);
    
    pinMode(Pin_vol_up, INPUT);
    pinMode(Pin_vol_down, INPUT);
    pinMode(Pin_mute, INPUT);
    
    pinMode(Pin_previous, INPUT_PULLUP);
    pinMode(Pin_pause, INPUT_PULLUP);
    pinMode(Pin_next, INPUT_PULLUP);
    
    tft.init();
    tft.setRotation(0); 
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);
    
    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, 144);    

    SPI.begin(18, 19, 23, SD_CS); 
    if (!SD.begin(SD_CS)) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("SD FAIL", 10, 100, 4);
        while (1); 
    }
    
    file_num = get_music_list(SD, "/", 0, file_list);
    
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(18); 
    
    if(file_num > 0) {
        open_new_song(file_list[file_index]);
    }
}

void loop() {
    audio.loop();


    if (millis() - button_time > 300) {
        
        // Next
        if (digitalRead(Pin_next) == LOW) {
            if (file_index < file_num - 1) file_index++;
            else file_index = 0;
            open_new_song(file_list[file_index]);
            button_time = millis();
        }
        
        // Prev
        if (digitalRead(Pin_previous) == LOW) {
             if (file_index > 0) file_index--;
             else file_index = file_num - 1;
             open_new_song(file_list[file_index]);
             button_time = millis();
        }

        // Pause
        if (digitalRead(Pin_mute) == LOW) {
            audio.pauseResume();
            paused = !paused;
            button_time = millis();
        }
        
        if (digitalRead(Pin_vol_up) == LOW) {
            if (music_info.volume < 21) music_info.volume++;
            audio.setVolume(music_info.volume);
            button_time = millis();
        }
        if (digitalRead(Pin_vol_down) == LOW) {
            if (music_info.volume > 0) music_info.volume--;
            audio.setVolume(music_info.volume);
            button_time = millis();
        }
    }
}

void open_new_song(String filename) {
    audio.stopSong();
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("PLAYING:", 10, 50, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(filename.substring(1), 10, 80, 2);
    
    pinMode(TFT_CS_PIN, OUTPUT);
    digitalWrite(TFT_CS_PIN, HIGH); 
    
    audio.connecttoFS(SD, filename.c_str());
}

int get_music_list(fs::FS &fs, const char *dirname, uint8_t levels, String wavlist[30]) {
    int i = 0;
    File root = fs.open(dirname);
    if (!root) return 0;
    File file = root.openNextFile();
    while (file && i < 30) {
        if (!file.isDirectory()) {
            String temp = file.name();
            if (temp.endsWith(".mp3")) {
                wavlist[i] = "/" + temp;
                i++;
            }
        }
        file = root.openNextFile();
    }
    return i;
}

// auto-play next
void audio_eof_mp3(const char *info) {
    if (file_index < file_num - 1) file_index++;
    else file_index = 0;
    open_new_song(file_list[file_index]);
}

// Dummy callbacks
void audio_info(const char *info) {}
void audio_id3data(const char *info) {}
