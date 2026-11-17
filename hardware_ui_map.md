CHOMPI Hardware UI Map
Hardware control names, switch-chain indices, and LED addresses. Read positions with the instrument facing the player and keyboard at the bottom.

## Naming
Physical encoders 1-4 are the upper knobs from left to right. SW1-SW6 are PCB references; ENC_n names firmware signals. KEY_n and SW_TOG are switch signals. Array and LED API indices are zero-based; physical key and encoder numbers are one-based.
PTH and SMT LED indices are separate address spaces. The listed LED numbers refer to the Rev4 PTH board.

## Rotary Controls
Rotation and push are separate inputs. Rotation uses `hw.enc[n]`; most pushes use `hw.button_sr`. The wheel uses its direct click input.

| Control | PCB reference | Signal | Rotation | Push | LED |
|---|---|---|---|---|---|
| Physical encoder 1 | SW4 | ENC_4 | `hw.enc[3]` | `ENC_4_SW` (3) | LED2 / PTH 1 |
| Physical encoder 2 | SW1 | ENC_1 | `hw.enc[0]` | `ENC_1_SW` (0) | LED3 / PTH 2 |
| Physical encoder 3 | SW2 | ENC_2 | `hw.enc[1]` | `ENC_2_SW` (1) | LED4 / PTH 3 |
| Physical encoder 4 | SW3 | ENC_3 | `hw.enc[2]` | `ENC_3_SW` (2) | LED5 / PTH 4 |
| Transport wheel | SW5 | ENC_5 | `hw.enc[4]` | Direct D10 | LED6 / PTH 5; LED7 / PTH 6 |
| Volume control | SW6 | ENC_6 | `hw.enc[5]` | `ENC_6_SW` (32) | LED10 / PTH 9 |

## Rotation Wiring
Seed pin names are `daisy::seed::D<n>`.

Hardware reference	Rotation A	Rotation B	Push wiring
SW1 / ENC_1	encoder_sr.RawState(0) / EncoderSrId::ENC_1_A	encoder_sr.RawState(1) / EncoderSrId::ENC_1_B	Button chain U5 slot 1
SW2 / ENC_2	encoder_sr.RawState(2) / EncoderSrId::ENC_2_A	encoder_sr.RawState(3) / EncoderSrId::ENC_2_B	Button chain U5 slot 2
SW3 / ENC_3	encoder_sr.RawState(4) / EncoderSrId::ENC_3_A	encoder_sr.RawState(5) / EncoderSrId::ENC_3_B	Button chain U5 slot 3
SW4 / ENC_4	encoder_sr.RawState(6) / EncoderSrId::ENC_4_A	encoder_sr.RawState(7) / EncoderSrId::ENC_4_B	Button chain U5 slot 4
SW5 / ENC_5	D0	D20	D10, direct encoder click input
SW6 / ENC_6	D15	D17	Button chain U10 slot 1; encoder click pin is unassigned
The four pipe encoders use a separate single-CD4021 rotation chain: clock D22, latch D23, data D19. Its IC PCB reference is not established here.

## Function Keys and Toggle
Read keys with `hw.button_sr.State(static_cast<int>(Hardware::SwId::<name>))`.

| Control | Signal | SwId index | Button chain | LED |
|---|---|---:|---|---|
| SHIFT | KEY_26 | 5 | U5 slot 6 | LED1 / PTH 0 |
| Reserved toggle | SW_TOG | 6 | U5 slot 7 | None |
| PLAY | KEY_27 | 33 | U10 slot 2 | LED8 / PTH 7 |
| LOOP | KEY_28 | 34 | U10 slot 3 | LED9 / PTH 8 |
The toggle is filtered by `ProcessAllControls()` and read with `GetToggleState()`.

## Panel LED Matrix
These are the 10 PTH RGB LEDs. The wheel has two indicators.

