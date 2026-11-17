// SPDX-License-Identifier: GPL-3.0-only
#include "Mk3Bridge.h"
#include "../engine/AudioEngine.h"
#include "font8x8.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "MK3_BRIDGE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...)
#define LOGW(...)
#define LOGE(...)
#endif

namespace mk3 {

constexpr uint16_t MK3_VID = 0x17cc;
constexpr uint16_t MK3_PID = 0x1600;

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 272;
constexpr int SCREEN_PIXELS = SCREEN_W * SCREEN_H;
constexpr int PIXEL_BYTES = SCREEN_PIXELS * 2;
constexpr int HEADER_BYTES = 20;
constexpr int TAIL_BYTES = 8;
constexpr int TOTAL_FRAME_BYTES = HEADER_BYTES + PIXEL_BYTES + TAIL_BYTES;

constexpr unsigned int EP_OUT_LEDS    = 0x03;
constexpr unsigned int EP_IN_INPUT    = 0x83;
constexpr unsigned int EP_OUT_DISPLAY = 0x04;
constexpr size_t USBFS_CHUNK_SIZE     = 16384;

// RGB565 Helper (Big-Endian for Maschine MK3)
static constexpr inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// UI Colors (RGB565)
constexpr uint16_t COL_BG         = rgb565(8, 12, 18);
constexpr uint16_t COL_PANEL_BG   = rgb565(14, 20, 30);
constexpr uint16_t COL_PANEL_BRD  = rgb565(36, 52, 75);
constexpr uint16_t COL_CYAN       = rgb565(0, 240, 255);
constexpr uint16_t COL_LIME       = rgb565(57, 255, 20);
constexpr uint16_t COL_AMBER      = rgb565(255, 170, 0);
constexpr uint16_t COL_MAGENTA    = rgb565(255, 0, 128);
constexpr uint16_t COL_PURPLE     = rgb565(160, 32, 240);
constexpr uint16_t COL_WHITE      = rgb565(255, 255, 255);
constexpr uint16_t COL_DIM_WHITE  = rgb565(160, 175, 200);
constexpr uint16_t COL_DARK_GREY  = rgb565(60, 70, 85);
constexpr uint16_t COL_RED        = rgb565(255, 40, 40);

// Hardware LED Colors (0-127)
constexpr uint8_t LED_OFF         = 0;
constexpr uint8_t LED_RED         = 6;
constexpr uint8_t LED_ORANGE      = 10;
constexpr uint8_t LED_PEACHY      = 11;
constexpr uint8_t LED_AMBER       = 14;
constexpr uint8_t LED_YELLOW      = 22;
constexpr uint8_t LED_SPRING      = 26;
constexpr uint8_t LED_GREEN       = 30;
constexpr uint8_t LED_CYAN        = 31;
constexpr uint8_t LED_LIME        = 34;
constexpr uint8_t LED_SKY         = 38;
constexpr uint8_t LED_BLUE        = 42;
constexpr uint8_t LED_PURPLE      = 50;
constexpr uint8_t LED_MAGENTA     = 58;
constexpr uint8_t LED_PINK        = 62;
constexpr uint8_t LED_DARK_GREY   = 76;
constexpr uint8_t LED_GREY        = 77;
constexpr uint8_t LED_WHITE       = 78;

constexpr uint8_t SLOT_LEDS[4] = { LED_CYAN, LED_AMBER, LED_PURPLE, LED_LIME };
constexpr uint16_t SLOT_COLORS[4] = { COL_CYAN, COL_AMBER, COL_MAGENTA, COL_LIME };

static inline bool isAccidental(int semitone) {
    int s = ((semitone % 12) + 12) % 12;
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}

static const char* NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

Mk3Bridge::Mk3Bridge(fm1deck::AudioEngine* engine)
    : engine_(engine) {
    screen0Pixels_ = std::make_unique<uint8_t[]>(PIXEL_BYTES);
    screen1Pixels_ = std::make_unique<uint8_t[]>(PIXEL_BYTES);
    prevScreen0Pixels_ = std::make_unique<uint8_t[]>(PIXEL_BYTES);
    prevScreen1Pixels_ = std::make_unique<uint8_t[]>(PIXEL_BYTES);
    memset(prevScreen0Pixels_.get(), 0xFF, PIXEL_BYTES);
    memset(prevScreen1Pixels_.get(), 0xFF, PIXEL_BYTES);
    fwScreenCopy_ = std::make_unique<uint16_t[]>(fm1deck::FirmwareSlot::kScreenPixels);

    for (int i = 0; i < 16; ++i) {
        pads_[static_cast<size_t>(i)].color = isAccidental(i) ? LED_BLUE : LED_CYAN;
    }
    for (int i = 0; i < 8; ++i) {
        encoderVals_[static_cast<size_t>(i)] = 512;
    }
}

Mk3Bridge::~Mk3Bridge() {
    stop();
}

void Mk3Bridge::start() {
    if (running_.load(std::memory_order_relaxed)) return;
    running_.store(true, std::memory_order_release);
    workerThread_ = std::thread(&Mk3Bridge::threadMain, this);
    LOGI("Mk3Bridge worker thread started");
}

void Mk3Bridge::stop() {
    if (!running_.load(std::memory_order_relaxed)) return;
    running_.store(false, std::memory_order_release);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    disconnectDevice();
    LOGI("Mk3Bridge stopped");
}

int Mk3Bridge::findAndOpenDevice(char* outPath, size_t maxLen) {
    DIR* dir = opendir("/sys/bus/usb/devices");
    if (!dir) {
        system("/dev/su/su -c 'setenforce 0'");
        system("su -c 'setenforce 0'");
        dir = opendir("/sys/bus/usb/devices");
    }
    if (!dir) return -1;
    struct dirent* entry;
    int busnum = -1, devnum = -1;

    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        char path[256];
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor", entry->d_name);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        char vid[16] = {0};
        if (!fgets(vid, sizeof(vid), f)) { fclose(f); continue; }
        fclose(f);
        if (strncmp(vid, "17cc", 4) != 0) continue;

        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idProduct", entry->d_name);
        f = fopen(path, "r");
        if (!f) continue;
        char pid[16] = {0};
        if (!fgets(pid, sizeof(pid), f)) { fclose(f); continue; }
        fclose(f);
        if (strncmp(pid, "1600", 4) != 0) continue;

        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/busnum", entry->d_name);
        f = fopen(path, "r");
        if (f) { fscanf(f, "%d", &busnum); fclose(f); }

        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/devnum", entry->d_name);
        f = fopen(path, "r");
        if (f) { fscanf(f, "%d", &devnum); fclose(f); }

        snprintf(outPath, maxLen, "/dev/bus/usb/%03d/%03d", busnum, devnum);
        closedir(dir);
        break;
    }
    if (busnum < 0 || devnum < 0) return -1;

