#include "encoder_pages.h"

#include <cstdio>

namespace
{
int g_failures = 0;

void Check(bool ok, const char* name)
{
    std::printf("%-46s %s\n", name, ok ? "PASS" : "FAIL");
    if(!ok)
        ++g_failures;
}
} // namespace

int main()
{
    using bowls::Encoder1Page;
    using bowls::VolumeControlPage;

    Check(bowls::NextVolumeControlPage(VolumeControlPage::kMasterVolume)
                  == VolumeControlPage::kReverbMix
              && bowls::NextVolumeControlPage(VolumeControlPage::kReverbMix)
                     == VolumeControlPage::kReverbDecay
              && bowls::NextVolumeControlPage(VolumeControlPage::kReverbDecay)
                     == VolumeControlPage::kMasterVolume,
          "Volume control cycles through three explicit pages");

    Check(bowls::NextEncoder1Page(Encoder1Page::kBowlVolume)
                  == Encoder1Page::kPan
              && bowls::NextEncoder1Page(Encoder1Page::kPan)
                     == Encoder1Page::kCoarseTune
              && bowls::NextEncoder1Page(Encoder1Page::kCoarseTune)
                     == Encoder1Page::kFineTune
              && bowls::NextEncoder1Page(Encoder1Page::kFineTune)
                     == Encoder1Page::kBowlVolume,
          "Encoder 1 cycles through coarse and fine tune pages");

    Check(!bowls::Encoder1UsesFinePitchStep(Encoder1Page::kCoarseTune)
              && bowls::Encoder1UsesFinePitchStep(Encoder1Page::kFineTune),
          "Pitch step size follows the active tune page");

    return g_failures == 0 ? 0 : 1;
}
