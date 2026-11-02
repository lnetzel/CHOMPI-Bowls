#pragma once

// CHOMPI addressable LED driver.
// 25 SMT keyboard LEDs on D18 (PA7, TIM3_CH2, GRB order) and
// 10 PTH panel LEDs on D16 (PA3, TIM5_CH4, RGB order).
// Timing and mapping follow the CHOMPI-pitch-quiz keyboard_leds.h driver,
// which in turn derives from TAPE temp_led_stuff.h.
//
// Usage: SetSmtLed / SetPthLed write to shadow buffers, Flush() encodes the
// WS2812 bitstream and pushes both chains out with polled DMA transfers.

#include "daisy_seed.h"
#include "stm32h7xx_hal.h"

#include <cstring>

namespace chompi_leds
{
constexpr unsigned kPadding      = 144;
constexpr unsigned kSmtLedCount  = 25;
constexpr unsigned kPthLedCount  = 10;
constexpr unsigned kSmtSymbols   = kSmtLedCount * 24 + 2 * kPadding;
constexpr unsigned kPthSymbols   = kPthLedCount * 24 + 2 * kPadding;

// SMT chain addresses in pitch order (C3..C5, whites and blacks interleaved).
constexpr unsigned kKeyLeds[25] = {
    24, 0, 23, 1, 22, 21, 2, 20, 3, 19, 4, 18, 17,
    5, 16, 6, 15, 14, 7, 13, 8, 12, 9, 11, 10};
// SMT chain addresses of the 15 white keys, left to right.
constexpr unsigned kWhiteKeyLeds[15] = {
    24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10};

// Panel LED addresses (PTH chain).
constexpr unsigned kPthShift      = 0;
constexpr unsigned kPthEncoder1   = 1;
constexpr unsigned kPthEncoder2   = 2;
constexpr unsigned kPthEncoder3   = 3;
constexpr unsigned kPthEncoder4   = 4;
constexpr unsigned kPthWheelLeft  = 5;
constexpr unsigned kPthWheelRight = 6;
constexpr unsigned kPthPlay       = 7;
constexpr unsigned kPthLoop       = 8;
constexpr unsigned kPthVolume     = 9;

static uint32_t DMA_BUFFER_MEM_SECTION smt_symbols_[kSmtSymbols];
static uint32_t DMA_BUFFER_MEM_SECTION pth_symbols_[kPthSymbols];
static uint8_t                         smt_color_[kSmtLedCount][3]; // RGB
static uint8_t                         pth_color_[kPthLedCount][3]; // RGB
static TIM_HandleTypeDef               smt_timer_;
static DMA_HandleTypeDef               smt_dma_;
static TIM_HandleTypeDef               pth_timer_;
static DMA_HandleTypeDef               pth_dma_;

inline void SetSmtLed(unsigned index, uint8_t red, uint8_t green, uint8_t blue)
{
    if(index >= kSmtLedCount)
        return;
    smt_color_[index][0] = red;
    smt_color_[index][1] = green;
    smt_color_[index][2] = blue;
}

inline void SetPthLed(unsigned index, uint8_t red, uint8_t green, uint8_t blue)
{
    if(index >= kPthLedCount)
        return;
    pth_color_[index][0] = red;
    pth_color_[index][1] = green;
    pth_color_[index][2] = blue;
}

inline void ClearAll()
{
    std::memset(smt_color_, 0, sizeof(smt_color_));
    std::memset(pth_color_, 0, sizeof(pth_color_));
}

inline void EncodeBytes(uint32_t* destination, uint8_t first, uint8_t second, uint8_t third)
{
    const uint8_t order[] = {first, second, third};
    for(unsigned color = 0; color < 3; ++color)
        for(unsigned bit = 0; bit < 8; ++bit)
            destination[color * 8 + bit] = (order[color] & (0x80 >> bit)) ? 20 : 10;
}

inline void Init()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM5_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    GPIO_InitTypeDef pin = {};
    pin.Pin              = GPIO_PIN_7;
    pin.Mode             = GPIO_MODE_AF_PP;
    pin.Pull             = GPIO_NOPULL;
    pin.Speed            = GPIO_SPEED_FREQ_VERY_HIGH;
    pin.Alternate        = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOA, &pin);

    GPIO_InitTypeDef panel_pin = {};
    panel_pin.Pin              = GPIO_PIN_3;
    panel_pin.Mode             = GPIO_MODE_AF_PP;
    panel_pin.Pull             = GPIO_NOPULL;
    panel_pin.Speed            = GPIO_SPEED_FREQ_VERY_HIGH;
    panel_pin.Alternate        = GPIO_AF2_TIM5;
    HAL_GPIO_Init(GPIOA, &panel_pin);

    smt_timer_.Instance               = TIM3;
    smt_timer_.Init.Prescaler         = 7;
    smt_timer_.Init.CounterMode       = TIM_COUNTERMODE_UP;
    smt_timer_.Init.Period            = (daisy::System::GetPClk1Freq() * 2 / 8 / 833332) - 1;
    smt_timer_.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&smt_timer_);
    TIM_OC_InitTypeDef channel = {};
    channel.OCMode                   = TIM_OCMODE_PWM1;
    channel.OCPolarity               = TIM_OCPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&smt_timer_, &channel, TIM_CHANNEL_2);

    pth_timer_.Instance           = TIM5;
    pth_timer_.Init.Prescaler     = 7;
    pth_timer_.Init.CounterMode   = TIM_COUNTERMODE_UP;
    pth_timer_.Init.Period        = smt_timer_.Init.Period;
    pth_timer_.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&pth_timer_);
    HAL_TIM_PWM_ConfigChannel(&pth_timer_, &channel, TIM_CHANNEL_4);

    smt_dma_.Instance                 = DMA2_Stream5;
    smt_dma_.Init.Request             = DMA_REQUEST_TIM3_UP;
    smt_dma_.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    smt_dma_.Init.PeriphInc           = DMA_PINC_DISABLE;
    smt_dma_.Init.MemInc              = DMA_MINC_ENABLE;
    smt_dma_.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    smt_dma_.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    smt_dma_.Init.Mode                = DMA_NORMAL;
    smt_dma_.Init.Priority            = DMA_PRIORITY_LOW;
    smt_dma_.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&smt_dma_);
    HAL_TIM_PWM_Start(&smt_timer_, TIM_CHANNEL_2);

    pth_dma_.Instance                 = DMA2_Stream6;
    pth_dma_.Init.Request             = DMA_REQUEST_TIM5_CH4;
    pth_dma_.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    pth_dma_.Init.PeriphInc           = DMA_PINC_DISABLE;
    pth_dma_.Init.MemInc              = DMA_MINC_ENABLE;
    pth_dma_.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    pth_dma_.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    pth_dma_.Init.Mode                = DMA_NORMAL;
    pth_dma_.Init.Priority            = DMA_PRIORITY_LOW;
    pth_dma_.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&pth_dma_);
    HAL_TIM_PWM_Start(&pth_timer_, TIM_CHANNEL_4);
}

