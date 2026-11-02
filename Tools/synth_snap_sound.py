# Synthesizes a bare metal mechanism working, a lever, a sliding bolt, a ratchet and a heavy lock,
# into C:\SF\PogoStick-assets\S_Pogo_Snap.wav. Re-runnable; same seed, same file.
#   python Tools/synth_snap_sound.py
#
# Each steel part is an impulse response with many dense, damped modes, including close pairs
# that beat. Few clean, long-ringing modes is what made the first draft sound like glass. Hits
# are short noise bursts rather than clean impulses, so contact sounds gritty.
import os
import wave

import numpy as np

OUTPUT = r"C:\SF\PogoStick-assets\S_Pogo_Snap.wav"
RATE = 48000
DURATION = 0.9
rng = np.random.default_rng(23)


def part(low, high, count, decay, length=0.25):
    """Impulse response of a steel part: `count` modes log-spread over low..high Hz."""
    t = np.arange(int(RATE * length)) / RATE
    response = np.zeros_like(t)
    for freq in np.exp(rng.uniform(np.log(low), np.log(high), count)):
        # Higher modes die faster; each has a slightly detuned twin so it beats instead of ringing pure.
        mode_decay = decay * (low / freq) ** 0.5 * rng.uniform(0.6, 1.4)
        amp = rng.uniform(0.3, 1.0) / np.sqrt(count)
        for detune in (1.0, 1.0 + rng.uniform(0.004, 0.015)):
            response += amp * np.sin(2 * np.pi * freq * detune * t + rng.uniform(0, 2 * np.pi)) * np.exp(-t / mode_decay)
    return response


def convolve(signal, response):
    size = len(signal) + len(response)
    return np.fft.irfft(np.fft.rfft(signal, size) * np.fft.rfft(response, size), size)[:len(signal)]


lever = part(900, 6500, 24, 0.020)    # small, light: a dry click
bolt = part(350, 4500, 30, 0.035)     # mid-sized sliding bolt
frame = part(140, 2800, 36, 0.060)    # heavy body that takes the final hit

samples = int(RATE * DURATION)
hits = {name: np.zeros(samples) for name in ("lever", "bolt", "frame")}


def hit(name, start, gain, length=0.003):
    index = int(start * RATE)
    count = int(RATE * length)
    burst = rng.standard_normal(count) * np.exp(-np.linspace(0, 5, count))
    hits[name][index:index + count] += gain * burst


def scrape(name, start, end, gain):
    """Steel sliding on steel: grit with irregular stick-slip catches."""
    index, stop = int(start * RATE), int(end * RATE)
    grit = rng.standard_normal(stop - index) * 0.06
    position = 0
    while position < len(grit) - 100:
        grit[position:position + 60] += rng.standard_normal(60) * rng.uniform(0.2, 0.6)
        position += int(RATE * rng.uniform(0.002, 0.007))
    envelope = np.sin(np.linspace(0, np.pi, len(grit))) ** 0.5
    hits[name][index:stop] += gain * grit * envelope


# The mechanism, in order.
hit("lever", 0.000, 0.8)                        # lever thrown
hit("lever", 0.012, 0.4)                        # and its bounce
scrape("bolt", 0.050, 0.160, 0.5)               # bolt slides back
hit("bolt", 0.165, 1.0, 0.004)                  # bolt hits its stop: clack
hit("frame", 0.165, 0.3, 0.004)
for i, start in enumerate((0.250, 0.285, 0.318, 0.349)):  # ratchet pawl over four teeth
    hit("lever", start + rng.uniform(-0.004, 0.004), 0.55 + 0.1 * i)
scrape("bolt", 0.395, 0.470, 0.4)               # bolt driven home
hit("frame", 0.475, 1.4, 0.006)                 # heavy lock: clank
hit("bolt", 0.475, 0.8, 0.005)
hit("frame", 0.497, 0.35, 0.003)                # rebound off the lock
hit("lever", 0.540, 0.25)                       # settle
hit("lever", 0.562, 0.12)

out = convolve(hits["lever"], lever) + convolve(hits["bolt"], bolt) + convolve(hits["frame"], frame)

# Soft saturation: heavy contacts clip a little, like parts rattling against each other.
out = np.tanh(out * 3.0 / np.max(np.abs(out))) / np.tanh(3.0)

# Fade the tail out and normalise to -1 dBFS.
tail = int(RATE * 0.15)
out[-tail:] *= np.linspace(1, 0, tail)
out *= 10 ** (-1 / 20) / np.max(np.abs(out))

os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
with wave.open(OUTPUT, "wb") as file:
    file.setnchannels(1)
    file.setsampwidth(2)
    file.setframerate(RATE)
    file.writeframes((out * 32767).astype("<i2").tobytes())
print(f"Wrote {OUTPUT} ({DURATION} s, {RATE} Hz)")
