# Synthesizes a steel spring straining under load into C:\SF\PogoStick-assets\S_Pogo_Charge.wav.
# Re-runnable; same seed, same file. Was the charge sound before the knob clicks
# (Tools/slice_charge_clicks.py); kept as a 2 s alternative, no longer imported.
#   python Tools/synth_charge_sound.py
import os
import wave

import numpy as np

OUTPUT = r"C:\SF\PogoStick-assets\S_Pogo_Charge.wav"
RATE = 48000
DURATION = 2.0
rng = np.random.default_rng(7)

t = np.arange(int(RATE * DURATION)) / RATE
progress = t / DURATION


def biquad_bandpass(signal, freq, q):
    """RBJ constant-peak bandpass; freq may vary per sample."""
    freq = np.broadcast_to(freq, signal.shape)
    out = np.zeros_like(signal)
    x1 = x2 = y1 = y2 = 0.0
    for i, x in enumerate(signal):
        w = 2 * np.pi * freq[i] / RATE
        alpha = np.sin(w) / (2 * q)
        a0 = 1 + alpha
        y = (alpha * x - alpha * x2 + 2 * np.cos(w) * y1 - (1 - alpha) * y2) / a0
        out[i] = y
        x2, x1, y2, y1 = x1, x, y1, y
    return out


# Excitation: metal-on-metal friction. Steady grit plus irregular stick-slip grains that get
# denser and harder as the load builds. No tones of its own; the resonators below make the iron.
excitation = rng.standard_normal(t.size) * (0.04 + 0.10 * progress)
time = 0.02
while time < DURATION - 0.05:
    p = time / DURATION
    index = int(time * RATE)
    length = int(RATE * rng.uniform(0.002, 0.008))
    grain = rng.standard_normal(length) * np.exp(-np.linspace(0, 5, length))
    excitation[index:index + length] += grain * (0.3 + 0.7 * p) * rng.uniform(0.4, 1.0)
    time += 1 / (14 + 30 * p) * rng.uniform(0.5, 1.5)

# Resonators: the inharmonic modes of a steel bar, narrow and fixed, drifting up only a few
# percent under load. A pitch glide or wobble is what made the first draft sound cartoony.
drift = 1 + 0.03 * progress
modes = ((240, 1.0, 90), (662, 0.7, 110), (1296, 0.45, 130), (2143, 0.30, 150), (3202, 0.18, 160))
iron = sum(gain * biquad_bandpass(excitation, freq * drift, q) for freq, gain, q in modes)
iron *= 0.25 + 0.75 * progress ** 1.2

# Full-charge stop: a short dry metal tick, run through the same modes so it belongs to the spring.
latch_start = int(RATE * (DURATION - 0.07))
latch_length = int(RATE * 0.06)
tick = np.zeros(latch_length)
tick[:int(RATE * 0.002)] = rng.standard_normal(int(RATE * 0.002))
latch = sum(gain * biquad_bandpass(tick, freq * drift[-1], q / 3) for freq, gain, q in modes)
iron[latch_start:latch_start + latch_length] += latch * 1.5 * np.exp(-np.linspace(0, 6, latch_length))

# Short fade in/out so it never starts or stops on a pop, then normalise to -1 dBFS.
fade = int(RATE * 0.02)
iron[:fade] *= np.linspace(0, 1, fade)
iron[-fade // 2:] *= np.linspace(1, 0, fade // 2)
iron *= 10 ** (-1 / 20) / np.max(np.abs(iron))

os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
with wave.open(OUTPUT, "wb") as file:
    file.setnchannels(1)
    file.setsampwidth(2)
    file.setframerate(RATE)
    file.writeframes((iron * 32767).astype("<i2").tobytes())
print(f"Wrote {OUTPUT} ({DURATION} s, {RATE} Hz)")