/** Encode shadow buffers and push both chains. Blocks ~2ms while polling DMA. */
inline bool Flush()
{
    for(unsigned led = 0; led < kSmtLedCount; ++led)
        EncodeBytes(&smt_symbols_[kPadding + led * 24],
                    smt_color_[led][1], // GRB for the keyboard chain
                    smt_color_[led][0],
                    smt_color_[led][2]);
    for(unsigned led = 0; led < kPthLedCount; ++led)
        EncodeBytes(&pth_symbols_[kPadding + led * 24],
                    pth_color_[led][0], // RGB for the panel chain
                    pth_color_[led][1],
                    pth_color_[led][2]);

    if(HAL_DMA_Start(&smt_dma_,
                     reinterpret_cast<uint32_t>(smt_symbols_),
                     reinterpret_cast<uint32_t>(&TIM3->CCR2),
                     kSmtSymbols)
       != HAL_OK)
        return false;
    __HAL_TIM_ENABLE_DMA(&smt_timer_, TIM_DMA_UPDATE);
    const bool smt_ok
        = HAL_DMA_PollForTransfer(&smt_dma_, HAL_DMA_FULL_TRANSFER, 10) == HAL_OK;
    __HAL_TIM_DISABLE_DMA(&smt_timer_, TIM_DMA_UPDATE);
    if(!smt_ok)
        HAL_DMA_Abort(&smt_dma_);
    __HAL_TIM_SET_COMPARE(&smt_timer_, TIM_CHANNEL_2, 0);
    daisy::System::Delay(1);

    if(HAL_DMA_Start(&pth_dma_,
                     reinterpret_cast<uint32_t>(pth_symbols_),
                     reinterpret_cast<uint32_t>(&TIM5->CCR4),
                     kPthSymbols)
       != HAL_OK)
        return false;
    __HAL_TIM_ENABLE_DMA(&pth_timer_, TIM_DMA_CC4);
    const bool pth_ok
        = HAL_DMA_PollForTransfer(&pth_dma_, HAL_DMA_FULL_TRANSFER, 10) == HAL_OK;
    __HAL_TIM_DISABLE_DMA(&pth_timer_, TIM_DMA_CC4);
    if(!pth_ok)
        HAL_DMA_Abort(&pth_dma_);
    __HAL_TIM_SET_COMPARE(&pth_timer_, TIM_CHANNEL_4, 0);
    daisy::System::Delay(1);
    return smt_ok && pth_ok;
}

} // namespace chompi_leds
