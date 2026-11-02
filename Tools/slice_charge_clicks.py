# Cuts the charge sound, FilmCow's "A clicky squeaky knob 13", into one clip per click, so the
# mod can play each click as the charge passes its point and the sound fits any charge time at
# its own pitch. Writes C:\SF\PogoStick-assets\S_Pogo_Charge_Click_NN.wav for import_sounds.py
# and prints the click positions for PogoStickAssets::ChargeClickPositions.
#   python Tools/slice_charge_clicks.py [--preview DIR]
# --preview also writes how 1 s, 2 s and 5 s charges will sound, for listening before import.
import glob
import os
import sys
import wave

import numpy as np

SOURCE = r"C:\Users\latif\Desktop\New folder\FilmCow SFX - Mechanisms & Movement 192kHz\A clicky squeaky knob 13.wav"
OUTPUT_DIR = r"C:\SF\PogoStick-assets"
RATE = 48000
EDGE = 0.003          # each clip starts this long before its click, so the attack is kept whole
MIN_GAP = 0.035       # onsets closer than this are one click
FADE_OUT = 0.008


def read_mono(path):
    with wave.open(path) as file:
        channels, width, rate = file.getnchannels(), file.getsampwidth(), file.getframerate()
        raw = file.readframes(file.getnframes())
    if width == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        x = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    else:
        x = np.frombuffer(raw, {2: "<i2", 4: "<i4"}[width]).astype(float) / 2 ** (8 * width - 1)
    return x.reshape(-1, channels).mean(axis=1), rate


def resample(x, rate):
    """Band-limit to the new Nyquist in the frequency domain, then resample."""
    count = int(round(len(x) * RATE / rate))
    spectrum = np.fft.rfft(x)
    return np.fft.irfft(spectrum[:count // 2 + 1], count) * count / len(x)


def write(path, x):
    with wave.open(path, "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())


def find_onsets(x):
    """Sharp rises in a 2 ms envelope of the high-passed signal; the clicks live up there."""
    high = np.diff(x, prepend=0.0)
    hop = int(RATE * 0.002)
    env = np.sqrt(np.convolve(high ** 2, np.ones(hop) / hop, mode="same"))[::hop]
    db = 20 * np.log10(env + 1e-9)
    floor = db.max() - 35
    onsets = []
    for i in range(5, len(db)):
        if db[i] > floor and db[i] - db[i - 5:i].min() > 12:
            time = i * hop / RATE
            if not onsets or time - onsets[-1] > MIN_GAP:
                onsets.append(time)
    return onsets


x, rate = read_mono(SOURCE)
x = resample(x, rate)
onsets = find_onsets(x)
gain = 10 ** (-1 / 20) / np.max(np.abs(x))  # one gain for all clips, so their balance is kept

for old in glob.glob(os.path.join(OUTPUT_DIR, "S_Pogo_Charge_Click_*.wav")):
    os.remove(old)

clips = []
for index, onset in enumerate(onsets):
    start = max(0, int((onset - EDGE) * RATE))
    end = int((onsets[index + 1] - EDGE) * RATE) if index + 1 < len(onsets) else len(x)
    clip = x[start:end] * gain
    fade = min(int(RATE * FADE_OUT), len(clip))
    clip[-fade:] *= np.linspace(1, 0, fade)
    clips.append(clip)
    write(os.path.join(OUTPUT_DIR, f"S_Pogo_Charge_Click_{index + 1:02d}.wav"), clip)

# The first click lands when charging starts, the last one at full charge.
span = onsets[-1] - onsets[0]
positions = [(onset - onsets[0]) / span for onset in onsets]
print(f"{len(onsets)} clicks over {span:.3f} s of the recording")
for index, (onset, clip) in enumerate(zip(onsets, clips)):
    print(f"  {index + 1:02d}  at {onset:.3f} s  position {positions[index]:.3f}  {len(clip) / RATE * 1000:.0f} ms")
print("ChargeClickPositions = {" + ", ".join(f"{p:.3f}f" for p in positions) + "};")

if "--preview" in sys.argv:
    preview_dir = sys.argv[sys.argv.index("--preview") + 1]
    for charge_time in (1.0, 2.0, 5.0):
        out = np.zeros(int(RATE * (charge_time + 1.0)))
        for position, clip in zip(positions, clips):
            index = int(position * charge_time * RATE)
            out[index:index + len(clip)] += clip[:len(out) - index]
        write(os.path.join(preview_dir, f"charge_preview_{charge_time:g}s.wav"), out)
    print(f"Previews in {preview_dir}")
