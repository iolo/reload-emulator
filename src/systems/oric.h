#pragma once

// oric.h
//
// Oric emulator in a C header.
//
// Do this:
// ~~~C
// #define CHIPS_IMPL
// ~~~
// before you include this file in *one* C or C++ file to create the
// implementation.
//
// Optionally provide the following macros with your own implementation
//
// ~~~C
// CHIPS_ASSERT(c)
// ~~~
//     your own assert macro (default: assert(c))
//
// You need to include the following headers before including oric.h:
//
// - chips/chips_common.h
// - chips/wdc65C02cpu.h | chips/mos6502cpu.h
// - chips/mos6522via.h
// - chips/ay38910psg.h
// - chips/kbd.h
// - chips/mem.h
// - chips/clk.h
// - systems/oric_fdd.h
// - systems/oric_fdc.h
// - systems/oric_fdc_rom.h
// - systems/oric_td.h
//
// ## The Oric
//
//
// TODO!
//
// ## Links
//
// ## zlib/libpng license
//
// Copyright (c) 2023 Veselin Sladkov
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the
// use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//     1. The origin of this software must not be misrepresented; you must not
//     claim that you wrote the original software. If you use this software in a
//     product, an acknowledgment in the product documentation would be
//     appreciated but is not required.
//     2. Altered source versions must be plainly marked as such, and must not
//     be misrepresented as being the original software.
//     3. This notice may not be removed or altered from any source
//     distribution.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bump snapshot version when oric_t memory layout changes
#define ORIC_SNAPSHOT_VERSION (1)

#define ORIC_FREQUENCY     (1000000)  // 1 MHz
#define ORIC_MAX_TAPE_SIZE (1 << 16)  // Max size of tape file in bytes

#define ORIC_SCREEN_WIDTH     240  // (240)
#define ORIC_SCREEN_HEIGHT    224  // (224)
#define ORIC_FRAMEBUFFER_SIZE ((ORIC_SCREEN_WIDTH / 2) * ORIC_SCREEN_HEIGHT)

#define PALETTE_BITS 3
#define PALETTE_SIZE (1 << PALETTE_BITS)

#ifdef PICO_RP2040
#define RGBA8(r, g, b) (0xFF000000 | (r << 16) | (g << 8) | (b))
#else
#define RGBA8(b, g, r) (0xFF000000 | (r << 16) | (g << 8) | (b))
#endif

static const uint32_t oric_palette[PALETTE_SIZE] = {
    RGBA8(0x00, 0x00, 0x00), /* black */
    RGBA8(0xFF, 0x00, 0x00), /* red */
    RGBA8(0x00, 0xFF, 0x00), /* green */
    RGBA8(0xFF, 0xFF, 0x00), /* yellow */
    RGBA8(0x00, 0x00, 0xFF), /* blue */
    RGBA8(0xFF, 0x00, 0xFF), /* magenta */
    RGBA8(0x00, 0xFF, 0xFF), /* cyan */
    RGBA8(0xFF, 0xFF, 0xFF), /* white */
};

// Config parameters for oric_init()
typedef struct {
    bool td_enabled;      // Set to true to enable tape drive emulation
    bool fdc_enabled;     // Set to true to enable floppy disk controller emulation
    chips_debug_t debug;  // Optional debugging hook
    chips_audio_desc_t audio;
    struct {
        chips_range_t rom;
        chips_range_t boot_rom;
    } roms;
} oric_desc_t;

// Oric emulator state
typedef struct {
    MOS6502CPU_T cpu;
    mos6522via_t via;
    ay38910psg_t psg;
    kbd_t kbd;
    mem_t mem;
    bool valid;
    chips_debug_t debug;

    chips_audio_callback_t audio_callback;

    uint8_t ram[0xC000];
    uint8_t overlay_ram[0x4000];
    uint8_t* rom;
    uint8_t* boot_rom;

    int blink_counter;
    uint8_t pattr;

    uint8_t reserved[3];
    uint8_t fb[ORIC_FRAMEBUFFER_SIZE];
    bool screen_dirty;

    uint16_t extension;

    oric_td_t td;  // Tape drive

    disk2_fdc_t fdc;  // Disk II floppy disk controller

    uint32_t system_ticks;

} oric_t;