    LOGI("Found Maschine MK3 at %s (bus %d, dev %d)", outPath, busnum, devnum);

    int fd = open(outPath, O_RDWR);
    if (fd < 0 && (errno == EACCES || errno == EPERM)) {
        LOGW("Open %s failed (errno %d), attempting su chmod...", outPath, errno);
        system("/dev/su/su -c 'chmod 666 /dev/bus/usb/*/*'");
        system("/dev/su/su -c '[ -f /data/local/tmp/usb_audio_policy_configuration.xml ] && mount -o bind /data/local/tmp/usb_audio_policy_configuration.xml /vendor/etc/usb_audio_policy_configuration.xml'");
        system("su -c 'chmod 666 /dev/bus/usb/*/*'");
        fd = open(outPath, O_RDWR);
    }
    if (fd < 0) {
        LOGE("Could not open %s: %s (errno %d)", outPath, strerror(errno), errno);
        return -1;
    }

    // Detach and claim Interface 4 (HID Control Surface)
    struct usbdevfs_ioctl ctl;
    memset(&ctl, 0, sizeof(ctl));
    ctl.ifno = 4;
    ctl.ioctl_code = USBDEVFS_DISCONNECT;
    ioctl(fd, USBDEVFS_IOCTL, &ctl);

    int iface4 = 4;
    if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface4) < 0) {
        LOGE("Failed to claim Interface 4: %s", strerror(errno));
        close(fd);
        return -1;
    }

    // Detach and claim Interface 5 (Bulk Display)
    memset(&ctl, 0, sizeof(ctl));
    ctl.ifno = 5;
    ctl.ioctl_code = USBDEVFS_DISCONNECT;
    ioctl(fd, USBDEVFS_IOCTL, &ctl);

    int iface5 = 5;
    if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface5) < 0) {
        LOGE("Failed to claim Interface 5: %s", strerror(errno));
        close(fd);
        return -1;
    }

    currentBus_ = busnum;
    currentDev_ = devnum;
    usbFd_ = fd;
    LOGI("Maschine MK3 Interfaces 4 & 5 claimed successfully!");
    return fd;
}

void Mk3Bridge::disconnectDevice() {
    if (usbFd_ >= 0) {
        close(usbFd_);
        usbFd_ = -1;
    }
    connected_.store(false, std::memory_order_release);
    currentBus_ = -1;
    currentDev_ = -1;
}

int Mk3Bridge::bulkSend(unsigned int ep, const uint8_t* data, size_t len, unsigned int timeoutMs) {
    if (usbFd_ < 0) return -1;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = std::min(len - sent, USBFS_CHUNK_SIZE);
        struct usbdevfs_bulktransfer bulk;
        bulk.ep = ep;
        bulk.len = static_cast<unsigned int>(chunk);
        bulk.timeout = timeoutMs;
        bulk.data = const_cast<void*>(static_cast<const void*>(data + sent));

        int ret = ioctl(usbFd_, USBDEVFS_BULK, &bulk);
        if (ret < 0) {
            if (errno == EBUSY || errno == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            LOGW("USBDEVFS_BULK ep 0x%02x failed: %s (errno %d)", ep, strerror(errno), errno);
            return ret;
        }
        sent += static_cast<size_t>(ret);
        if (ep == EP_OUT_DISPLAY && sent < len) {
            usleep(500); // Allow kernel musb to service isochronous USB audio packets without crackling
        }
    }
    return 0;
}

