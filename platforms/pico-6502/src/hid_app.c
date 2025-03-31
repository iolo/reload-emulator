#include "tusb.h"
#include "class/hid/hid.h"

#define GAMEPAD_MAX_DEVICES 2

typedef struct {
    uint16_t id;
    uint32_t type;
    uint8_t index;
    uint8_t hat_state;
    uint32_t button_state;
} gamepad_t;

uint8_t const keycode_to_ascii_table[128][2] = {HID_KEYCODE_TO_ASCII};

static hid_keyboard_report_t prev_report = {0, 0, {0}};

static gamepad_t gamepads[GAMEPAD_MAX_DEVICES];
static int8_t gamepads_count = 0;

extern void kbd_raw_key_down(int code);
extern void kbd_raw_key_up(int code);
extern void gamepad_state_update(uint8_t index, uint8_t hat_state, uint32_t button_state);

static inline bool find_key_in_report(hid_keyboard_report_t const* report, uint8_t keycode) {
    for (uint8_t i = 0; i < 6; i++) {
        if (report->keycode[i] == keycode) {
            return true;
        }
    }
    return false;
}

static void process_kbd_report(hid_keyboard_report_t const* r1, hid_keyboard_report_t const* r2,
                               void (*kbd_raw_key_cb)(int code)) {
    // Left GUI modifier
    if (r1->modifier & KEYBOARD_MODIFIER_LEFTGUI && !(r2->modifier & KEYBOARD_MODIFIER_LEFTGUI)) {
        kbd_raw_key_cb(HID_KEY_GUI_LEFT | 0x100);
    }

    // Right GUI modifier
    if (r1->modifier & KEYBOARD_MODIFIER_RIGHTGUI && !(r2->modifier & KEYBOARD_MODIFIER_RIGHTGUI)) {
        kbd_raw_key_cb(HID_KEY_GUI_RIGHT | 0x100);
    }

    // Process keycodes
    for (int i = 0; i < 6; i++) {
        if (r1->keycode[i]) {
            if (!find_key_in_report(r2, r1->keycode[i])) {
                bool is_shift = r1->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
                bool is_ctrl = r1->modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL);
                uint8_t keycode = r1->keycode[i];
                int code = keycode_to_ascii_table[keycode][is_shift];
                if (code == 0 && keycode != 0) {
                    code = keycode | 0x100;
                } else if (is_ctrl) {
                    code &= ~0x60;
                }
                kbd_raw_key_cb(code);
            }
        }
    }
}

static void find_pressed_keys(hid_keyboard_report_t const* report) {
    process_kbd_report(report, &prev_report, &kbd_raw_key_down);
}

static void find_released_keys(hid_keyboard_report_t const* report) {
    process_kbd_report(&prev_report, report, &kbd_raw_key_up);
}

static gamepad_t* find_gamepad(uint16_t id) {
    for (int i = 0; i < gamepads_count; i++) {
        if (gamepads[i].id == id) {
            return &gamepads[i];
        }
    }
    return NULL;
}

// Support for Xbox One controller (04284001)
static uint8_t get_hat_state_04284001(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[2] & 0x0F;
    
    switch (dpad) {
        case 0x1:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0xC:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x9:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x0:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_04284001(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[3] & 0x10) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[3] & 0x20) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[3] & 0x40) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[3] & 0x80) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[3] & 0x01) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[3] & 0x02) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

// Support for PS3 controller (05832060)
static uint8_t get_hat_state_05832060(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[2] & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_05832060(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[3] & 0x40) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[3] & 0x20) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[3] & 0x10) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[3] & 0x80) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[3] & 0x08) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[3] & 0x04) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

// Support for PS4 controller (054C0CDA)
static uint8_t get_hat_state_054C0CDA(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[5] & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_054C0CDA(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[5] & 0x10) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[5] & 0x20) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[5] & 0x40) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[5] & 0x80) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[6] & 0x01) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[6] & 0x02) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

static uint8_t get_hat_state_0079181C(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[2] & 0xF;

    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }

    return hat_state;
}

static uint32_t get_button_state_0079181C(uint8_t const* report) {
    uint32_t button_state = 0;

    if (report[0] & 0x01) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[0] & 0x02) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[0] & 0x08) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[0] & 0x10) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[0] & 0x40) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[0] & 0x80) {
        button_state |= GAMEPAD_BUTTON_TR;
    }

    return button_state;
}

// Support for 8BitDo SN30 Pro controller (007918D2)
static uint8_t get_hat_state_007918D2(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[2] & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_007918D2(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[0] & 0x01) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[0] & 0x02) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[0] & 0x08) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[0] & 0x10) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[0] & 0x40) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[0] & 0x80) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