// Oric interface

// Initialize a new Oric instance
void oric_init(oric_t* sys, const oric_desc_t* desc);
// Discard Oric instance
void oric_discard(oric_t* sys);
// Reset a Oric instance
void oric_reset(oric_t* sys);

void oric_tick(oric_t* sys);

// Tick Oric instance for a given number of microseconds, return number of executed ticks
uint32_t oric_exec(oric_t* sys, uint32_t micro_seconds);
// Take a snapshot, patches pointers to zero or offsets, returns snapshot version
uint32_t oric_save_snapshot(oric_t* sys, oric_t* dst);
// Load a snapshot, returns false if snapshot version doesn't match
bool oric_load_snapshot(oric_t* sys, uint32_t version, oric_t* src);

void oric_screen_update(oric_t* sys);

#ifdef __cplusplus
}  // extern "C"
#endif

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h> /* memcpy, memset */
#ifndef CHIPS_ASSERT
#include <assert.h>
#define CHIPS_ASSERT(c) assert(c)
#endif

static void _oric_psg_out(int port_id, uint8_t data, void* user_data);
static uint8_t _oric_psg_in(int port_id, void* user_data);
static void _oric_init_memorymap(oric_t* sys);
static void _oric_init_key_map(oric_t* sys);

#define PATTR_50HZ  (0x02)
#define PATTR_HIRES (0x04)
#define LATTR_ALT   (0x01)
#define LATTR_DSIZE (0x02)
#define LATTR_BLINK (0x04)

void oric_init(oric_t* sys, const oric_desc_t* desc) {
    CHIPS_ASSERT(sys && desc);
    if (desc->debug.callback.func) {
        CHIPS_ASSERT(desc->debug.stopped);
    }

    memset(sys, 0, sizeof(oric_t));
    sys->valid = true;
    sys->debug = desc->debug;
    sys->audio_callback = desc->audio.callback;

    CHIPS_ASSERT(desc->roms.rom.ptr && (desc->roms.rom.size == 0x4000));
    CHIPS_ASSERT(desc->roms.boot_rom.ptr && (desc->roms.boot_rom.size == 0x200));
    sys->rom = desc->roms.rom.ptr;
    sys->boot_rom = desc->roms.boot_rom.ptr;

    MOS6502CPU_INIT(&sys->cpu, &(MOS6502CPU_DESC_T){0});

    mos6522via_init(&sys->via);
    ay38910psg_init(&sys->psg, &(ay38910psg_desc_t){.type = AY38910PSG_TYPE_8912,
                                                    .in_cb = _oric_psg_in,
                                                    .out_cb = _oric_psg_out,
                                                    .magnitude = CHIPS_DEFAULT(desc->audio.volume, 1.0f),
                                                    .user_data = sys});

    // setup memory map and keyboard matrix
    _oric_init_memorymap(sys);
    _oric_init_key_map(sys);

    sys->blink_counter = 0;
    sys->pattr = 0;

    sys->extension = 0;

    // Optionally setup tape drive
    if (desc->td_enabled) {
        oric_td_init(&sys->td);
    }

    // Optionally setup floppy disk controller
    if (desc->fdc_enabled) {
        disk2_fdc_init(&sys->fdc);
        if (CHIPS_ARRAY_SIZE(oric_nib_images) > 0) {
            disk2_fdd_insert_disk(&sys->fdc.fdd[0], oric_nib_images[0]);
        }
    }
}

void oric_discard(oric_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    if (sys->fdc.valid) {
        disk2_fdc_discard(&sys->fdc);
    }
    if (sys->td.valid) {
        oric_td_discard(&sys->td);
    }
    sys->valid = false;
}

void oric_nmi(oric_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    MOS6502CPU_NMI(&sys->cpu);
}