// Drawing primitives
static inline void setPixel(uint8_t* pixels, int x, int y, uint16_t color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    int idx = (y * SCREEN_W + x) * 2;
    pixels[idx]     = static_cast<uint8_t>((color >> 8) & 0xFF);
    pixels[idx + 1] = static_cast<uint8_t>(color & 0xFF);
}

static void fillRect(uint8_t* pixels, int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W)  w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    const uint8_t hi = static_cast<uint8_t>((color >> 8) & 0xFF);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);

    for (int j = y; j < y + h; ++j) {
        int idx = (j * SCREEN_W + x) * 2;
        for (int i = 0; i < w; ++i) {
            pixels[idx++] = hi;
            pixels[idx++] = lo;
        }
    }
}

static void drawRect(uint8_t* pixels, int x, int y, int w, int h, uint16_t color) {
    for (int i = x; i < x + w; ++i) {
        setPixel(pixels, i, y, color);
        setPixel(pixels, i, y + h - 1, color);
    }
    for (int j = y; j < y + h; ++j) {
        setPixel(pixels, x, j, color);
        setPixel(pixels, x + w - 1, j, color);
    }
}

static void drawChar(uint8_t* pixels, int x, int y, char c, uint16_t fg, uint16_t bg, int scale, bool transparent) {
    if (static_cast<unsigned char>(c) >= 128) c = '?';
    for (int py = 0; py < 8; ++py) {
        uint8_t row = font8x8_basic[static_cast<unsigned char>(c)][py];
        for (int px = 0; px < 8; ++px) {
            const int bit = (row >> (7 - px)) & 1;
            const uint16_t col = bit ? fg : bg;
            if (bit || !transparent) {
                if (scale == 1) {
                    setPixel(pixels, x + px, y + py, col);
                } else {
                    for (int sy = 0; sy < scale; ++sy) {
                        for (int sx = 0; sx < scale; ++sx) {
                            setPixel(pixels, x + px * scale + sx, y + py * scale + sy, col);
                        }
                    }
                }
            }
        }
    }
}

static void drawString(uint8_t* pixels, int x, int y, const char* str, uint16_t fg, uint16_t bg, int scale, bool transparent) {
    int curX = x;
    while (*str) {
        if (*str == '\n') {
            y += 8 * scale + 3;
            curX = x;
        } else {
            drawChar(pixels, curX, y, *str, fg, bg, scale, transparent);
            curX += 8 * scale;
        }
        str++;
    }
}

static void drawPanel(uint8_t* pixels, int x, int y, int w, int h, const char* title, uint16_t borderCol) {
    fillRect(pixels, x, y, w, h, COL_PANEL_BG);
    drawRect(pixels, x, y, w, h, borderCol);
    fillRect(pixels, x + 1, y + 1, w - 2, 14, rgb565(20, 28, 42));
    drawRect(pixels, x, y, w, 15, borderCol);
    drawString(pixels, x + 6, y + 4, title, borderCol, 0, 1, true);
}

static void drawHBar(uint8_t* pixels, int x, int y, int w, int h, int val, int maxVal, uint16_t fg, uint16_t bg) {
    fillRect(pixels, x, y, w, h, bg);
    drawRect(pixels, x, y, w, h, COL_PANEL_BRD);
    if (val > maxVal) val = maxVal;
    if (val < 0) val = 0;
    const int fillW = (val * (w - 2)) / (maxVal > 0 ? maxVal : 1);
    if (fillW > 0) {
        fillRect(pixels, x + 1, y + 1, fillW, h - 2, fg);
    }
}

void Mk3Bridge::flushScreen(int screenId, const uint8_t* pixels) {
    uint8_t frame[TOTAL_FRAME_BYTES];

    // Verified 20-byte Header: [0x84, 0x00, screenId, 0x60, 0,0,0,0, X_hi, X_lo, Y_hi, Y_lo, W_hi, W_lo, H_hi, H_lo, 0x00, half_hi, half_mid, half_lo]
    frame[0] = 0x84;
    frame[1] = 0x00;
    frame[2] = static_cast<uint8_t>(screenId);
    frame[3] = 0x60;
    memset(frame + 4, 0, 4);

    frame[8] = 0x00; frame[9]  = 0x00; // X = 0
    frame[10] = 0x00; frame[11] = 0x00; // Y = 0
    frame[12] = (SCREEN_W >> 8) & 0xFF; frame[13] = SCREEN_W & 0xFF; // W = 480
    frame[14] = (SCREEN_H >> 8) & 0xFF; frame[15] = SCREEN_H & 0xFF; // H = 272

    const uint32_t halfPixels = static_cast<uint32_t>(SCREEN_PIXELS / 2);
    frame[16] = 0x00;
    frame[17] = (halfPixels >> 16) & 0xFF;
    frame[18] = (halfPixels >> 8) & 0xFF;
    frame[19] = halfPixels & 0xFF;

    // 261,120 bytes of Big-Endian RGB565 Pixels
    memcpy(frame + HEADER_BYTES, pixels, PIXEL_BYTES);

    // Verified 8-byte Tail: _BLIT (0x03, 0, 0, 0) + _END (0x40, 0, 0, 0)
    const uint8_t tail[TAIL_BYTES] = { 0x03, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00 };
    memcpy(frame + HEADER_BYTES + PIXEL_BYTES, tail, TAIL_BYTES);

    bulkSend(EP_OUT_DISPLAY, frame, TOTAL_FRAME_BYTES, 1000);
}

