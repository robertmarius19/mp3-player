#include <TFT_eSPI.h>
#include "Arduino.h"
#include "Audio.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <WiFi.h>

// WiFi Credentials
const char* ssid = "***********";
const char* password = "********";

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

enum PlayMode { SD_CARD_MODE, RADIO_MODE };
PlayMode current_mode = SD_CARD_MODE;


struct RadioStation {
    String name;
    String url;
};

RadioStation radio_stations[] = {
    {"NPR", "https://npr-ice.streamguys1.com/live.mp3"},
    {"Chill Radio", "http://streams.ilovemusic.de/iloveradio17.mp3"},
    {"80s Hits", "http://streams.80s80s.de/web/mp3-192"},
    {"EDM Radio", "http://streams.bigfm.de/bigfm-nitroxedm-128-mp3"}
};
int radio_count = 4;
int radio_index = 0;
String current_bitrate = "";
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
bool wifi_connected = false;
uint32_t run_time = 0;
uint32_t button_time = 0;
uint32_t mode_button_press_time = 0;

// Radio mode UI
String current_stream_title = "";
int signal_strength = 0;
uint32_t radio_ui_update = 0;

void open_new_song(String filename);
void open_radio_station(int index);
void tft_music();
void drawStart();
void drawVolume();
void print_song_time();
int get_music_list(fs::FS &fs, const char *dirname, uint8_t levels, String wavlist[30]);
void update_playlist_visuals();
void update_radio_list();
void switch_mode();
bool mountSD();
void connectWiFi();
void draw_radio_ui();
void draw_signal_indicator();
void drawRadioPanel();
void setup() {
    Serial.begin(115200);
    Serial.println("Starting MP3 Player with Radio...");
    
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

    // Show startup message
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("MP3 Player", 60, 100, 4);
    tft.drawString("Starting...", 70, 130, 2);

    // setup sd_card
    if (!mountSD()) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("SD CARD ERROR", 10, 100, 4);
        tft.drawString("Radio only", 40, 130, 2);
        delay(2000);
    } else {
        // read music files from SD card
        file_num = get_music_list(SD, "/", 0, file_list);
        Serial.print("Music files found: ");
        Serial.println(file_num);
    }

    connectWiFi();


    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(18); 
    

    if(psramFound()){
        audio.setInBufferSize(300 * 1024);
        Serial.println("PSRAM Enabled: Using 300KB Audio Buffer");
    }
    
    delay(100);

    
    tft.fillScreen(TFT_BLACK);
    drawStart();
    
    if (current_mode == SD_CARD_MODE) {
        update_playlist_visuals();
        if (file_num > 0) {
            open_new_song(file_list[file_index]);
            print_song_time();
        } else {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("No MP3 Found", 10, 50, 2);
        }
    } else {
        update_radio_list();
        if (wifi_connected) {
            open_radio_station(radio_index);
        }
    }
    
    drawVolume();
}