void oric_reset(oric_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    mos6522via_reset(&sys->via);
    ay38910psg_reset(&sys->psg);
    if (sys->fdc.valid) {
        disk2_fdc_reset(&sys->fdc);
    }
    if (sys->td.valid) {
        oric_td_reset(&sys->td);
    }
    MOS6502CPU_RESET(&sys->cpu);
}

static void _oric_mem_rw(oric_t* sys, uint16_t addr, bool rw) {
    if ((addr >= 0x0300) && (addr <= 0x03FF)) {
        // Memory-mapped IO area
        if ((addr >= 0x0300) && (addr <= 0x030F)) {
            if (rw) {
                MOS6502CPU_SET_DATA(&sys->cpu, mos6522via_read(&sys->via, addr & 0xF));
            } else {
                mos6522via_write(&sys->via, addr & 0xF, MOS6502CPU_GET_DATA(&sys->cpu));
            }
        } else if ((addr >= 0x0310) && (addr <= 0x031F)) {
            if (sys->fdc.valid) {
                // Disk II FDC
                if (rw) {
                    // Memory read
                    MOS6502CPU_SET_DATA(&sys->cpu, disk2_fdc_read_byte(&sys->fdc, addr & 0xF));
                } else {
                    // Memory write
                    disk2_fdc_write_byte(&sys->fdc, addr & 0xF, MOS6502CPU_GET_DATA(&sys->cpu));
                }
            } else {
                if (rw) {
                    MOS6502CPU_SET_DATA(&sys->cpu, 0x00);
                }
            }
        } else if ((addr >= 0x0320) && (addr <= 0x03FF)) {
            if (sys->fdc.valid) {
                // Disk II boot rom
                if (rw) {
                    // Memory read
                    MOS6502CPU_SET_DATA(&sys->cpu, sys->boot_rom[(addr & 0xFF) + sys->extension]);
                } else {
                    // Memory write
                    switch (addr) {
                        case 0x380:
                            mem_map_rw(&sys->mem, 0, 0xC000, 0x4000, sys->rom, sys->overlay_ram);
                            sys->extension = 0;
                            break;

                        case 0x381:
                            mem_map_ram(&sys->mem, 0, 0xC000, 0x4000, sys->overlay_ram);
                            sys->extension = 0;
                            break;

                        case 0x382:
                            mem_map_rw(&sys->mem, 0, 0xC000, 0x4000, sys->rom, sys->overlay_ram);
                            sys->extension = 0x100;
                            break;

                        case 0x383:
                            mem_map_ram(&sys->mem, 0, 0xC000, 0x4000, sys->overlay_ram);
                            sys->extension = 0x100;
                            break;

                        default:
                            break;
                    }
                }
            } else {
                if (rw) {
                    MOS6502CPU_SET_DATA(&sys->cpu, 0x00);
                }
            }
        }
    } else {
        // Regular memory access
        if (rw) {
            // Memory read
            MOS6502CPU_SET_DATA(&sys->cpu, mem_rd(&sys->mem, addr));
        } else {
            // Memory write
            mem_wr(&sys->mem, addr, MOS6502CPU_GET_DATA(&sys->cpu));

            if (addr >= 0x9800 && addr <= 0xBFDF) {
                sys->screen_dirty = true;
            }
        }
    }
}

static uint8_t _last_motor_state = 0;