void Mk3Bridge::pushButtonLeds() {
    uint8_t rep80[64];
    memset(rep80, 0, sizeof(rep80));
    rep80[0] = 0x80; // Report ID

    // Control buttons 1..28 (idle: 4 dim, pressed: 127)
    for (int i = 1; i <= 28; ++i) {
        rep80[i] = 4;
    }

    // 8 Display buttons above the screens (brightness 4 when idle, 127 when pressed)
    for (int i = 0; i < 8; ++i) {
        rep80[13 + i] = btnDisplay_[static_cast<size_t>(i)] ? 127 : 4;
    }

    // Group buttons A-D: active slot glows bright white/color, others dim
    const int focusSlot = engine_ ? engine_->focus() : 0;
    for (int i = 0; i < 4; ++i) {
        rep80[30 + i] = (i == focusSlot) ? LED_WHITE : SLOT_LEDS[i];
    }
    // Group buttons E-H: Mute, Solo, Oct-, Oct+
    rep80[34] = LED_RED;    // E: MUTE
    rep80[35] = LED_YELLOW; // F: SOLO
    rep80[36] = LED_SKY;    // G: OCT-
    rep80[37] = LED_SKY;    // H: OCT+

    // Transport buttons
    const bool isPlaying = engine_ && engine_->slot() && engine_->slot()->playing();
    rep80[42] = isPlaying ? 127 : 30; // PLAY (Green)
    rep80[43] = btnRec_   ? 127 : 20; // REC (Red)
    rep80[44] = btnStop_  ? 127 : 20; // STOP
    rep80[45] = btnShift_ ? 127 : 20; // SHIFT

    // 4D Joystick ring
    rep80[59] = joyUp_    ? LED_WHITE : LED_CYAN;
    rep80[60] = joyLeft_  ? LED_WHITE : LED_CYAN;
    rep80[61] = joyRight_ ? LED_WHITE : LED_CYAN;
    rep80[62] = joyDown_  ? LED_WHITE : LED_CYAN;

    struct usbdevfs_bulktransfer bulk;
    bulk.ep = EP_OUT_LEDS;
    bulk.len = 63;
    bulk.timeout = 200;
    bulk.data = rep80;
    ioctl(usbFd_, USBDEVFS_BULK, &bulk);
}

void Mk3Bridge::pushPadsLeds() {
    uint8_t rep81[64];
    memset(rep81, 0, sizeof(rep81));
    rep81[0] = 0x81; // Report ID

    // Touch strip: smooth VU / position gradient
    for (int i = 0; i < 25; ++i) {
        rep81[1 + i] = (i < 8) ? LED_GREEN : ((i < 18) ? LED_SKY : LED_PURPLE);
    }

    // 16 Pads (Report 0x81 mapping: 26..29: Pads 13..16; 30..33: Pads 9..12; 34..37: Pads 5..8; 38..41: Pads 1..4)
    rep81[26] = pads_[12].color; rep81[27] = pads_[13].color; rep81[28] = pads_[14].color; rep81[29] = pads_[15].color;
    rep81[30] = pads_[8].color;  rep81[31] = pads_[9].color;  rep81[32] = pads_[10].color; rep81[33] = pads_[11].color;
    rep81[34] = pads_[4].color;  rep81[35] = pads_[5].color;  rep81[36] = pads_[6].color;  rep81[37] = pads_[7].color;
    rep81[38] = pads_[0].color;  rep81[39] = pads_[1].color;  rep81[40] = pads_[2].color;  rep81[41] = pads_[3].color;

    struct usbdevfs_bulktransfer bulk;
    bulk.ep = EP_OUT_LEDS;
    bulk.len = 42;
    bulk.timeout = 200;
    bulk.data = rep81;
    ioctl(usbFd_, USBDEVFS_BULK, &bulk);
}