void loop() {
    audio.loop();

    // Update radio UI 
    if (current_mode == RADIO_MODE && millis() - radio_ui_update > 3000) {
        radio_ui_update = millis();
        draw_radio_ui();
    }

    //  long press on Pin_mute change the mode
    if (digitalRead(Pin_mute) == LOW) {
        if (mode_button_press_time == 0) {
            mode_button_press_time = millis();
        } else if (millis() - mode_button_press_time > 1500) { // 1.5 second hold
            switch_mode();
            mode_button_press_time = 0;
            while(digitalRead(Pin_mute) == LOW) delay(10); 
        }
    } else {
        mode_button_press_time = 0;
    }

    // auto-play next song
    if (play_next_request && current_mode == SD_CARD_MODE) {
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
        if (current_mode == SD_CARD_MODE) {
            print_song_time();
        }
        tft_music();
    }

    // buttons_controls
    if (millis() - button_time > 300) {
        
        // next
        if (digitalRead(Pin_next) == LOW) {
            Serial.println("Next");
            
            if (current_mode == SD_CARD_MODE) {
                audio.stopSong(); 
                if (file_index < file_num - 1) file_index++;
                else file_index = 0;
                drawStart();
                open_new_song(file_list[file_index]);
                print_song_time();
            } else {
                // radio mode
                if (radio_index < radio_count - 1) radio_index++;
                else radio_index = 0;
                drawStart();
                open_radio_station(radio_index);
            }
            button_time = millis();
        }
        
        // prev
        if (digitalRead(Pin_prev) == LOW) {
            Serial.println("Prev");
            
            if (current_mode == SD_CARD_MODE) {
                audio.stopSong(); 
                if (file_index > 0) file_index--;
                else file_index = file_num - 1;
                drawStart();
                open_new_song(file_list[file_index]);
                print_song_time();
            } else {
                // radio mode
                if (radio_index > 0) radio_index--;
                else radio_index = radio_count - 1;
                drawStart();
                open_radio_station(radio_index);
            }
            button_time = millis();
        }
        
        // pause 
        if (digitalRead(Pin_mute) == LOW && current_mode == SD_CARD_MODE) {
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

void connectWiFi() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Connecting WiFi...", 20, 100, 2);
    
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        Serial.println("\nWiFi Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("WiFi Connected!", 30, 100, 2);
        delay(1000);
    } else {
        wifi_connected = false;
        Serial.println("\nWiFi Failed!");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("WiFi Failed", 40, 100, 2);
        tft.drawString("SD Mode Only", 30, 130, 2);
        delay(2000);
    }
}

void switch_mode() {
    Serial.println("Switching mode...");
    audio.stopSong();
    delay(200);
    
    if (current_mode == SD_CARD_MODE) {
        // switch to radio-mode
        if (wifi_connected) {
            current_mode = RADIO_MODE;
            Serial.println("Switched to RADIO MODE");
            tft.fillScreen(TFT_BLACK);
            drawStart();
            open_radio_station(radio_index);
        } else {
            // can't switch - no WiFi
            tft.fillRect(60, 120, 180, 30, TFT_BLACK);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("No WiFi!", 70, 125, 2);
            delay(1000);
            drawStart();
        }
    } else {
        // switch with sd-card mode
        if (file_num > 0) {
            current_mode = SD_CARD_MODE;
            Serial.println("Switched to SD CARD MODE");
            tft.fillScreen(TFT_BLACK);
            drawStart();
            open_new_song(file_list[file_index]);
            print_song_time();
        } else {
            // Can't switch - no files
            tft.fillRect(60, 120, 180, 30, TFT_BLACK);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("No MP3 Files!", 40, 125, 2);
            delay(1000);
            drawStart();
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

void open_radio_station(int index) {
    if (!wifi_connected) {
        Serial.println("No WiFi connection!");
        return;
    }
    
    audio.stopSong();
    delay(100);
    
    Serial.print("Connecting to: ");
    Serial.println(radio_stations[index].name);
    
    // Show connecting message
    tft.fillRect(4, 64, 232, 22, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Connecting...", 4, 66, 2);
    
    bool success = audio.connecttohost(radio_stations[index].url.c_str());
    
    if (success) {
        music_info.name = radio_stations[index].name;
        current_stream_title = "";
        music_info.runtime = 0;
        music_info.length = 0;
        music_info.volume = audio.getVolume();
        paused = false;
        signal_strength = WiFi.RSSI();
        
        // Clear connecting message
        tft.fillRect(4, 64, 232, 22, TFT_BLACK);
        
        Serial.println("✓ Streaming");
    } else {
        Serial.println("✗ Failed to connect");
        tft.fillRect(4, 64, 232, 22, TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("CONNECTION FAILED", 10, 66, 2);
    }
}

void open_new_song(String filename) {
    audio.stopSong();
    
    bool success = audio.connecttoFS(SD, filename.c_str());
    // re-init sd-card if open fails
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
        cleanName.replace(".MP3", "");
        cleanName.replace(".wav", "");
        cleanName.replace(".WAV", "");
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
    if (current_mode == RADIO_MODE) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("RADIO", 6, 45, 2);
        // Draw WiFi signal indicator
        draw_signal_indicator();
    } else {
        if(paused) {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("PAUSED", 6, 45, 2);
        } else {
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.drawString("PLAYING", 6, 45, 2);
        }
    }

    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    if (current_mode == RADIO_MODE) {
        tft.drawString("STATIONS", 6, 100, 2);
    } else {
        tft.drawString("PLAYLIST", 6, 100, 2);
    }
    tft.fillRect(4, 64, 232, 22, TFT_BLACK);

    if (current_mode == SD_CARD_MODE) {
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
    } else {
        update_radio_list();
    }
}

void update_radio_list() {
    
    tft.fillRect(0, 118, 240, 122, TFT_BLACK); 
    
    drawRadioPanel();

   
    tft.setTextColor(0xBDD7, TFT_BLACK);
    int y = 0;
    
    for (int i = 0; i < radio_count && y < 120; i++) {
        String displayName = radio_stations[i].name;
        
       
        if(displayName.length() > 18) displayName = displayName.substring(0, 18) + "..";
        
        // Highlight selection
        if (i == radio_index) {
             tft.setTextColor(TFT_CYAN, TFT_BLACK);
             tft.drawString(">", 5, 118 + y, 2);
        } else {
             tft.setTextColor(0xBDD7, TFT_BLACK);
        }
        
        tft.drawString(displayName, 18, 118 + y, 2);
        y += 15;
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
            displayName.replace(".MP3", "");
            displayName.replace(".wav", "");
            displayName.replace(".WAV", "");
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
    if (current_mode == RADIO_MODE) {
        // Radio mode 
        tft.fillRect(180, 95, 60, 20, TFT_BLACK);
        
        // station name
        tft.fillRect(4, 64, 232, 22, TFT_BLACK);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString(music_info.name, 4, 66, 2);
        
        // LIVE indicator with simple animation
        static uint8_t live_fade = 0;
        live_fade += 8;
        uint16_t live_color = tft.color565(255, live_fade, 0); // Red to yellow fade
        
        tft.fillRect(5, 15, 60, 30, TFT_BLACK);
        tft.setTextColor(live_color, TFT_BLACK);
        tft.drawString("LIVE", 5, 20, 4);
        
        // Show current song/track if available
        if (current_stream_title.length() > 0) {
            tft.fillRect(0, 87, 240, 9, TFT_BLACK);
            tft.setTextColor(0xBDD7, TFT_BLACK);
            String displayTitle = current_stream_title;
            if (displayTitle.length() > 32) {
                displayTitle = displayTitle.substring(0, 32) + "...";
            }
            tft.drawString(displayTitle, 5, 88, 1);
        }
        
        return;
    }
    
    // SD CARD MODE - original code
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

void draw_radio_ui() {
    signal_strength = WiFi.RSSI();
    draw_signal_indicator();
}

void draw_signal_indicator() {
    //draw wifi signal
    int x = 198;
    int y = 50;  =
    int bar_width = 4;
    int bar_spacing = 6;
    

    tft.fillRect(x, y, 30, 12, TFT_BLACK);
    
    int bars = 0;
    if (signal_strength > -50) bars = 5;
    else if (signal_strength > -60) bars = 4;
    else if (signal_strength > -70) bars = 3;
    else if (signal_strength > -80) bars = 2;
    else if (signal_strength > -90) bars = 1;
    
    for (int i = 0; i < 5; i++) {
        int bar_height = 3 + (i * 2);
        uint16_t color;
        
        if (i < bars) {
            if (bars >= 4) color = TFT_GREEN;
            else if (bars >= 2) color = TFT_YELLOW;
            else color = TFT_RED;
        } else {
        
            color = 0x39C7;
        }
        
        tft.fillRect(x + (i * bar_spacing), y + (12 - bar_height), bar_width, bar_height, color);
    }
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

void drawRadioPanel() {
    if (current_mode != RADIO_MODE) return;  
    int x_start = 160;
    int y_start = 90;
    int width = 80;
    int height = 122;
    
    int center_x = x_start + (width/2);
    int center_y = y_start + 75;

    // tower-base
    tft.drawLine(center_x, center_y - 20, center_x - 10, center_y + 20, TFT_WHITE);
    tft.drawLine(center_x, center_y - 20, center_x + 10, center_y + 20, TFT_WHITE);
    tft.drawLine(center_x - 5, center_y, center_x + 5, center_y, TFT_WHITE); // Crossbar
    tft.drawLine(center_x, center_y - 20, center_x, center_y - 35, TFT_WHITE); // Antenna tip
    
    // antenna-circles
    tft.drawCircle(center_x, center_y - 35, 3, TFT_RED);
    tft.drawCircle(center_x, center_y - 35, 7, TFT_ORANGE);
    tft.drawCircle(center_x, center_y - 35, 12, TFT_YELLOW);
    
    tft.setTextDatum(TL_DATUM);
}
void audio_eof_mp3(const char *info) {
    Serial.print("EOF MP3: ");
    Serial.println(info);
    if (current_mode == SD_CARD_MODE) {
        play_next_request = true;
    }
}

void audio_info(const char *info) { 
    Serial.print("audio_info: "); 
    Serial.println(info); 
}

void audio_showstation(const char *info) {
    Serial.print("station: ");
    Serial.println(info);
    // update display with station info if in radio mode
    if (current_mode == RADIO_MODE) {
        music_info.name = String(info);
    }
}

void audio_showstreamtitle(const char *info) {
    Serial.print("streamtitle: ");
    Serial.println(info);
    // update display with current song if in radio mode
    if (current_mode == RADIO_MODE && strlen(info) > 0) {
        current_stream_title = String(info);
        
        tft.fillRect(0, 87, 240, 9, TFT_BLACK);
        tft.setTextColor(0xBDD7, TFT_BLACK);
        String displayTitle = current_stream_title;
        if (displayTitle.length() > 32) {
            displayTitle = displayTitle.substring(0, 32) + "...";
        }
        tft.drawString(displayTitle, 5, 88, 1);
    }
}

void audio_bitrate(const char *info) {
    Serial.print("bitrate: "); 
    Serial.println(info);
    current_bitrate = String(info);
    if (current_mode == RADIO_MODE) {
        drawRadioPanel();
    }
}

void audio_id3data(const char *info) {
    Serial.print("id3data: "); 
    Serial.println(info); 
}