void oric_tick(oric_t* sys) {
    MOS6502CPU_TICK(&sys->cpu);

    _oric_mem_rw(sys, sys->cpu.addr, sys->cpu.rw);

    // Tick PSG
    if ((sys->system_ticks & 63) == 0) {
        ay38910psg_tick_channels(&sys->psg);
    }

    if ((sys->system_ticks & 127) == 0) {
        ay38910psg_tick_envelope_generator(&sys->psg);
    }

    static uint8_t t1 = 0;
    t1++;
    if (t1 == 46) {
        ay38910psg_tick_sample_generator(&sys->psg);
        if (sys->audio_callback.func) {
            // New sample is ready
            sys->audio_callback.func((uint8_t)((uint8_t)(sys->psg.sample * 255.0f)), sys->audio_callback.user_data);
        }
        t1 = 0;
    }

    // Tick FDC
    if (sys->fdc.valid && (sys->system_ticks & 127) == 0) {
        disk2_fdc_tick(&sys->fdc);
    }

    // Tick VIA
    if ((sys->system_ticks & 3) == 0) {
        MOS6502CPU_SET_IRQ(&sys->cpu, mos6522via_tick(&sys->via, 4));

        // Update PSG state
        if (mos6522via_get_cb2(&sys->via)) {
            const uint8_t psg_data = mos6522via_get_pa(&sys->via);
            if (mos6522via_get_ca2(&sys->via)) {
                ay38910psg_latch_address(&sys->psg, psg_data);
            } else {
                ay38910psg_write(&sys->psg, psg_data);
            }
        }

        if (!mos6522via_get_cb2(&sys->via)) {
            mos6522via_set_pa(&sys->via, ay38910psg_read(&sys->psg));
        }

        // PB0..PB2: select keyboard matrix line
        uint8_t pb = mos6522via_get_pb(&sys->via);
        uint8_t line = pb & 7;
        if (line >= 0 && line <= 7) {
            uint8_t line_mask = 1 << line;
            if (kbd_scan_lines(&sys->kbd) == line_mask) {
                mos6522via_set_pb(&sys->via, pb | (1 << 3));
            } else {
                mos6522via_set_pb(&sys->via, pb & ~(1 << 3));
            }
        }

        if (sys->td.valid) {
            uint8_t motor_state = pb & 0x40;
            if (motor_state != _last_motor_state) {
                if (motor_state) {
                    sys->td.port |= ORIC_TD_PORT_MOTOR;
                    printf("oric: motor on\n");
                } else {
                    sys->td.port &= ~ORIC_TD_PORT_MOTOR;
                    printf("oric: motor off\n");
                }
                _last_motor_state = motor_state;
            }

            static uint8_t t2 = 0;
            t2++;
            if (t2 == 52) {
                oric_td_tick(&sys->td);
                t2 = 0;
            }
            if (sys->td.port & ORIC_TD_PORT_READ) {
                mos6522via_set_cb1(&sys->via, true);
            } else {
                mos6522via_set_cb1(&sys->via, false);
            }
        }
    }

    sys->system_ticks++;
}

// PSG OUT callback (nothing to do here)
static void _oric_psg_out(int port_id, uint8_t data, void* user_data) {
    oric_t* sys = (oric_t*)user_data;
    if (port_id == AY38910PSG_PORT_A) {
        kbd_set_active_columns(&sys->kbd, data ^ 0xFF);
    } else {
        // This shouldn't happen since the AY-3-8912 only has one IO port
    }
}

// PSG IN callback (read keyboard matrix)
static uint8_t _oric_psg_in(int port_id, void* user_data) {
    // this shouldn't be called
    (void)port_id;
    (void)user_data;
    return 0xFF;
}