void Mk3Bridge::renderLeftScreen(uint8_t* pixels) {
    fillRect(pixels, 0, 0, SCREEN_W, SCREEN_H, COL_BG);

    // Top 4 Display Button Labels (HOME, MENU, EDIT, FX)
    const char* leftBtnLabels[4] = { "HOME", "MENU", "EDIT", " FX " };
    for (int i = 0; i < 4; ++i) {
        const int bx = 16 + i * 114;
        const bool pressed = btnDisplay_[static_cast<size_t>(i)];
        fillRect(pixels, bx, 2, 96, 20, pressed ? COL_CYAN : rgb565(18, 26, 38));
        drawRect(pixels, bx, 2, 96, 20, pressed ? COL_WHITE : COL_PANEL_BRD);
        drawString(pixels, bx + 28, 7, leftBtnLabels[i], pressed ? rgb565(0, 0, 0) : COL_DIM_WHITE, 0, 1, true);
    }

    // Live 240x240 RGB565 Screen Buffer from FirmwareSlot::copyScreen()
    auto* currentSlot = engine_ ? engine_->slot() : nullptr;
    if (currentSlot) {
        seenFrameLeft_ = currentSlot->copyScreen(fwScreenCopy_.get(), seenFrameLeft_);
    }

    constexpr int fwX = 14;
    constexpr int fwY = 26;
    drawRect(pixels, fwX - 1, fwY - 1, 242, 242, COL_CYAN);

    // Copy live firmware 240x240 image (already in big-endian RGB565)
    for (int y = 0; y < 240; ++y) {
        const uint8_t* srcRow = reinterpret_cast<const uint8_t*>(fwScreenCopy_.get()) + (y * 240 * 2);
        uint8_t* dstRow = pixels + ((fwY + y) * SCREEN_W + fwX) * 2;
        memcpy(dstRow, srcRow, 240 * 2);
    }

    // Right Sidebar: 4 Rack Slots & Master Telemetry (x = 264..468, y = 26..266)
    drawPanel(pixels, 264, fwY, 204, 240, "RACK SLOTS // GROOVE ENGINE", COL_CYAN);

    const int focusSlot = engine_ ? engine_->focus() : 0;
    const char* slotNames[4] = { "1: JIANT", "2: FELUCCA", "3: SLOOP", "4: HORTATOR" };

    for (int s = 0; s < 4; ++s) {
        const int sy = 48 + s * 38;
        const bool isFocus = (s == focusSlot);
        fillRect(pixels, 270, sy, 192, 32, isFocus ? rgb565(22, 32, 48) : COL_PANEL_BG);
        drawRect(pixels, 270, sy, 192, 32, isFocus ? SLOT_COLORS[s] : COL_PANEL_BRD);

        // Slot title
        drawString(pixels, 276, sy + 6, slotNames[s], isFocus ? COL_WHITE : COL_DIM_WHITE, 0, 1, true);

        // Slot Meter Bar
        if (engine_) {
            auto meter = engine_->slotMeter(s);
            drawHBar(pixels, 276, sy + 18, 120, 8, static_cast<int>(meter.level * 100), 100, SLOT_COLORS[s], rgb565(10, 14, 20));
            char loadStr[16];
            snprintf(loadStr, sizeof(loadStr), "%2.0f%%", meter.load * 100.0f);
            drawString(pixels, 404, sy + 18, loadStr, COL_DIM_WHITE, 0, 1, true);
        }
    }

    // Octave & Transport Status
    char octStr[32];
    snprintf(octStr, sizeof(octStr), "OCTAVE: %+d [C%d-D#%d]", octave_.load(), 3 + octave_.load(), 4 + octave_.load());
    drawString(pixels, 272, 204, octStr, COL_CYAN, 0, 1, true);

    const bool isPlaying = engine_ && engine_->slot() && engine_->slot()->playing();
    drawString(pixels, 272, 222, isPlaying ? "STATE : RUNNING [PLAY]" : "STATE : STOPPED [IDLE]",
               isPlaying ? COL_LIME : COL_AMBER, 0, 1, true);

    drawString(pixels, 272, 240, "AUDIO : OPENSL ES (USB OK)", COL_LIME, 0, 1, true);
}

