#include <TFT_eSPI.h>
#include "Arduino.h"
#include "Audio.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"

// sd_card-pins
#define SD_CS          22
#define SPI_MOSI       23
#define SPI_MISO       19
#define SPI_SCK        18

//audio-pins
#define I2S_DOUT       27
#define I2S_BCLK       26
#define I2S_LRC        25

//buttons
#define Pin_vol_up     39
#define Pin_vol_down   36
#define Pin_mute       35
#define Pin_previous   15
#define Pin_prev       33
#define Pin_next       2

#define TFT_BL         5

TFT_eSPI tft = TFT_eSPI();
Audio audio;

const int pwmFreq = 5000;
const int pwmResolution = 8;

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
uint32_t run_time = 0;
uint32_t button_time = 0;

void open_new_song(String filename);
void tft_music();
void drawStart();
void drawVolume();
void print_song_time();
int get_music_list(fs::FS &fs, const char *dirname, uint8_t levels, String wavlist[30]);

void setup() {
    Serial.begin(115200);
    Serial.println("Starting MP3 Player...");
    
    // Setup all button pins
    pinMode(Pin_vol_up, INPUT_PULLUP);
    pinMode(Pin_vol_down, INPUT_PULLUP);
    pinMode(Pin_mute, INPUT_PULLUP);
    pinMode(Pin_previous, INPUT_PULLUP);
    pinMode(Pin_pause, INPUT_PULLUP);
    pinMode(Pin_next, INPUT_PULLUP);

    // setup screen
    tft.init();
    tft.setRotation(0);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    // setup backlight
    ledcAttach(TFT_BL, pwmFreq, pwmResolution);
    ledcWrite(TFT_BL, 144);

    // Setup sd_card 
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    
    delay(100);
    
    if (!SD.begin(SD_CS, SPI)) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("SD CARD ERROR", 10, 100, 4);
        Serial.println("SD Mount Failed!");
        while (1) {
            delay(1000);
        }
    }
    
    Serial.println("SD Card initialized successfully");

    // Read music files from SD card
    file_num = get_music_list(SD, "/", 0, file_list);
    Serial.print("Music files found: ");
    Serial.println(file_num);

    // Display playlist on screen
    tft.setTextColor(0xBDD7, TFT_BLACK);
    int y = 0;
    for (int i = 0; i < file_num; i++) {
        if (y < 120) {
            String displayName = file_list[i].substring(1, file_list[i].length() - 4);
            tft.drawString(displayName, 18, 118 + y, 2);
            Serial.println(file_list[i]);
            y += 15;
        }
    }

    // i2s audio configuration
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(18); // start at medium volume (0-21)
    
    delay(100);


    drawStart();
    drawVolume();

    if (file_num > 0) {
        open_new_song(file_list[file_index]);
        print_song_time();
        Serial.println("Starting playback...");
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("No MP3/WAV Found", 10, 50, 2);
        Serial.println("No music files found on SD card!");
    }
}

void loop() {

    audio.loop();

    if (millis() - run_time > 1000) {
        run_time = millis();
        print_song_time();
        tft_music();
    }


    if (millis() - button_time > 300) {
        
        // next_button
        if (digitalRead(Pin_next) == LOW) {
            Serial.println("Next button pressed");
            if (file_index < file_num - 1)
                file_index++;
            else
                file_index = 0;
            
            drawStart();
            open_new_song(file_list[file_index]);
            print_song_time();
            button_time = millis();
        }
        
        // prev_button
        if (digitalRead(Pin_prev) == LOW) {
            Serial.println("Previous button pressed");
            if (file_index > 0)
                file_index--;
            else
                file_index = file_num - 1;
            
            drawStart();
            open_new_song(file_list[file_index]);
            print_song_time();
            button_time = millis();
        }
        
        // pause_button
        if (digitalRead(Pin_mute) == LOW) {
            Serial.println("Pause/Resume button pressed");
            audio.pauseResume();
            paused = !paused;
            
            if (paused) {
                tft.fillRect(6, 45, 100, 20, TFT_BLACK);
                tft.setTextColor(TFT_RED, TFT_BLACK);
                tft.drawString("PAUSED", 6, 45, 2);
            } else {
                tft.fillRect(6, 45, 100, 20, TFT_BLACK);
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
                tft.drawString("PLAYING", 6, 45, 2);
            }
            button_time = millis();
        }
        
        // vol_up
        if (digitalRead(Pin_vol_up) == LOW) {
            Serial.println("Volume Up pressed");
            if (music_info.volume < 21)
                music_info.volume++;
            audio.setVolume(music_info.volume);
            drawVolume();
            button_time = millis();
        }
        
        // vol_down
        if (digitalRead(Pin_vol_down) == LOW) {
            Serial.println("Volume Down pressed");
            if (music_info.volume > 0)
                music_info.volume--;
            audio.setVolume(music_info.volume);
            drawVolume();
            button_time = millis();
        }
    }
}