void oric_screen_update(oric_t* sys) {
    if (!sys->screen_dirty) {
        return;
    }

    bool blink_state = sys->blink_counter & 0x20;
    sys->blink_counter = (sys->blink_counter + 1) & 0x3F;

    uint8_t pattr = sys->pattr;

    for (int y = 0; y < 224; y++) {
        // Line attributes and current colors
        uint8_t lattr = 0;
        uint8_t fgcol = 7;
        uint8_t bgcol = 0;

        uint8_t* p = &sys->fb[y * (ORIC_SCREEN_WIDTH / 2)];

        for (int x = 0; x < 40; x++) {
            // Lookup the byte and, if needed, the pattern data
            uint8_t ch, pat;
            if ((pattr & PATTR_HIRES) && y < 200)
                ch = pat = sys->ram[0xA000 + y * 40 + x];

            else {
                ch = sys->ram[0xBB80 + (y >> 3) * 40 + x];
                int off = (lattr & LATTR_DSIZE ? y >> 1 : y) & 7;
                const uint8_t* base;
                if (pattr & PATTR_HIRES)
                    if (lattr & LATTR_ALT)
                        base = sys->ram + 0x9C00;
                    else
                        base = sys->ram + 0x9800;
                else if (lattr & LATTR_ALT)
                    base = sys->ram + 0xB800;
                else
                    base = sys->ram + 0xB400;
                pat = base[((ch & 0x7F) << 3) | off];
            }

            // Handle state-chaging attributes
            if (!(ch & 0x60)) {
                pat = 0x00;
                switch (ch & 0x18) {
                    case 0x00:
                        fgcol = ch & 7;
                        break;
                    case 0x08:
                        lattr = ch & 7;
                        break;
                    case 0x10:
                        bgcol = ch & 7;
                        break;
                    case 0x18:
                        pattr = ch & 7;
                        break;
                }
            }

            // Pick up the colors for the pattern
            uint8_t c_fgcol = fgcol;
            uint8_t c_bgcol = bgcol;

            // inverse video
            if (ch & 0x80) {
                c_bgcol = c_bgcol ^ 0x07;
                c_fgcol = c_fgcol ^ 0x07;
            }
            // blink
            if ((lattr & LATTR_BLINK) && blink_state) c_fgcol = c_bgcol;

            // Draw the pattern
            uint8_t c;
            c = pat & 0x20 ? c_fgcol : c_bgcol;
            *p = c << 4;
            c = pat & 0x10 ? c_fgcol : c_bgcol;
            *p++ |= c;
            c = pat & 0x08 ? c_fgcol : c_bgcol;
            *p = c << 4;
            c = pat & 0x04 ? c_fgcol : c_bgcol;
            *p++ |= c;
            c = pat & 0x02 ? c_fgcol : c_bgcol;
            *p = c << 4;
            c = pat & 0x01 ? c_fgcol : c_bgcol;
            *p++ |= c;
        }
    }

    sys->pattr = pattr;

    sys->screen_dirty = false;
}

uint32_t oric_exec(oric_t* sys, uint32_t micro_seconds) {
    CHIPS_ASSERT(sys && sys->valid);
    uint32_t num_ticks = clk_us_to_ticks(ORIC_FREQUENCY, micro_seconds);
    if (0 == sys->debug.callback.func) {
        // run without debug callback
        for (uint32_t ticks = 0; ticks < num_ticks; ticks++) {
            oric_tick(sys);
        }
    } else {
        // run with debug callback
        for (uint32_t ticks = 0; (ticks < num_ticks) && !(*sys->debug.stopped); ticks++) {
            oric_tick(sys);
            sys->debug.callback.func(sys->debug.callback.user_data, 0);
        }
    }
    kbd_update(&sys->kbd, micro_seconds);
    oric_screen_update(sys);
    return num_ticks;
}

static void _oric_init_memorymap(oric_t* sys) {
    mem_init(&sys->mem);
    memset(sys->ram, 0, sizeof(sys->ram));
    memset(sys->overlay_ram, 0, sizeof(sys->overlay_ram));
    mem_map_ram(&sys->mem, 0, 0x0000, 0xC000, sys->ram);
    mem_map_rw(&sys->mem, 0, 0xC000, 0x4000, sys->rom, sys->overlay_ram);
}

