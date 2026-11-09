#pragma once

// CHOMPI hardware layer for the Bowls firmware (ported from
// CHOMPI-PhaseOff: the hardware is identical).
// Covers: dual-codec audio (SAI1 + SAI2), the six rotary encoders, the
// 25-key keyboard switch chain and the AUX jack detect.
// Pin and chain assignments follow HARDWARE_UI_MAP.md / TAPE hardware.h.

#include "daisy_seed.h"
#include "dev/sr_4021.h"

#include <cstring>

namespace chompi
{
using namespace daisy;

/** Keyboard switch-chain indices in pitch order (C3..C5). */
constexpr unsigned kKeySwitches[25] = {
    15, 7, 8, 12, 9, 10, 13, 11, 14, 16, 21, 17, 18,
    22, 19, 23, 20, 24, 29, 25, 30, 26, 31, 27, 28};

/** Button-chain indices of the encoder push switches (HARDWARE_UI_MAP):
 *  physical encoders 1-4 (SW4/SW1/SW2/SW3), then the volume knob. */
constexpr unsigned kEncoderSwitches[4] = {3, 0, 1, 2};
constexpr unsigned kSwitchVolume       = 32;
constexpr unsigned kKeyPlay            = 33; /**< KEY_27 */
constexpr unsigned kKeyLoop            = 34; /**< KEY_28 */

/** Quadrature decoder for the CHOMPI encoders.
 *  A/B are either read from GPIO pins (wheel, volume) or fed from the
 *  encoder shift-register chain (physical encoders 1-4).
 *  Call Debounce() at ~1kHz or faster; Increment() reports detent steps.
 */
class QuadratureEncoder
{
  public:
    void InitGpio(dsy_gpio_pin pin_a, dsy_gpio_pin pin_b)
    {
        hw_a_.pin  = pin_a;
        hw_a_.mode = DSY_GPIO_MODE_INPUT;
        hw_a_.pull = DSY_GPIO_PULLUP;
        hw_b_.pin  = pin_b;
        hw_b_.mode = DSY_GPIO_MODE_INPUT;
        hw_b_.pull = DSY_GPIO_PULLUP;
        dsy_gpio_init(&hw_a_);
        dsy_gpio_init(&hw_b_);
        use_gpio_ = true;
    }

    void Debounce()
    {
        if(!use_gpio_)
            return;
        Debounce(dsy_gpio_read(&hw_a_), dsy_gpio_read(&hw_b_));
    }

    void Debounce(bool a_state, bool b_state)
    {
        // Throttle to ~1kHz; callers may run faster.
        const uint32_t now = System::GetNow();
        updated_           = false;
        if(now == last_update_)
            return;
        last_update_ = now;
        updated_     = true;

        history_a_ = static_cast<uint8_t>((history_a_ << 1) | a_state);
        history_b_ = static_cast<uint8_t>((history_b_ << 1) | b_state);

        inc_ = 0;
        if((history_a_ & 0x07) == 0x04 && (history_b_ & 0x03) == 0x00)
            inc_ = 1;
        else if((history_b_ & 0x07) == 0x04 && (history_a_ & 0x03) == 0x00)
            inc_ = -1;
    }

    /** +1 per detent clockwise, -1 counter-clockwise, 0 when idle. */
    int32_t Increment() const { return updated_ ? inc_ : 0; }

  private:
    dsy_gpio hw_a_{};
    dsy_gpio hw_b_{};
    uint32_t last_update_ = 0;
    uint8_t  history_a_   = 0xff;
    uint8_t  history_b_   = 0xff;
    int32_t  inc_         = 0;
    bool     updated_     = false;
    bool     use_gpio_    = false;
};

class ChompiHardware
{
  public:
    /** Encoder slots in physical order: 0-3 = panel knobs left to right,
     *  4 = transport wheel, 5 = volume knob. */
    static constexpr int kEncoderCount = 6;