void Mk3Bridge::renderRightScreen(uint8_t* pixels) {
    fillRect(pixels, 0, 0, SCREEN_W, SCREEN_H, COL_BG);

    // Top 4 Display Button Labels (ARP, SEQ, OCT-, OCT+)
    const char* rightBtnLabels[4] = { "ARP", "SEQ", "OCT-", "OCT+" };
    for (int i = 0; i < 4; ++i) {
        const int bx = 16 + i * 114;
        const bool pressed = btnDisplay_[static_cast<size_t>(4 + i)];
        fillRect(pixels, bx, 2, 96, 20, pressed ? COL_LIME : rgb565(18, 26, 38));
        drawRect(pixels, bx, 2, 96, 20, pressed ? COL_WHITE : COL_PANEL_BRD);
        drawString(pixels, bx + 28, 7, rightBtnLabels[i], pressed ? rgb565(0, 0, 0) : COL_DIM_WHITE, 0, 1, true);
    }

    // Read live UI state from active slot
    Fm1UiState uiState{};
    bool hasUiState = false;
    if (engine_ && engine_->slot()) {
        hasUiState = engine_->slot()->readUiState(&uiState);
    }

    // Page title sub-header
    fillRect(pixels, 12, 26, 456, 18, rgb565(16, 24, 36));
    drawRect(pixels, 12, 26, 456, 18, COL_PANEL_BRD);
    char titleStr[64];
    const int focusSlot = engine_ ? engine_->focus() : 0;
    const auto* host = engine_ ? engine_->host(focusSlot) : nullptr;
    const auto id = host ? host->activeId() : std::string{};
    snprintf(titleStr, sizeof(titleStr), "%s%s%s", id.empty() ? "EMPTY" : id.c_str(),
             hasUiState && uiState.page_title[0] ? " // " : "",
             hasUiState ? uiState.page_title : "");
    drawString(pixels, 20, 31, titleStr, COL_LIME, 0, 1, true);

    // 4 Parameter Dials/Bars for Knobs 1..4 (x = 12..468, y = 48..118)
    for (int k = 0; k < 4; ++k) {
        const int kx = 14 + k * 114;
        const bool touched = encoderTouch_[static_cast<size_t>(k)];
        fillRect(pixels, kx, 48, 110, 68, COL_PANEL_BG);
        drawRect(pixels, kx, 48, 110, 68, touched ? COL_WHITE : COL_PANEL_BRD);

        const char* const fallback[4] = {"K1", "K2", "K3", "K4"};
        const char* label = (hasUiState && uiState.knob[k][0]) ? uiState.knob[k] : fallback[k];
        drawString(pixels, kx + 8, 54, label, touched ? COL_WHITE : COL_CYAN, 0, 1, true);

        if (!hasUiState || !(uiState.knob_valid & (1u << k))) continue;
        const int val = uiState.knob_value[k];
        const int minV = uiState.knob_min[k];
        const int maxV = uiState.knob_max[k];

        char valStr[16];
        snprintf(valStr, sizeof(valStr), "%d", val);
        drawString(pixels, kx + 8, 70, valStr, COL_WHITE, 0, 2, true);

        if (maxV > minV) drawHBar(pixels, kx + 8, 96, 94, 10, val - minV, maxV - minV, COL_LIME, rgb565(8, 12, 18));
    }

    // 4 Secondary Controls (Knobs 5..8: SELECT, ALGO, PRESET, MASTER)
    const char* secLabels[4] = { "SELECT", "ALGO", "PRESET", "MASTER" };
    for (int k = 0; k < 4; ++k) {
        const int kx = 14 + k * 114;
        const int encIdx = 4 + k;
        const bool touched = encoderTouch_[static_cast<size_t>(encIdx)];
        fillRect(pixels, kx, 120, 110, 36, COL_PANEL_BG);
        drawRect(pixels, kx, 120, 110, 36, touched ? COL_WHITE : COL_PANEL_BRD);
        drawString(pixels, kx + 8, 124, secLabels[k], touched ? COL_WHITE : COL_AMBER, 0, 1, true);

        const int val = encoderVals_[static_cast<size_t>(encIdx)];
        drawHBar(pixels, kx + 8, 142, 94, 8, val, 1023, COL_AMBER, rgb565(8, 12, 18));
    }

    // 16-Pad Chromatic Visualizer Grid (y = 162..266, 4 rows x 4 cols)
    drawPanel(pixels, 12, 162, 456, 104, "16 CHROMATIC PADS // ACTIVE KEYBOARD", COL_CYAN);

    // Read firmware LED states for sequencer/pad step illumination
    uint32_t fwLeds[FM1_LED_COUNT]{};
    if (engine_ && engine_->slot()) {
        engine_->slot()->ledsSnapshot(fwLeds);
    }

    const int baseNote = 48 + octave_.load() * 12; // C3 + octave
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            // Physical: row 0 = pads 12..15; row 3 = pads 0..3
            const int p = (3 - r) * 4 + c;
            const int px = 18 + c * 112;
            const int py = 182 + r * 19;

            const bool pressed = pads_[static_cast<size_t>(p)].pressed;
            const int note = baseNote + p;
            const char* nName = NOTE_NAMES[note % 12];
            const bool accidental = isAccidental(note);

            const uint16_t padCol = pressed ? COL_WHITE : (accidental ? rgb565(20, 35, 60) : rgb565(30, 48, 80));
            fillRect(pixels, px, py, 106, 16, padCol);
            drawRect(pixels, px, py, 106, 16, pressed ? COL_WHITE : COL_PANEL_BRD);

            char pStr[24];
            snprintf(pStr, sizeof(pStr), "%02d:%-2s [C%d]", p + 1, nName, note / 12 - 1);
            drawString(pixels, px + 4, py + 4, pStr, pressed ? rgb565(0, 0, 0) : COL_DIM_WHITE, 0, 1, true);
        }
    }
}