// --- HELPER FUNCTIONS ---

void open_new_song(String filename) {

    audio.stopSong();
    delay(50);
    
    String cleanName = filename.substring(1, filename.lastIndexOf("."));
    music_info.name = cleanName;
    

    bool success = audio.connecttoFS(SD, filename.c_str());
    
    if (success) {
        Serial.print("Playing: ");
        Serial.println(filename);
        
        music_info.runtime = 0;
        music_info.length = audio.getAudioFileDuration();
        music_info.volume = audio.getVolume();
        music_info.m = music_info.length / 60;
        music_info.s = music_info.length % 60;
        paused = false;
    } else {
        Serial.print("Failed to open: ");
        Serial.println(filename);
    }
}

void tft_music() {
    if (music_info.length == 0) return;
    
    // progress bar
    int line = map(music_info.runtime, 0, music_info.length, 10, 210);
    
    tft.fillRect(0, 87, 240, 9, TFT_BLACK);
    tft.drawLine(10, 92, 210, 92, 0xBDD7);
    tft.drawLine(10, 90, 10, 94, 0xBDD7);
    tft.drawLine(210, 90, 210, 94, 0xBDD7);
    tft.fillRect(line, 88, 6, 8, TFT_GREEN);


    int tempSeconds = music_info.runtime % 60;
    int tempMinutes = music_info.runtime / 60;
    
    String ts = (tempSeconds < 10) ? "0" + String(tempSeconds) : String(tempSeconds);
    String tm = (tempMinutes < 10) ? "0" + String(tempMinutes) : String(tempMinutes);
    
    // display song name
    tft.setTextColor(0xFC3D, TFT_BLACK);
    tft.drawString(music_info.name, 4, 66, 2);
    
    // display total length
    String total_s = (music_info.s < 10) ? "0" + String(music_info.s) : String(music_info.s);
    String total_m = String(music_info.m);
    tft.drawString(total_m + ":" + total_s, 190, 100, 2);
    
    // display current time
    tft.setTextColor(0xBDD7, TFT_BLACK);
    tft.drawString(tm + ":" + ts, 5, 20, 4);
}

void drawVolume() {
    int v = map(music_info.volume, 0, 21, 0, 8);
    tft.fillRect(180, 18, 54, 24, TFT_BLACK);
    
    for (int i = 0; i < v; i++) {
        tft.drawLine(198 + i * 4, 40, 198 + i * 4, 37 - (i * 2), 0xBDD7);
    }
}

void drawStart() {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("TIME", 6, 0, 2);
    tft.drawString("VOLUME", 182, 0, 2);
    tft.drawString("PLAYING", 6, 45, 2);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("PLAYLIST", 6, 100, 2);
    tft.fillRect(4, 64, 232, 22, TFT_BLACK);
    tft.fillRect(0, 112, 14, 130, TFT_BLACK);
    tft.fillCircle(7, 125 + (file_index * 15), 3, TFT_ORANGE);
}

void print_song_time() {
    music_info.runtime = audio.getAudioCurrentTime();
    music_info.length = audio.getAudioFileDuration();
    music_info.volume = audio.getVolume();
    music_info.m = music_info.length / 60;
    music_info.s = music_info.length % 60;
}

int get_music_list(fs::FS &fs, const char *dirname, uint8_t levels, String wavlist[30]) {
    int i = 0;
    File root = fs.open(dirname);
    
    if (!root) {
        Serial.println("Failed to open directory");
        return 0;
    }
    
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return 0;
    }

    File file = root.openNextFile();
    while (file && i < 30) {
        if (!file.isDirectory()) {
            String temp = file.name();
            if (temp.endsWith(".mp3") || temp.endsWith(".MP3") || 
                temp.endsWith(".wav") || temp.endsWith(".WAV")) {
                wavlist[i] = "/" + temp;
                i++;
            }
        }
        file = root.openNextFile();
    }
    
    return i;
}

// --- AUDIO LIBRARY CALLBACKS ---

void audio_info(const char *info) {
    Serial.print("audio_info: ");
    Serial.println(info);
}

void audio_eof_mp3(const char *info) {
    Serial.println("Song ended, playing next...");
    
    file_index++;
    if (file_index >= file_num) {
        file_index = 0;
    }
    
    drawStart();
    open_new_song(file_list[file_index]);
    print_song_time();
}

void audio_showstation(const char *info) {
    Serial.print("station: ");
    Serial.println(info);
}

void audio_showstreamtitle(const char *info) {
    Serial.print("streamtitle: ");
    Serial.println(info);
}

void audio_bitrate(const char *info) {
    Serial.print("bitrate: ");
    Serial.println(info);
}

void audio_id3data(const char *info) {
    Serial.print("id3data: ");
    Serial.println(info);
}