    void Init()
    {
        seed.Init(true);
        InitAudio();

        // MP2722 USB-C power/data switch. CHOMPI-PIPES configures this
        // path before USB MIDI can reach the host; without it the MCU's
        // USB peripheral starts but the panel connector stays isolated.
        I2CHandle::Config i2c_cfg;
        i2c_cfg.mode              = I2CHandle::Config::Mode::I2C_MASTER;
        i2c_cfg.periph            = I2CHandle::Config::Peripheral::I2C_1;
        i2c_cfg.speed             = I2CHandle::Config::Speed::I2C_100KHZ;
        i2c_cfg.address           = 0x3F;
        i2c_cfg.pin_config.scl    = seed::D11;
        i2c_cfg.pin_config.sda    = seed::D12;
        i2c_.Init(i2c_cfg);

        usb_sw_.Init(seed::D32, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);

        // Button chain: 5x CD4021 (U5, U7, U8, U9, U10) on D7/D8/D9.
        ShiftRegister4021<5>::Config button_sr_cfg;
        button_sr_cfg.clk       = seed::D8;
        button_sr_cfg.latch     = seed::D7;
        button_sr_cfg.data[0]   = seed::D9;
        button_sr_cfg.dbc_size  = 8;
        button_sr_.Init(button_sr_cfg);

        // Rotation chain for physical encoders 1-4: single CD4021.
        ShiftRegister4021<1>::Config encoder_sr_cfg;
        encoder_sr_cfg.clk      = seed::D22;
        encoder_sr_cfg.latch    = seed::D23;
        encoder_sr_cfg.data[0]  = seed::D19;
        encoder_sr_cfg.dbc_size = 50; // RawState is used directly.
        encoder_sr_.Init(encoder_sr_cfg);

        // Physical encoders 1-4 read their A/B phases from the chain.
        // Wheel and volume encoders are wired to GPIOs directly.
        enc_[4].InitGpio(seed::D0, seed::D20);
        enc_[5].InitGpio(seed::D15, seed::D17);

        // Transport wheel click is a direct input on D10 (not the chain).
        wheel_click_.Init(seed::D10, 1000.f, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED, Switch::PULL_UP);

        // AUX input jack detection (pull-up on the board).
        jack_detect_.Init(seed::D21, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    }

    /** Connect the panel USB-C port to the Seed's USB device peripheral.
     *  Mirrors the known-working CHOMPI-PIPES MP2722 sequence. */
    void ConfigureUsbDevicePath()
    {
        usb_sw_.Write(false);
        System::Delay(1);
        uint8_t command[2] = {0x0a, 0x24};
        i2c_.TransmitBlocking(0x3F, command, sizeof(command), 200);
        System::Delay(1);
        usb_sw_.Write(true);
    }

    void StartAudio(AudioHandle::AudioCallback cb) { seed.StartAudio(cb); }

    /** Debounce every control. Call once per audio block (~0.5ms). */
    void ProcessControls()
    {
        button_sr_.Update();
        encoder_sr_.Update();

        // Physical knob order on the panel is SW4, SW1, SW2, SW3.
        enc_[0].Debounce(encoder_sr_.RawState(6), encoder_sr_.RawState(7));
        enc_[1].Debounce(encoder_sr_.RawState(0), encoder_sr_.RawState(1));
        enc_[2].Debounce(encoder_sr_.RawState(2), encoder_sr_.RawState(3));
        enc_[3].Debounce(encoder_sr_.RawState(4), encoder_sr_.RawState(5));
        enc_[4].Debounce();
        enc_[5].Debounce();

        wheel_click_.Debounce();

        // Debounce the 40 chain inputs with a shift-history filter.
        std::memset(key_rising_, 0, sizeof(key_rising_));
        std::memset(key_falling_, 0, sizeof(key_falling_));
        for(unsigned i = 0; i < 40; ++i)
        {
            key_history_[i] = static_cast<uint8_t>((key_history_[i] << 1)
                                                   | (button_sr_.RawState(i) ? 0 : 1));
            if(key_history_[i] == 0xff && !key_held_[i])
            {
                key_held_[i]   = true;
                key_rising_[i] = true;
            }
            else if(key_history_[i] == 0x00 && key_held_[i])
            {
                key_held_[i]    = false;
                key_falling_[i] = true;
            }
        }
    }

    /** Detent increment for an encoder slot since the last ProcessControls(). */
    int32_t EncoderIncrement(int slot) const { return enc_[slot].Increment(); }

    bool WheelClicked() const { return wheel_click_.RisingEdge(); }
    bool WheelPressed() const { return wheel_click_.Pressed(); }
    bool WheelReleased() const { return wheel_click_.FallingEdge(); }
    /** Milliseconds the wheel has been held down (0 when not pressed). */
    float WheelHeldMs() const { return wheel_click_.TimeHeldMs(); }
    bool AuxJackPlugged() { return jack_detect_.Read(); }

    bool KeyRising(unsigned key) const { return key_rising_[kKeySwitches[key]]; }
    bool KeyFalling(unsigned key) const { return key_falling_[kKeySwitches[key]]; }
    bool KeyHeld(unsigned key) const { return key_held_[kKeySwitches[key]]; }

    /** CHOMPI/SHIFT key (KEY_26, chain index 5). */
    bool ShiftHeld() const { return key_held_[5]; }

    /** Mode toggle (SW_TOG, chain index 6): DOWN = edit the phaser,
     *  UP = edit the synth. Debounced by the shift-history filter in
     *  ProcessControls(). The raw chain bit reads LOW-closed in the
     *  physical UP position, hence the inversion. */
    bool ModeSwitchUp() const { return !key_held_[6]; }

    /** Push switches of physical encoders 1-4 (button chain U5 slots
     *  4/1/2/3 = chain indices 3/0/1/2) and of the volume knob
     *  (U10 slot 1 = chain index 32), per HARDWARE_UI_MAP.md. */
    bool EncoderPressed(int slot) const
    {
        return key_held_[kEncoderSwitches[slot]];
    }
    bool EncoderClicked(int slot) const
    {
        return key_rising_[kEncoderSwitches[slot]];
    }
    bool VolumePressed() const { return key_held_[kSwitchVolume]; }
    bool VolumeClicked() const { return key_rising_[kSwitchVolume]; }

    /** PLAY (KEY_27, chain 33) and LOOP (KEY_28, chain 34) keys. */
    bool PlayPressed() const { return key_held_[kKeyPlay]; }
    bool PlayClicked() const { return key_rising_[kKeyPlay]; }
    bool PlayReleased() const { return key_falling_[kKeyPlay]; }
    bool LoopPressed() const { return key_held_[kKeyLoop]; }
    bool LoopClicked() const { return key_rising_[kKeyLoop]; }
    bool LoopReleased() const { return key_falling_[kKeyLoop]; }

    DaisySeed seed;

  private:
    void InitAudio()
    {
        SaiHandle::Config sai_cfg;
        sai_cfg.periph          = SaiHandle::Config::Peripheral::SAI_2;
        sai_cfg.sr              = SaiHandle::Config::SampleRate::SAI_48KHZ;
        sai_cfg.bit_depth       = SaiHandle::Config::BitDepth::SAI_24BIT;
        sai_cfg.a_sync          = SaiHandle::Config::Sync::SLAVE;
        sai_cfg.b_sync          = SaiHandle::Config::Sync::MASTER;
        sai_cfg.a_dir           = SaiHandle::Config::Direction::TRANSMIT;
        sai_cfg.b_dir           = SaiHandle::Config::Direction::RECEIVE;
        sai_cfg.pin_config.fs   = seed::D27;
        sai_cfg.pin_config.mclk = seed::D24;
        sai_cfg.pin_config.sck  = seed::D28;
        sai_cfg.pin_config.sb   = seed::D25;
        sai_cfg.pin_config.sa   = seed::D26;
        external_sai_handle_.Init(sai_cfg);

        AudioHandle::Config audio_cfg;
        audio_cfg.blocksize  = 24;
        audio_cfg.samplerate = SaiHandle::Config::SampleRate::SAI_48KHZ;
        audio_cfg.postgain   = 1.0f;
        seed.audio_handle.Init(audio_cfg, seed.AudioSaiHandle(), external_sai_handle_);
    }

    SaiHandle           external_sai_handle_;
    I2CHandle            i2c_;
    GPIO                 usb_sw_;
    ShiftRegister4021<5> button_sr_;
    ShiftRegister4021<1> encoder_sr_;
    QuadratureEncoder    enc_[kEncoderCount];
    Switch               wheel_click_;
    GPIO                 jack_detect_;

    uint8_t key_history_[40] = {};
    bool    key_held_[40]    = {};
    bool    key_rising_[40]  = {};
    bool    key_falling_[40] = {};
};

} // namespace chompi