static void _oric_init_key_map(oric_t* sys) {
    kbd_init(&sys->kbd, 2);
    const char* keymap =
        // no shift
        //   01234567 (col)
        "7N5V 1X3"   // row 0
        "JTRF  QD"   // row 1
        "M6B4 Z2C"   // row 2
        "K9;-  \\'"  // row 3
        " <>     "   // row 4
        "UIOP  ]["   // row 5
        "YHGE ASW"   // row 6
        "8L0/   ="   // row 7

        /* shift */
        "&n%v !x#"
        "jtrf  qd"
        "m^b$ z@c"
        "k(:_  |\""
        " ,.     "
        "uiop  }{"
        "yhge asw"
        "*l)?   +";

    CHIPS_ASSERT(strlen(keymap) == 128);
    // shift is column 4, line 4
    kbd_register_modifier(&sys->kbd, 0, 4, 4);
    // ctrl is column 4, line 2
    kbd_register_modifier(&sys->kbd, 1, 4, 2);
    for (int shift = 0; shift < 2; shift++) {
        for (int column = 0; column < 8; column++) {
            for (int line = 0; line < 8; line++) {
                int c = keymap[shift * 64 + line * 8 + column];
                if (c != 0x20) {
                    kbd_register_key(&sys->kbd, c, column, line, shift ? (1 << 0) : 0);
                }
            }
        }
    }

    // Special keys
    kbd_register_key(&sys->kbd, 0x20, 0, 4, 0);   // Space
    kbd_register_key(&sys->kbd, 0x150, 5, 4, 0);  // Left
    kbd_register_key(&sys->kbd, 0x14F, 7, 4, 0);  // Right
    kbd_register_key(&sys->kbd, 0x151, 6, 4, 0);  // Down
    kbd_register_key(&sys->kbd, 0x152, 3, 4, 0);  // Up
    kbd_register_key(&sys->kbd, 0x08, 5, 5, 0);   // Delete
    kbd_register_key(&sys->kbd, 0x0D, 5, 7, 0);   // Return

    kbd_register_key(&sys->kbd, 0x14, 1, 1, 2);  // Ctrl+T
    kbd_register_key(&sys->kbd, 0x10, 3, 5, 2);  // Ctrl+P
    kbd_register_key(&sys->kbd, 0x06, 3, 1, 2);  // Ctrl+F
    kbd_register_key(&sys->kbd, 0x04, 7, 1, 2);  // Ctrl+D
    kbd_register_key(&sys->kbd, 0x11, 6, 1, 2);  // Ctrl+Q
    kbd_register_key(&sys->kbd, 0x13, 6, 6, 2);  // Ctrl+S
    kbd_register_key(&sys->kbd, 0x0C, 1, 7, 2);  // Ctrl+L
    kbd_register_key(&sys->kbd, 0x0E, 1, 0, 2);  // Ctrl+N
}

void oric_key_up(oric_t* sys, int key_code) {
    CHIPS_ASSERT(sys && sys->valid);
    kbd_key_up(&sys->kbd, key_code);
}

uint32_t oric_save_snapshot(oric_t* sys, oric_t* dst) {
    CHIPS_ASSERT(sys && dst);
    *dst = *sys;
    chips_debug_snapshot_onsave(&dst->debug);
    chips_audio_callback_snapshot_onsave(&dst->audio_callback);
    // m6502_snapshot_onsave(&dst->cpu);
    ay38910psg_snapshot_onsave(&dst->psg);
    oric_td_snapshot_onsave(&dst->td);
    disk2_fdc_snapshot_onsave(&dst->fdc);
    mem_snapshot_onsave(&dst->mem, sys);
    return ORIC_SNAPSHOT_VERSION;
}

bool oric_load_snapshot(oric_t* sys, uint32_t version, oric_t* src) {
    CHIPS_ASSERT(sys && src);
    if (version != ORIC_SNAPSHOT_VERSION) {
        return false;
    }
    static oric_t im;
    im = *src;
    chips_debug_snapshot_onload(&im.debug, &sys->debug);
    chips_audio_callback_snapshot_onload(&im.audio_callback, &sys->audio_callback);
    // m6502_snapshot_onload(&im.cpu, &sys->cpu);
    ay38910psg_snapshot_onload(&im.psg, &sys->psg);
    oric_td_snapshot_onload(&im.td, &sys->td);
    disk2_fdc_snapshot_onload(&im.fdc, &sys->fdc);
    mem_snapshot_onload(&im.mem, sys);
    *sys = im;
    return true;
}

#endif  // CHIPS_IMPL
