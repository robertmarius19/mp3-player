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
int playlist_index = 0;

bool paused = false;
bool play_next_request = false;
uint32_t run_time = 0;
uint32_t button_time = 0;


void open_new_song(String filename);
void tft_music();
void drawStart();
void drawVolume();
void print_song_time();
int get_music_list(fs::FS &fs, const char *dirname, uint8_t levels, String wavlist[30]);
void update_playlist_visuals();
bool mountSD(); // New helper function

void setup() {
    Serial.begin(115200);
    Serial.println("Starting MP3 Player...");
    
    // Setup all button pins
    pinMode(Pin_vol_up, INPUT_PULLUP);
    pinMode(Pin_vol_down, INPUT_PULLUP);
    pinMode(Pin_mute, INPUT_PULLUP);
    pinMode(Pin_previous, INPUT_PULLUP);
    pinMode(Pin_prev, INPUT_PULLUP);
    pinMode(Pin_next, INPUT_PULLUP);

    // setup screen
    tft.init();
    tft.setRotation(0);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    // setup backlight
    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, 144);

    // setup sd_card
    if (!mountSD()) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("SD CARD ERROR", 10, 100, 4);
        while(1) delay(1000);
    }
    
    // read music files from SD card
    file_num = get_music_list(SD, "/", 0, file_list);
    Serial.print("Music files found: ");
    Serial.println(file_num);

    // audio_config
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(18); 
    
    // using psram
    if(psramFound()){
        audio.setInBufferSize(300 * 1024);
        Serial.println("PSRAM Enabled: Using 300KB Audio Buffer");
    }
    
    delay(100);

    // UI_drawing
    drawStart();
    update_playlist_visuals(); 
    drawVolume();

    if (file_num > 0) {
        open_new_song(file_list[file_index]);
        print_song_time();
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("No MP3 Found", 10, 50, 2);
    }
}

void loop() {
    audio.loop();

    // auto-play next song
    if (play_next_request) {
        play_next_request = false;
        audio.stopSong();

        if (file_index < file_num - 1) file_index++;
        else file_index = 0;

        drawStart(); 
        open_new_song(file_list[file_index]);
        print_song_time();
    } 

    // update_time
    if (millis() - run_time > 1000) {
        run_time = millis();
        print_song_time();
        tft_music();
    }

    // buttons_controls
    if (millis() - button_time > 300) {
        
        // next
        if (digitalRead(Pin_next) == LOW) {
            Serial.println("Next");
            audio.stopSong(); 
            if (file_index < file_num - 1) file_index++;
            else file_index = 0;
            drawStart();
            open_new_song(file_list[file_index]);
            print_song_time();
            button_time = millis();
        }
        
        // prev
        if (digitalRead(Pin_prev) == LOW) {
            Serial.println("Prev");
            audio.stopSong(); 
            if (file_index > 0) file_index--;
            else file_index = file_num - 1;
            drawStart();
            open_new_song(file_list[file_index]);
            print_song_time();
            button_time = millis();
        }
        
        // pause
        if (digitalRead(Pin_mute) == LOW) {
            audio.pauseResume();
            paused = !paused;
            tft.fillRect(6, 45, 100, 20, TFT_BLACK);
            if (paused) {
                tft.setTextColor(TFT_RED, TFT_BLACK);
                tft.drawString("PAUSED", 6, 45, 2);
            } else {
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
                tft.drawString("PLAYING", 6, 45, 2);
            }
            button_time = millis();
        }
        
        // vol
        if (digitalRead(Pin_vol_up) == LOW) {
            if (music_info.volume < 21) music_info.volume++;
            audio.setVolume(music_info.volume);
            drawVolume();
            button_time = millis();
        }
        if (digitalRead(Pin_vol_down) == LOW) {
            if (music_info.volume > 0) music_info.volume--;
            audio.setVolume(music_info.volume);
            drawVolume();
            button_time = millis();
        }
    }
}


bool mountSD() {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    delay(50);
    return SD.begin(SD_CS, SPI);
}