bool Mk3Bridge::processInputPacket(const uint8_t* buf, int len) {
    if (len <= 0) return false;
    const uint8_t repId = buf[0];
    bool changed = false;

    // Report 0x01: Digital Buttons & Continuous Encoders
    if (repId == 0x01 && len >= 28) {
        // Transport Buttons (buf[6])
        const bool playNow = (buf[6] & 0x20) != 0;
        const bool recNow  = (buf[6] & 0x40) != 0;
        const bool stopNow = (buf[6] & 0x80) != 0;
        bool panelChanged = playNow != btnPlay_ || recNow != btnRec_;

        if (playNow != btnPlay_) {
            btnPlay_ = playNow;
            changed = true;
        }
        if (recNow != btnRec_) {
            btnRec_ = recNow;
            changed = true;
        }
        if (stopNow != btnStop_) {
            btnStop_ = stopNow;
            if (stopNow && engine_ && engine_->slot()) {
                engine_->link()->stop();
                engine_->slot()->requestSave();
            }
            changed = true;
        }

        // Group Buttons A-H (buf[2])
        for (int i = 0; i < 8; ++i) {
            const bool st = (buf[2] & (1 << i)) != 0;
            if (st != btnGroup_[static_cast<size_t>(i)]) {
                btnGroup_[static_cast<size_t>(i)] = st;
                if (st && engine_) {
                    if (i < 4) {
                        // Switch active slot
                        engine_->setFocus(i);
                        LOGI("Group button pressed: switch focus to slot %d", i);
                    } else if (i == 4) {
                        // Group E: MUTE toggle
                        uint32_t mute = 0, solo = 0;
                        engine_->setGate(mute ^ (1u << engine_->focus()), solo);
                    } else if (i == 5) {
                        // Group F: SOLO toggle
                        uint32_t mute = 0, solo = 0;
                        engine_->setGate(mute, solo ^ (1u << engine_->focus()));
                    } else if (i == 6) {
                        // Group G: OCT-
                        octave_.store(std::max(-2, octave_.load() - 1));
                    } else if (i == 7) {
                        // Group H: OCT+
                        octave_.store(std::min(2, octave_.load() + 1));
                    }
                }
                changed = true;
            }
        }

        // Display Buttons 0..7 above screens (buf[9])
        for (int i = 0; i < 8; ++i) {
            const bool st = (buf[9] & (1 << (7 - i))) != 0;
            if (st != btnDisplay_[static_cast<size_t>(i)]) {
                btnDisplay_[static_cast<size_t>(i)] = st;
                if (i < 6) panelChanged = true;
                if (st && engine_ && engine_->slot()) {
                    switch (i) {
                        case 6: octave_.store(std::max(-2, octave_.load() - 1)); break;
                        case 7: octave_.store(std::min(2, octave_.load() + 1)); break;
                    }
                }
                changed = true;
            }
        }
        if (panelChanged && engine_) {
            const auto focus = engine_->focus();
            auto* slot = engine_->slot(focus);
            const auto* host = engine_->host(focus);
            if (slot && host) {
                const int roles[] = {FM1_B_HOME, FM1_B_GLO, FM1_B_EDIT, FM1_B_FX, FM1_B_ARP, FM1_B_SEQ};
                uint32_t mask = (playNow ? 1u << FM1_B_PLAY : 0u) | (recNow ? 1u << FM1_B_REC : 0u);
                for (int i = 0; i < 6; ++i)
                    if (btnDisplay_[static_cast<size_t>(i)]) mask |= 1u << roles[i];
                slot->pushButtons(engine_->link()->filterButtons(mask, host->caps()));
            }
        }

        // Encoder Touch (buf[10])
        for (int i = 0; i < 8; ++i) {
            const bool t = (buf[10] & (1 << (7 - i))) != 0;
            if (t != encoderTouch_[static_cast<size_t>(i)]) {
                encoderTouch_[static_cast<size_t>(i)] = t;
                changed = true;
            }
        }

        // 8 Continuous Encoders (Knobs 8 downto 1 at bytes 12..27)
        for (int i = 0; i < 8; ++i) {
            const int offset = 12 + i * 2;
            if (offset + 1 < len) {
                const uint16_t raw = buf[offset] | (buf[offset + 1] << 8);
                const int encIdx = 7 - i;
                const uint16_t oldVal = encoderVals_[static_cast<size_t>(encIdx)];
                if (raw != oldVal) {
                    encoderVals_[static_cast<size_t>(encIdx)] = raw;
                    int16_t diff = static_cast<int16_t>(raw - oldVal);
                    if (diff > 512) diff -= 1024;
                    else if (diff < -512) diff += 1024;

                    if (engine_ && engine_->slot()) {
                        if (encIdx < 4) {
                            engine_->slot()->pushEnc(FM1_E_K1 + encIdx, diff);
                        } else if (encIdx == 4) {
                            engine_->slot()->pushEnc(FM1_E_SELECT, diff);
                        } else if (encIdx == 5) {
                            engine_->slot()->pushEnc(FM1_E_ALGO, diff);
                        } else if (encIdx == 6) {
                            engine_->slot()->pushEnc(FM1_E_PRESET, diff);
                        } else if (encIdx == 7) {
                            // Knob 8: Master Volume ADC
                            engine_->slot()->setMaster(raw);
                        }
                    }
                    changed = true;
                }
            }
        }
    }

    // Report 0x02: Analog Velocity / Pressure Pads
    else if (repId == 0x02 && len >= 4) {
        bool activePads[16] = {false};
        const int slots = std::min((len - 1) / 3, 16);

        for (int sIdx = 0; sIdx < slots; ++sIdx) {
            const int off = 1 + sIdx * 3;
            const uint8_t rawPad = buf[off];
            const uint16_t val = buf[off + 1] | (buf[off + 2] << 8);

            if ((val & 0xF000) == 0x4000) {
                const uint16_t vel = val & 0x0FFF;
                const int p = rawPad % 16;
                activePads[p] = true;

                if (!pads_[p].pressed) {
                    pads_[p].pressed = true;
                    pads_[p].velocity = vel;
                    pads_[p].color = LED_WHITE;

                    // Send MIDI Note-On
                    const int baseNote = 48 + octave_.load() * 12;
                    const int note = baseNote + p;
                    const uint32_t midiVel = std::max(1u, static_cast<uint32_t>((vel * 127) / 4095));
                    if (engine_ && engine_->slot()) {
                        engine_->slot()->pushMidi(0x90, note, midiVel);
                    }
                    changed = true;
                }
            }
        }

        // Release pads not in report
        for (int p = 0; p < 16; ++p) {
            if (!activePads[p] && pads_[p].pressed) {
                pads_[p].pressed = false;
                pads_[p].color = isAccidental(p) ? LED_BLUE : LED_CYAN;

                // Send MIDI Note-Off
                const int baseNote = 48 + octave_.load() * 12;
                const int note = baseNote + p;
                if (engine_ && engine_->slot()) {
                    engine_->slot()->pushMidi(0x80, note, 0);
                }
                changed = true;
            }
        }
    }

    return changed;
}

