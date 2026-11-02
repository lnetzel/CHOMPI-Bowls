# CHOMPI Singing Bowl — Development Plan (firmware name CHOMPI-BOWLS)

I'd approach this as a dedicated physical-modelling instrument, not as a conventional synthesiser with a singing-bowl preset.

The goal should be to make CHOMPI behave like a collection of struck, resonating metal bowls, independently playable bowls and a keyboard layout based on the actual tonal characteristics of singing bowls.

There are three major design challenges:

1. The sound engine: convincingly model the resonances, excitation and decay of metal bowls.
2. The tuning system: create a playable scale based on real singing-bowl tunings rather than imposing equal temperament.
3. The instrument behaviour: make bowl voices feel like physical objects being struck, with natural variations between hits.

One important distinction before we start: real singing bowls aren't manufactured to a universal musical scale. Their fundamental frequencies and overtones vary considerably. Some commercial bowls are marketed using chakra notes or Western note names, but those aren't a reliable universal tuning standard.

I'd therefore design a collection of measured or representative bowl profiles, rather than pretend there is one authentic singing-bowl scale.

## 1. Hardware and DSP architecture

CHOMPI Sample

Your specified hardware target

The relevant platform characteristics to design around are:

| Resource       | Design consideration                    |
| -------------- | --------------------------------------- |
| CPU            | STM32H750 Cortex-M7, up to 480 MHz      |
| Internal SRAM  | Fast working memory, relatively limited |
| External SDRAM | 64 MB on the SDRAM-equipped variant     |
| QSPI flash     | 8 MB                                    |
| Audio          | Stereo, 24-bit audio path               |
| Sample rate    | Target 48 kHz                           |

I'd make the following architectural decisions:

- Native synthesis, no samples or wavetables required.
- Four independent physical-model voices (before voice stealing, but one bowl can of course only make one sound at a time, unless it's struck again)
- Stereo output, with controllable spatial placement.
- No external memory dependency for the core synthesis engine.
- All synthesis parameters stored as one preset file (json), it's save directly and will be remembered between power cycles.
- Optional reverb handled separately from the bowl modelling. That will be in a future iteration.
My recommendation: modal synthesis with paired resonators.

There is a particularly important detail about actual singing bowls that I want to incorporate.

A real bowl often has two very closely spaced resonances for each vibration mode. This is caused by slight asymmetries in the physical bowl.

Those paired resonances produce the characteristic beating and shimmering sound.

A 2006 acoustic study of Tibetan singing bowls experimentally identified these paired modes and found approximately 5–7 prominent resonances in the bowls studied, extending up to around 4–6 kHz.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://iypt.ru\&sz=32)

iypt.ru



This is a much more interesting starting point than simply stacking eight unrelated sine oscillators.

### Proposed voice structure

One bowl voice

Strike / excitation generator

Impulse + shaped noise burst

Mode 1

Fundamental pair

2 resonators

Mode 2

Second resonance pair

2 resonators

Mode 3

Third resonance pair

2 resonators

Mode 4

Fourth resonance pair

2 resonators

Mode 5–8

Higher resonance pairs

2 resonators

Modal summation

All resonances combined into one bowl voice

With eight mode pairs, that's 16 resonators per voice, or 64 across four voices.

That's a very reasonable starting architecture for the Daisy.

We can increase or decrease the number of modes later based on CPU measurements and listening tests.

## 2. The actual singing bowl model

I would divide the sound engine into three independent parts.

### A. Resonance model

Each bowl has its own collection of resonant frequencies.

Rather than using conventional harmonic ratios, use measured modal ratios as the starting point.

One of the acoustic studies I found measured the following ratios for a 180 mm bowl:

| Mode | Frequency ratio | At 220 Hz fundamental |
| ---- | --------------- | --------------------- |
| 1    | 1.0             | 220 Hz                |
| 2    | 2.8             | 616 Hz                |
| 3    | 5.2             | 1,144 Hz              |
| 4    | 8.1             | 1,782 Hz              |
| 5    | 11.6            | 2,552 Hz              |
| 6    | 15.6            | 3,432 Hz              |
| 7    | 19.9            | 4,378 Hz              |

These are actual measured ratios, rounded for illustration.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://iypt.ru\&sz=32)

iypt.ru



Notice how different these are from the harmonic series.

I would use these as a starting template, not as universal values. Other bowl profiles should have their own modal ratios.

### B. Paired resonances and beating

This is one of the most important parts of the implementation.

Instead of one oscillator at 220 Hz, we might have:

- Resonator A: 220.0 Hz
- Resonator B: 220.8 Hz

Both are excited simultaneously.

The difference creates a beating effect at approximately 0.8 Hz.

The same principle applies to the higher modes, with independently configurable detuning.

Illustrative beating behaviour

Conceptual illustration, not an actual measured waveform.

I'd make the detuning profile specific to each mode. A single global detune parameter would sound too much like an ordinary chorus.

### C. Excitation model

This is what makes a bowl feel struck rather than simply switched on.

I would implement three excitation types initially:

| Excitation  | Character                                   |
| ----------- | ------------------------------------------- |
| Hard strike | Bright, sharp, metallic                     |
| Soft strike | Rounder, warmer, less high-frequency energy |
| Gentle tap  | Subtle, delicate, less aggressive attack    |

Technically, these can be implemented using short excitation signals with different spectral distributions.

The excitation should be distributed across the modes using mode-dependent weights.

A harder strike should generally excite more high-frequency modes, while a softer strike favours the lower modes.

One important feature for later: strike position. Real bowl modes have spatial patterns, so striking different positions changes which modes are excited. The acoustic study demonstrates this relationship between excitation location and modal response.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://iypt.ru\&sz=32)

iypt.ru



### D. Natural decay

Every mode should have its own decay time.

I would avoid a conventional amplitude envelope controlling the entire voice.

Instead, each resonator should lose energy independently.

This means the bowl's tone evolves naturally after the strike.

For example:

- Fundamental: 12 seconds.
- Second mode: 9 seconds.
- Third mode: 7 seconds.
- Higher modes: 2–5 seconds.

Those are starting sound-design values, not universal measurements.

This is what gives you that beautiful transition from a bright initial strike to a soft, almost floating resonance.

## 3. The tuning system — probably the most important design decision

I would not use ordinary equal temperament as the basis of the keyboard.

But I also wouldn't invent an arbitrary exotic scale and call it a singing-bowl tuning.

There is no single historically established Tibetan singing-bowl scale that all bowls follow.

Instead, I'd build a Bowl Set system.

Think of it as a collection of real-world bowl pitches, each assigned to a physical key.

### What actual measurements tell us

A 2023 study documented four singing bowls with the following measured fundamental frequencies and overtones:

| Bowl diameter | Fundamental |
| ------------- | ----------- |
| 23 cm         | 99 Hz       |
| 15 cm         | 219 Hz      |
| 12 cm         | 465 Hz      |
| 8 cm          | 927 Hz      |

These frequencies span quite a large range.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://pmc.ncbi.nlm.nih.gov\&sz=32)

PMC



Another experimental study measured bowls with fundamentals of approximately 86.7, 219.6, 310.2 and 513 Hz.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://iypt.ru\&sz=32)

iypt.ru



These are useful reference points, but they're not a statistically representative catalogue of all commercially available bowls.

### My proposed keyboard concept

I'd create three tuning modes:

1\. Bowl Collection

A collection of individually modelled bowls with their own measured or representative fundamental frequencies.

This would be the most authentic mode.

Each key represents a particular bowl, not a semitone.

2\. Bowl Scale

A playable collection of frequencies selected from real bowl measurements, arranged into a musically useful scale.

I'd start with a seven-key layout, with no requirement that adjacent keys have equal frequency ratios.

3\. Free Tuning

The user can tune the root frequency and adjust the interval relationships between keys.

This is where you can create completely imaginary bowl collections.

### Keyboard mapping

I suggest using a lookup table rather than calculating pitches from MIDI note numbers.

For example:

```
struct BowlKey
{
    float fundamental_hz;
    uint8_t bowl_profile;
};

const BowlKey bowl_scale[] =
{
    { 99.0f,  0 },
    { 146.0f, 1 },
    { 219.6f, 2 },
    { 310.2f, 3 },
    { 390.0f, 4 },
    { 465.0f, 5 },
    { 513.0f, 6 }
};
```

The intermediate frequencies here are illustrative design choices, not claimed measurements of real bowls. The four measured frequencies are the research anchors.

The actual mapping should be finalized after we decide which real-world bowl collection we're representing.

A critical detail: changing the fundamental pitch should scale the entire bowl's modal structure proportionally, unless the user explicitly chooses to alter the inharmonicity.

Otherwise, you end up changing the physical character of the bowl every time you play a different note.

## 4. Four-voice polyphony

I'd make each voice represent one independently struck bowl.

That means:

- Each voice has its own fundamental frequency.
- Each voice has its own modal frequency profile.
- Each voice has independent decay characteristics.
- Each voice can have independent strike intensity.
- Each voice has its own stereo position.

The four voices should be able to ring simultaneously for as long as their natural decay requires.

Voice stealing: I'd avoid stealing an already ringing bowl whenever possible.

If all four voices are occupied and a fifth note arrives, I'd steal the quietest voice, not necessarily the oldest one.

This is important for an instrument intended to create long overlapping resonances.

### CPU and memory budget

I'd aim for:

| Component            | Proposed implementation           |
| -------------------- | --------------------------------- |
| Voices               | 4                                 |
| Mode pairs per voice | 8                                 |
| Resonators           | 64 total                          |
| Resonator type       | Efficient second-order resonators |
| Sample rate          | 48 kHz                            |
| Internal state       | Per-resonator state variables     |
| Reverb               | Separate stereo effect            |

There is no need to allocate large sample buffers or use SDRAM for the main synthesis engine.

The computational cost of 64 resonators should be modest for an STM32H750, although we should benchmark the actual implementation rather than assume a CPU percentage.

## 5. CHOMPI controls and instrument behaviour

I wouldn't make this too complicated. A singing bowl instrument should be immediately playable.

I'd suggest the following parameter allocation, adapting it to the available CHOMPI controls.

| Control      | Parameter      | Behaviour                      |
| ------------ | -------------- | ------------------------------ |
| Encoder 1    | Bowl character | Changes the resonant profile   |
| Encoder 2    | Strike         | Soft to hard excitation        |
| Encoder 3    | Decay          | Short to very long sustain     |
| Encoder 4    | Shimmer        | Controls paired-mode detuning  |
| SHIFT + Encoder 2    | Brightness     | Adjusts energy in higher modes |
| Volume       | Master level   | Output volume                  |
 

### A particularly useful feature: Rim rubbing

I would consider this a second playing mode.

A singing bowl can be struck, but it can also be made to sing continuously by rubbing a mallet around its rim.

These are fundamentally different excitation mechanisms.

For the first version, I'd implement this as a sustained excitation signal, rather than attempting a complete nonlinear friction You would play this via turning the Transport Wheel slow or fast. Slow would of course make the sound a bit softer and faster would make the sound more loud.

The result should be a continuous, evolving resonance rather than a struck sound with an extremely long envelope.


### Stereo behaviour

I'd avoid conventional stereo chorus.

Instead, I'd use the natural paired-mode differences to create some stereo movement.

For example:

- Modal family A slightly left.
- Modal family B slightly right.
- Higher modes have somewhat wider separation.
- Fundamental remains centred.

This gives you a sense of physical space without making the bowl sound like a conventional stereo effect.

I'd also include a width control from mono to fairly wide.


## 7. Important implementation details

A few technical decisions I'd make from the outset:

Use stable resonator structures.

I would use second-order resonators with coefficients calculated when a voice is triggered or its parameters change, rather than recalculating trigonometric functions in the audio callback.

Keep the resonator state continuous.

Don't reset all the resonators just because a new note is played. Each voice needs its own state, and its existing ringing should continue until its energy has decayed sufficiently.

Avoid unnecessary random modulation.

The beating should primarily come from the paired resonances, not from random LFOs. That's a much more convincing result.

Make the profile data independent of the DSP implementation.

A bowl profile should contain frequencies/ratios, damping, modal amplitudes, paired-mode offsets and excitation characteristics. That way you can develop new bowl models without rewriting the sound engine.

For example, a profile could eventually be stored in a JSON file on the SD card, following the same general approach as your other CHOMPI preset data.&#x20;

## 8. What I would consider a successful first version

I'd use these as acceptance criteria:

| Test                    | Expected result                                     |
| ----------------------- | --------------------------------------------------- |
| Single strike           | Clearly metallic, convincing bowl character         |
| Long decay              | Natural evolution of the tone                       |
| Paired resonances       | Audible but not exaggerated beating                 |
| Four simultaneous notes | No audible voice stealing or glitches               |
| Different bowl profiles | Distinct physical characteristics                   |
| Non-equal tuning        | Keyboard intervals reflect selected bowl collection |
| Soft vs hard strike     | Noticeably different spectral character             |
| CPU                     | Comfortable headroom for future features            |


## 9. My biggest recommendation: build the instrument around real measurements

I found two particularly useful research resources:

1. [The Dynamics of Tibetan Singing Bowls (2006)](https://iypt.ru/wp-content/uploads/2024/10/The-dynamics-of-Tibetan-singing-bowls.pdf?utm_source=chatgpt.com) — probably the most valuable technical reference. It contains measured resonances, mode pairs, damping and excitation behaviour.
2. [Acute Relaxation Response Induced by Tibetan Singing Bowl Sounds (2023)](https://pmc.ncbi.nlm.nih.gov/articles/PMC9955072/?utm_source=chatgpt.com) — contains measurements of four actual bowls, including their fundamental frequencies and overtones.

I'd use the first paper to build the physical model and the second to help establish representative bowl profiles and tuning collections.

My suggested scope for the first coding iteration:

- One bowl.
- Eight paired resonant modes.
- One strike generator.
- Independent decay per mode.
- Adjustable fundamental frequency.
- No reverb.
- No stereo tricks.
- No four-voice polyphony yet.

Get that one bowl sounding genuinely good first. Then expand it.

The reason I'm emphasizing this is that a mediocre physical model multiplied by four voices is still a mediocre instrument. But a really convincing single bowl model can become something quite special when played polyphonically.

I would much rather have CHOMPI produce four beautiful, overlapping, slightly imperfect metal resonances than a technically impressive but sterile collection of sine-wave resonators.