// Support for Switch Pro controller (07382217)
static uint8_t get_hat_state_07382217(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = (report[5] >> 4) & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_07382217(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[3] & 0x04) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[3] & 0x02) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[3] & 0x08) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[3] & 0x01) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[3] & 0x40) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[3] & 0x80) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

static uint8_t get_hat_state_081FE401(uint8_t const* report) {
    uint8_t hat_state = 0;

    switch (report[0] << 8 | report[1]) {
        case 0x7F7F:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;

        case 0x7F00:
            hat_state = GAMEPAD_HAT_UP;
            break;

        case 0xFF00:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;

        case 0xFF7F:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;

        case 0xFFFF:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;

        case 0x7FFF:
            hat_state = GAMEPAD_HAT_DOWN;
            break;

        case 0x00FF:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;

        case 0x007F:
            hat_state = GAMEPAD_HAT_LEFT;
            break;

        case 0x0000:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;

        default:
            break;
    }

    return hat_state;
}

static uint32_t get_button_state_081FE401(uint8_t const* report) {
    uint32_t button_state = 0;

    if (report[5] & 0x20) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[5] & 0x40) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[5] & 0x10) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[5] & 0x80) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[6] & 0x01) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[6] & 0x02) {
        button_state |= GAMEPAD_BUTTON_TR;
    }

    return button_state;
}

// Support for Generic USB controller (1C59002X)
static uint8_t get_hat_state_1C59002X(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[0] & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_1C59002X(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[1] & 0x01) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[1] & 0x02) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[1] & 0x04) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[1] & 0x08) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[1] & 0x10) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[1] & 0x20) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

static void process_gamepad_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    uint16_t id = dev_addr << 8 | instance;
    gamepad_t* gamepad = find_gamepad(id);

    switch (gamepad->type) {
        case 0x04284001: // Xbox One controller
            if (len != 11) {
                return;
            }
            gamepad->hat_state = get_hat_state_04284001(report);
            gamepad->button_state = get_button_state_04284001(report);
            break;
            
        case 0x05832060: // PS3 controller
            if (len != 8) {
                return;
            }
            gamepad->hat_state = get_hat_state_05832060(report);
            gamepad->button_state = get_button_state_05832060(report);
            break;
            
        case 0x054C0CDA: // PS4 controller
            if (len != 10) {
                return;
            }
            gamepad->hat_state = get_hat_state_054C0CDA(report);
            gamepad->button_state = get_button_state_054C0CDA(report);
            break;
            
        case 0x0079181C:
            if (len != 9) {
                return;
            }
            gamepad->hat_state = get_hat_state_0079181C(report);
            gamepad->button_state = get_button_state_0079181C(report);
            break;
            
        case 0x007918D2: // 8BitDo SN30 Pro
            if (len != 9) {
                return;
            }
            gamepad->hat_state = get_hat_state_007918D2(report);
            gamepad->button_state = get_button_state_007918D2(report);
            break;
            
        case 0x07382217: // Switch Pro controller
            if (len != 10) {
                return;
            }
            gamepad->hat_state = get_hat_state_07382217(report);
            gamepad->button_state = get_button_state_07382217(report);
            break;
            
        case 0x081FE401:
            if (len != 8) {
                return;
            }
            gamepad->hat_state = get_hat_state_081FE401(report);
            gamepad->button_state = get_button_state_081FE401(report);
            break;

        case 0x1C590020: // Generic USB controller
        case 0x1C590021:
        case 0x1C590022:
        case 0x1C590023:
        case 0x1C590024:
        case 0x1C590025:
            if (len != 6) {
                return;
            }
            gamepad->hat_state = get_hat_state_1C59002X(report);
            gamepad->button_state = get_button_state_1C59002X(report);
            break;

        default:
            return;
    }

    gamepad_state_update(gamepad->index, gamepad->hat_state, gamepad->button_state);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_len;
    (void)desc_report;

    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    switch (itf_protocol) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            break;

        case HID_ITF_PROTOCOL_NONE:
            if (gamepads_count < GAMEPAD_MAX_DEVICES) {
                gamepad_t* gamepad = &gamepads[gamepads_count];
                gamepad->id = dev_addr << 8 | instance;
                gamepad->type = vid << 16 | pid;
                gamepad->index = gamepads_count;
                gamepad->hat_state = 0;
                gamepad->button_state = 0;
                gamepads_count++;

                printf("Gamepad connected: VID: %04X PID: %04X\n", vid, pid);
            }
            break;

        default:
            break;
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    (void)instance;
    (void)len;

    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    switch (itf_protocol) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            find_pressed_keys((const hid_keyboard_report_t*)report);
            find_released_keys((const hid_keyboard_report_t*)report);
            memcpy(&prev_report, report, sizeof(hid_keyboard_report_t));
            break;

        case HID_ITF_PROTOCOL_NONE:
            process_gamepad_report(dev_addr, instance, report, len);
            break;

        default:
            break;
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr;
    (void)instance;
}