| Board label | LED API | Signal |
|---|---|---|
| LED1 | PTH 0 | KEY_26 |
| LED2 | PTH 1 | SW4 / ENC_4 |
| LED3 | PTH 2 | SW1 / ENC_1 |
| LED4 | PTH 3 | SW2 / ENC_2 |
| LED5 | PTH 4 | SW3 / ENC_3 |
| LED6 | PTH 5 | SW5 / ENC_5 |
| LED7 | PTH 6 | SW5 / ENC_5 |
| LED8 | PTH 7 | KEY_27 |
| LED9 | PTH 8 | KEY_28 |
| LED10 | PTH 9 | SW6 / ENC_6 |

## Keyboard Matrix
The keyboard has 15 white and 10 black keys. KEY_1-KEY_15 are white keys left to right; KEY_16-KEY_25 are black keys left to right. The table is in physical pitch order.

MIDI note numbers are authoritative; octave labels differ between applications. LED entries are SMT chain addresses.

Physical key	Hardware signal	Boot note	MIDI note	SwId index	Button chain location	LED API address
White 1 (leftmost)	KEY_1	C3	48	15	U7 slot 8	SMT 24
Black 1	KEY_16	C#3	49	7	U5 slot 8	SMT 0
White 2	KEY_2	D3	50	8	U7 slot 1	SMT 23
Black 2	KEY_17	D#3	51	12	U7 slot 5	SMT 1
White 3	KEY_3	E3	52	9	U7 slot 2	SMT 22
White 4	KEY_4	F3	53	10	U7 slot 3	SMT 21
Black 3	KEY_18	F#3	54	13	U7 slot 6	SMT 2
White 5	KEY_5	G3	55	11	U7 slot 4	SMT 20
Black 4	KEY_19	G#3	56	14	U7 slot 7	SMT 3
White 6	KEY_6	A3	57	16	U8 slot 1	SMT 19
Black 5	KEY_20	A#3	58	21	U8 slot 6	SMT 4
White 7	KEY_7	B3	59	17	U8 slot 2	SMT 18
White 8	KEY_8	C4	60	18	U8 slot 3	SMT 17
Black 6	KEY_21	C#4	61	22	U8 slot 7	SMT 5
White 9	KEY_9	D4	62	19	U8 slot 4	SMT 16
Black 7	KEY_22	D#4	63	23	U8 slot 8	SMT 6
White 10	KEY_10	E4	64	20	U8 slot 5	SMT 15
White 11	KEY_11	F4	65	24	U9 slot 1	SMT 14
Black 8	KEY_23	F#4	66	29	U9 slot 6	SMT 7
White 12	KEY_12	G4	67	25	U9 slot 2	SMT 13
Black 9	KEY_24	G#4	68	30	U9 slot 7	SMT 8
White 13	KEY_13	A4	69	26	U9 slot 3	SMT 12
Black 10	KEY_25	A#4	70	31	U9 slot 8	SMT 9
White 14	KEY_14	B4	71	27	U9 slot 4	SMT 11
White 15 (rightmost)	KEY_15	C5	72	28	U9 slot 5	SMT 10
## Shared Wiring
Interface	Firmware configuration / hardware route
Button input chain	Five CD4021s: U5, U7, U8, U9, U10; clock D8, latch D7, data D9
Button chain slot notation	Slots 1-8 reproduce the source's per-register ordering, not IC package pin numbers; global index = register ordinal (0-4) * 8 + slot - 1
Unused button inputs	Index 4 = NC_6 (U5 slot 5); indices 35-39 = NC_1-NC_5 (U10 slots 4-8)
PTH panel LED output	D16, TIM5 channel 4; 10 physical LEDs; driver emits RGB byte order
SMT keyboard LED output	D18, TIM3 channel 2; 25 physical LEDs; driver emits GRB byte order
LED API indices exclude DMA porch padding. kNumPthLeds and kNumSmtLeds include two six-slot porches (22 and 37 buffer slots); these are not counts of physical LEDs. Use indices 0-9 and 0-24 respectively for real LEDs.
Update/debounce inputs before reading them and flush LED changes with `fill_led_data()`.