void open_new_song(String filename) {
    audio.stopSong();
    
    bool success = audio.connecttoFS(SD, filename.c_str());
    // re-init sd-card if open fails (couldnt play more than 7 songs without it)
    if (!success) {
        Serial.println("Memory Error! Resetting SD Card...");
        SD.end();        
        delay(100);      
        mountSD();       
        delay(100);
        
        success = audio.connecttoFS(SD, filename.c_str());
    }

    if (success) {
        Serial.print("Playing: ");
        Serial.println(filename);
        
        // Parse Name for Screen
        String cleanName = filename;
        if(cleanName.startsWith("/")) cleanName.remove(0,1);
        cleanName.replace(".mp3", "");
        if(cleanName.length() > 33) cleanName = cleanName.substring(0, 33);
        music_info.name = cleanName;

        music_info.runtime = 0;
        music_info.length = audio.getAudioFileDuration();
        music_info.volume = audio.getVolume();
        music_info.m = music_info.length / 60;
        music_info.s = music_info.length % 60;
        paused = false;
    } else {
        Serial.print("Failed to open: ");
        Serial.println(filename);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("FILE ERROR", 6, 45, 2);
    }
}

void drawStart() {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("TIME", 6, 0, 2);
    tft.drawString("VOLUME", 182, 0, 2);
    
    tft.fillRect(6, 45, 100, 20, TFT_BLACK);
    if(paused) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("PAUSED", 6, 45, 2);
    } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("PLAYING", 6, 45, 2);
    }

    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("PLAYLIST", 6, 100, 2);
    tft.fillRect(4, 64, 232, 22, TFT_BLACK);

    if (file_index >= playlist_index + 8) {
        playlist_index = file_index - 7; 
        update_playlist_visuals(); 
    }
    else if (file_index < playlist_index) {
        playlist_index = file_index;
        update_playlist_visuals(); 
    }
    else {
        update_playlist_visuals();
    }
}

void update_playlist_visuals() {
    tft.fillRect(0, 118, 240, 122, TFT_BLACK); 
    tft.setTextColor(0xBDD7, TFT_BLACK);
    int y = 0;
    
    for (int i = playlist_index; i < file_num; i++) {
        if (y < 120) {
            String displayName = file_list[i];
            if(displayName.startsWith("/")) displayName.remove(0,1);
            displayName.replace(".mp3", "");
            if(displayName.length() > 34) displayName = displayName.substring(0, 34);
            
            tft.drawString(displayName, 18, 118 + y, 2);
            y += 15;
        } else {
            break;
        }
    }

    int relative_position = file_index - playlist_index;
    if (relative_position >= 0 && relative_position < 8) {
        tft.fillCircle(7, 125 + (relative_position * 15), 3, TFT_ORANGE);
    }
}

void tft_music() {
    if (music_info.length == 0) return;
    
    int line = map(music_info.runtime, 0, music_info.length, 10, 210);
    tft.fillRect(0, 87, 240, 9, TFT_BLACK);
    tft.drawLine(10, 92, 210, 92, 0xBDD7);
    tft.drawLine(10, 90, 10, 94, 0xBDD7);
    tft.drawLine(210, 90, 210, 94, 0xBDD7);
    tft.fillRect(line, 88, 6, 8, TFT_GREEN);

    int s = music_info.runtime % 60;
    int m = music_info.runtime / 60;
    String ts = (s < 10) ? "0" + String(s) : String(s);
    String tm = (m < 10) ? "0" + String(m) : String(m);
    
    tft.setTextColor(0xFC3D, TFT_BLACK);
    tft.drawString(music_info.name, 4, 66, 2);
    
    String total_s = (music_info.s < 10) ? "0" + String(music_info.s) : String(music_info.s);
    String total_m = String(music_info.m);
    tft.drawString(total_m + ":" + total_s, 190, 100, 2);
    
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
    if (!root || !root.isDirectory()) return 0;

    File file = root.openNextFile();
    while (file && i < 30) {
        if (!file.isDirectory()) {
            String temp = file.name();
            if (!temp.startsWith(".") && !temp.startsWith("_")) {
                if (temp.endsWith(".mp3") || temp.endsWith(".MP3") || 
                    temp.endsWith(".wav") || temp.endsWith(".WAV")) {
                    if(!temp.startsWith("/")) temp = "/" + temp;
                    wavlist[i] = temp;
                    i++;
                }
            }
        }
        file = root.openNextFile();
    }
    return i;
}
void audio_eof_mp3(const char *info) {
    Serial.print("EOF MP3: ");
    Serial.println(info);
    play_next_request = true;
}

void audio_info(const char *info) { 
    Serial.print("audio_info: "); 
    Serial.println(info); 
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