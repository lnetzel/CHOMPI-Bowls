#pragma once

#include <cstdint>

namespace bowls
{
enum class VolumeControlPage : uint8_t
{
    kMasterVolume,
    kReverbMix,
    kReverbDecay,
};

constexpr VolumeControlPage NextVolumeControlPage(VolumeControlPage page)
{
    switch(page)
    {
        case VolumeControlPage::kMasterVolume:
            return VolumeControlPage::kReverbMix;
        case VolumeControlPage::kReverbMix:
            return VolumeControlPage::kReverbDecay;
        case VolumeControlPage::kReverbDecay:
            return VolumeControlPage::kMasterVolume;
    }
    return VolumeControlPage::kMasterVolume;
}

enum class Encoder1Page : uint8_t
{
    kBowlVolume,
    kPan,
    kCoarseTune,
    kFineTune,
};

constexpr Encoder1Page NextEncoder1Page(Encoder1Page page)
{
    switch(page)
    {
        case Encoder1Page::kBowlVolume:
            return Encoder1Page::kPan;
        case Encoder1Page::kPan:
            return Encoder1Page::kCoarseTune;
        case Encoder1Page::kCoarseTune:
            return Encoder1Page::kFineTune;
        case Encoder1Page::kFineTune:
            return Encoder1Page::kBowlVolume;
    }
    return Encoder1Page::kBowlVolume;
}

constexpr bool Encoder1UsesFinePitchStep(Encoder1Page page)
{
    return page == Encoder1Page::kFineTune;
}
} // namespace bowls