void Mk3Bridge::threadMain() {
    LOGI("Mk3Bridge worker loop entering");
    char devPath[128] = {0};
    uint8_t inBuf[64];
    uint32_t loopTicks = 0;

    while (running_.load(std::memory_order_relaxed)) {
        if (usbFd_ < 0) {
            if (findAndOpenDevice(devPath, sizeof(devPath)) >= 0) {
                connected_.store(true, std::memory_order_release);
                // First frame and LEDs ignition
                pushButtonLeds();
                pushPadsLeds();
                renderLeftScreen(screen0Pixels_.get());
                flushScreen(0, screen0Pixels_.get());
                memcpy(prevScreen0Pixels_.get(), screen0Pixels_.get(), PIXEL_BYTES);
                renderRightScreen(screen1Pixels_.get());
                flushScreen(1, screen1Pixels_.get());
                memcpy(prevScreen1Pixels_.get(), screen1Pixels_.get(), PIXEL_BYTES);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        // Poll Endpoint 0x83 (Interrupt IN, 20ms timeout)
        struct usbdevfs_bulktransfer bulk;
        memset(&bulk, 0, sizeof(bulk));
        bulk.ep = EP_IN_INPUT;
        bulk.len = sizeof(inBuf);
        bulk.timeout = 20;
        bulk.data = inBuf;

        int ret = ioctl(usbFd_, USBDEVFS_BULK, &bulk);
        bool needsRender = false;

        if (ret > 0) {
            if (processInputPacket(inBuf, ret)) {
                needsRender = true;
            }
        } else if (ret < 0) {
            if (errno == ENODEV || errno == ENOENT || errno == ESHUTDOWN) {
                LOGW("EP 0x83 fatal disconnect (errno %d); disconnecting for reconnect", errno);
                disconnectDevice();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            // ETIMEDOUT, EBUSY, EAGAIN are normal non-fatal polling states
        }

        loopTicks++;

        // Push LEDs whenever input state changed or periodically (every ~100ms)
        if (needsRender || (loopTicks % 5 == 0)) {
            pushButtonLeds();
            pushPadsLeds();
        }

        // Render Right Screen (Parameters and Pads visualizer, immediate on hit/turn, periodic ~300ms)
        if (needsRender || (loopTicks % 15 == 0)) {
            renderRightScreen(screen1Pixels_.get());
            if (memcmp(screen1Pixels_.get(), prevScreen1Pixels_.get(), PIXEL_BYTES) != 0) {
                flushScreen(1, screen1Pixels_.get());
                memcpy(prevScreen1Pixels_.get(), screen1Pixels_.get(), PIXEL_BYTES);
            }
        }

        // Render Left Screen (live firmware 240x240 display + rack meters, paced ~8-10 FPS every 120ms)
        if (loopTicks % 6 == 0) {
            renderLeftScreen(screen0Pixels_.get());
            if (memcmp(screen0Pixels_.get(), prevScreen0Pixels_.get(), PIXEL_BYTES) != 0) {
                flushScreen(0, screen0Pixels_.get());
                memcpy(prevScreen0Pixels_.get(), screen0Pixels_.get(), PIXEL_BYTES);
            }
        }
    }

    disconnectDevice();
    LOGI("Mk3Bridge worker loop exited");
}

} // namespace mk3
