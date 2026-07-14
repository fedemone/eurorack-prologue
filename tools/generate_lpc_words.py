#!/usr/bin/env python3
"""Generate mussola_words.cc: custom LPC10 word banks for Mussola.

Replaces the Plaits TI-ROM word banks with Italian opera fragments
(Madama Butterfly: "un bel di'", "bello", "giunto il tempo", "cosi'",
"fan", "tutto") and liturgical phrases ("kyrie eleison",
"om mani padme hum").

Pipeline:
  formant targets -> all-pole filter A(z) -> reflection coefficients
  (step-down recursion) -> quantization to the TMS5100-style codebooks
  used by plaits::LPCSpeechSynthWordBank -> LPC10 bitstream.

The bitstream is verified by round-tripping through a literal Python
port of LPCSpeechSynthWordBank::LoadNextWord()/Load(), and each
phoneme's lattice filter is verified against the direct-form 1/A(z)
impulse response (this also pins down the reflection-coefficient sign
convention of the plaits synthesis lattice).

Usage: python3 tools/generate_lpc_words.py [output.cc]
"""

import math
import sys

FS = 8000.0                 # LPC synth internal rate
FRAME_MS = 25.0             # 40 frames per second
BASE_PERIOD = 80            # 100 Hz average f0 (plaits normalization)
MAX_FRAMES_PER_BANK = 1024  # kLPCSpeechSynthMaxFrames
MAX_WORDS_PER_BANK = 31     # kLPCSpeechSynthMaxWords - 1 (boundary sentinel)

# ---------------------------------------------------------------------------
# Codebooks (must match LPCSpeechSynthWordBank luts in
# plaits_patches/lpc_speech_synth_controller.cc)
# ---------------------------------------------------------------------------

ENERGY_LUT = [0x00, 0x02, 0x03, 0x04, 0x05, 0x07, 0x0a, 0x0f,
              0x14, 0x20, 0x29, 0x39, 0x51, 0x72, 0xa1, 0xff]

PERIOD_LUT = [0, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
              31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 45, 47, 49,
              51, 53, 54, 57, 59, 61, 63, 66, 69, 71, 73, 77, 79, 81, 85, 87,
              92, 95, 99, 102, 106, 110, 115, 119, 123, 128, 133, 138, 143,
              149, 154, 160]

K0_LUT = [-32064, -31872, -31808, -31680, -31552, -31424, -31232, -30848,
          -30592, -30336, -30016, -29696, -29376, -28928, -28480, -27968,
          -26368, -24256, -21632, -18368, -14528, -10048, -5184, 0,
          5184, 10048, 14528, 18368, 21632, 24256, 26368, 27968]

K1_LUT = [-20992, -19328, -17536, -15552, -13440, -11200, -8768, -6272,
          -3712, -1088, 1536, 4160, 6720, 9216, 11584, 13824,
          15936, 17856, 19648, 21248, 22656, 24000, 25152, 26176,
          27072, 27840, 28544, 29120, 29632, 30080, 30464, 32384]

K2_LUT = [-110, -97, -83, -70, -56, -43, -29, -16, -2, 11, 25, 38, 52, 65,
          79, 92]
K3_LUT = [-82, -68, -54, -40, -26, -12, 1, 15, 29, 43, 57, 71, 85, 99, 113,
          126]
K4_LUT = [-82, -70, -59, -47, -35, -24, -12, -1, 11, 23, 34, 46, 57, 69, 81,
          92]
K5_LUT = [-64, -53, -42, -31, -20, -9, 3, 14, 25, 36, 47, 58, 69, 80, 91,
          102]
K6_LUT = [-77, -65, -53, -41, -29, -17, -5, 7, 19, 31, 43, 55, 67, 79, 90,
          102]
K7_LUT = [-64, -40, -16, 7, 31, 55, 79, 102]
K8_LUT = [-64, -44, -24, -4, 16, 37, 57, 77]
K9_LUT = [-51, -33, -15, 4, 22, 32, 59, 77]

K_LUTS = [K0_LUT, K1_LUT, K2_LUT, K3_LUT, K4_LUT, K5_LUT, K6_LUT, K7_LUT,
          K8_LUT, K9_LUT]
K_SCALES = [32768.0, 32768.0] + [128.0] * 8
K_BITS = [5, 5, 4, 4, 4, 4, 4, 3, 3, 3]


def nearest_index(lut, value):
    return min(range(len(lut)), key=lambda i: abs(lut[i] - value))


# ---------------------------------------------------------------------------
# Formant synthesis -> LPC reflection coefficients
# ---------------------------------------------------------------------------

def formants_to_a(formants):
    """Cascade 2nd-order resonators into direct-form A(z), order 10."""
    # Pad with broad, high resonances so every phoneme is order 10.
    padding = [(3300.0, 700.0), (3750.0, 900.0)]
    sections = list(formants)
    for pad in padding:
        if len(sections) >= 5:
            break
        sections.append(pad)
    while len(sections) < 5:
        sections.append((3600.0, 1200.0))

    a = [1.0]
    for f, bw in sections[:5]:
        r = math.exp(-math.pi * bw / FS)
        theta = 2.0 * math.pi * f / FS
        quad = [1.0, -2.0 * r * math.cos(theta), r * r]
        # polynomial multiply
        out = [0.0] * (len(a) + 2)
        for i, ai in enumerate(a):
            for j, qj in enumerate(quad):
                out[i + j] += ai * qj
        a = out
    assert len(a) == 11
    return a  # a[0] = 1


def step_down(a):
    """A(z) (a[0]=1, order 10) -> reflection coefficients k[0..9]."""
    a = list(a[1:])  # a_1..a_10
    m = len(a)
    ks = [0.0] * m
    for i in range(m, 0, -1):
        k = a[i - 1]
        ks[i - 1] = k
        if abs(k) >= 1.0:
            raise ValueError("unstable filter, |k|>=1: %f" % k)
        if i > 1:
            prev = [0.0] * (i - 1)
            for j in range(i - 1):
                prev[j] = (a[j] - k * a[i - 2 - j]) / (1.0 - k * k)
            a = prev
    return ks


def lattice_impulse_response(ks, n, sign):
    """Simulate the exact plaits synthesis lattice for an impulse."""
    k = [sign * x for x in ks]
    s = [0.0] * 11
    out = []
    for t in range(n):
        e = [0.0] * 11
        e[10] = 1.0 if t == 0 else 0.0
        for j in range(9, -1, -1):
            e[j] = e[j + 1] - k[j] * s[j]
        for j in range(9, 0, -1):
            s[j] = s[j - 1] + k[j - 1] * e[j - 1]
        s[0] = e[0]
        out.append(e[0])
    return out


def direct_form_impulse_response(a, n):
    """Impulse response of 1/A(z)."""
    y = []
    for t in range(n):
        acc = 1.0 if t == 0 else 0.0
        for j in range(1, len(a)):
            if t - j >= 0:
                acc -= a[j] * y[t - j]
        y.append(acc)
    return y


def resolve_lattice_sign():
    """Find the reflection-coefficient sign the plaits lattice expects."""
    a = formants_to_a([(700.0, 130.0), (1220.0, 120.0), (2600.0, 160.0)])
    ks = step_down(a)
    ref = direct_form_impulse_response(a, 64)
    best_sign, best_err = None, None
    for sign in (1.0, -1.0):
        ir = lattice_impulse_response(ks, 64, sign)
        err = max(abs(x - y) for x, y in zip(ir, ref))
        if best_err is None or err < best_err:
            best_sign, best_err = sign, err
    if best_err > 1e-6:
        raise AssertionError(
            "lattice does not match direct form (err=%g)" % best_err)
    return best_sign


# ---------------------------------------------------------------------------
# Phoneme inventory (formant frequencies/bandwidths in Hz)
# voiced=False -> noise excitation (period 0), only k0..k3 transmitted
# ---------------------------------------------------------------------------

PHONEMES = {
    # vowels
    'a':  dict(formants=[(700, 130), (1220, 120), (2600, 160)],
               voiced=True, energy=1.0),
    'e':  dict(formants=[(480, 100), (1900, 120), (2500, 150)],
               voiced=True, energy=1.0),
    'i':  dict(formants=[(300, 90), (2250, 110), (3000, 170)],
               voiced=True, energy=0.9),
    'o':  dict(formants=[(500, 100), (900, 110), (2500, 150)],
               voiced=True, energy=1.0),
    'u':  dict(formants=[(320, 90), (800, 100), (2400, 160)],
               voiced=True, energy=0.9),
    # sonorants
    'n':  dict(formants=[(250, 80), (1700, 300), (2600, 300)],
               voiced=True, energy=0.4),
    'm':  dict(formants=[(250, 80), (1100, 300), (2200, 300)],
               voiced=True, energy=0.4),
    'l':  dict(formants=[(360, 90), (1300, 150), (3000, 250)],
               voiced=True, energy=0.55),
    'r':  dict(formants=[(490, 130), (1350, 200), (2000, 300)],
               voiced=True, energy=0.45),
    # voiced stop murmurs (closure voicing)
    'b':  dict(formants=[(200, 150), (800, 300)],
               voiced=True, energy=0.25),
    'd':  dict(formants=[(200, 150), (1700, 300)],
               voiced=True, energy=0.25),
    # unvoiced fricatives / bursts (noise excitation)
    's':  dict(formants=[(3400, 300), (2600, 400)],
               voiced=False, energy=0.45),
    'z':  dict(formants=[(3300, 300), (2500, 400)],
               voiced=False, energy=0.35),
    'f':  dict(formants=[(2000, 800), (3500, 900)],
               voiced=False, energy=0.3),
    'S':  dict(formants=[(2200, 300), (3000, 400)],   # esh, for "gi(unto)"
               voiced=False, energy=0.4),
    'T':  dict(formants=[(3000, 600), (2000, 500)],   # t/d burst
               voiced=False, energy=0.35),
    'K':  dict(formants=[(1800, 400), (2600, 500)],   # k burst
               voiced=False, energy=0.35),
    'P':  dict(formants=[(900, 600), (2000, 800)],    # p burst
               voiced=False, energy=0.3),
    'h':  dict(formants=[(1000, 600), (3000, 800)],
               voiced=False, energy=0.2),
    # silence (stop closure / word gap)
    '_':  dict(formants=[], voiced=False, energy=0.0),
}


def quantize_phoneme(name, sign):
    """Phoneme -> quantized k indices + LUT values."""
    p = PHONEMES[name]
    if p['energy'] == 0.0:
        return None
    a = formants_to_a(p['formants'])
    ks = [sign * k for k in step_down(a)]
    idx, val = [], []
    for j in range(10):
        want = ks[j] * K_SCALES[j]
        i = nearest_index(K_LUTS[j], want)
        idx.append(i)
        val.append(K_LUTS[j][i])
    return idx, val


# ---------------------------------------------------------------------------
# Phrase definitions
# Each segment: (phoneme, frames, energy_scale, pitch_semitones)
# pitch may be a single number or a (start, end) glide across the segment.
# ---------------------------------------------------------------------------

def seg(ph, n, en=1.0, st=0.0):
    return (ph, n, en, st)


PHRASES = {
    # --- Madama Butterfly fragments ---
    'un bel di': [
        seg('u', 4, 0.9, 0.0), seg('n', 3, 0.5, 0.0),
        seg('_', 1), seg('b', 1, 0.3), seg('e', 6, 1.0, 0.0),
        seg('l', 3, 0.6, 0.0),
        seg('_', 1), seg('d', 1, 0.3), seg('T', 1, 0.5),
        seg('i', 12, 1.0, (2.0, 4.0)), seg('i', 4, 0.7, (4.0, 3.0)),
    ],
    'bello': [
        seg('b', 1, 0.3), seg('e', 8, 1.0, (0.0, 1.0)),
        seg('l', 4, 0.6, 1.0), seg('o', 9, 1.0, (0.0, -1.0)),
    ],
    'giunto il tempo': [
        seg('S', 2, 0.8), seg('u', 6, 1.0, 0.0), seg('n', 3, 0.5),
        seg('_', 1), seg('T', 1, 0.5), seg('o', 5, 0.9, 0.0),
        seg('i', 3, 0.7, 0.0), seg('l', 3, 0.5),
        seg('_', 1), seg('T', 1, 0.6), seg('e', 6, 1.0, (1.0, 0.0)),
        seg('m', 3, 0.5), seg('_', 1), seg('P', 1, 0.5),
        seg('o', 8, 1.0, (0.0, -2.0)),
    ],
    'cosi': [
        seg('K', 1, 0.7), seg('o', 5, 0.9, 0.0), seg('z', 2, 0.6),
        seg('i', 11, 1.0, (3.0, 3.0)), seg('i', 3, 0.7, (3.0, 2.0)),
    ],
    'fan': [
        seg('f', 3, 0.8), seg('a', 11, 1.0, (0.0, 1.0)),
        seg('n', 5, 0.5, (1.0, 0.0)),
    ],
    'tutto': [
        seg('T', 1, 0.7), seg('u', 7, 1.0, 0.0),
        seg('_', 2), seg('T', 1, 0.7), seg('o', 9, 1.0, (0.0, -1.0)),
    ],
    # --- Kyrie ---
    'kyrie': [
        seg('K', 1, 0.7), seg('i', 4, 0.9, 0.0), seg('r', 1, 0.5),
        seg('i', 4, 0.9, (2.0, 2.0)), seg('e', 7, 1.0, (0.0, -1.0)),
    ],
    'eleison': [
        seg('e', 5, 1.0, 0.0), seg('l', 2, 0.5), seg('e', 5, 1.0, 2.0),
        seg('i', 3, 0.8, 2.0), seg('s', 3, 0.5),
        seg('o', 6, 1.0, (0.0, -2.0)), seg('n', 4, 0.4, -2.0),
    ],
    # --- Om mani padme hum ---
    'om': [
        seg('o', 9, 1.0, 0.0), seg('m', 9, 0.6, (0.0, -1.0)),
    ],
    'mani': [
        seg('m', 3, 0.5), seg('a', 7, 1.0, 0.0),
        seg('n', 3, 0.5), seg('i', 7, 0.9, (0.0, -1.0)),
    ],
    'padme': [
        seg('P', 1, 0.6), seg('a', 7, 1.0, 0.0), seg('d', 1, 0.3),
        seg('m', 3, 0.5), seg('e', 7, 0.9, (0.0, -1.0)),
    ],
    'hum': [
        seg('h', 2, 0.6), seg('u', 9, 1.0, 0.0),
        seg('m', 9, 0.6, (0.0, -2.0)),
    ],
}

# Concatenated liturgical phrases
PHRASES['kyrie eleison'] = (PHRASES['kyrie'] + [seg('_', 2)] +
                            PHRASES['eleison'])
PHRASES['om mani padme hum'] = (PHRASES['om'] + [seg('_', 2)] +
                                PHRASES['mani'] + [seg('_', 2)] +
                                PHRASES['padme'] + [seg('_', 2)] +
                                PHRASES['hum'])

# Bank layout: Harmonics selects the bank, Phoneme/Morph selects the word.
BANKS = [
    ('Puccini I',  ['un bel di', 'bello']),
    ('Puccini II', ['giunto il tempo', 'cosi']),
    ('Puccini III', ['fan', 'tutto']),
    ('Kyrie',      ['kyrie', 'eleison', 'kyrie eleison']),
    ('Mantra',     ['om', 'mani', 'padme', 'hum', 'om mani padme hum']),
]


# ---------------------------------------------------------------------------
# Frame expansion
# ---------------------------------------------------------------------------

def pitch_at(st, i, n):
    if isinstance(st, tuple):
        t = i / max(1, n - 1) if n > 1 else 0.0
        return st[0] + (st[1] - st[0]) * t
    return st


def phrase_to_frames(segments, sign):
    """Expand a phrase into decoder-level frames.

    Each frame: dict(energy_idx, period_idx, k_idx or None(repeat/silence))

    A silence frame is appended to every phrase: after a word ends, the
    LPC controller keeps replaying the LAST frame indefinitely, so the
    last frame must be silent or the word turns into a drone.
    """
    frames = []
    prev_kidx = None
    segments = list(segments) + [seg('_', 1)]
    for ph, n, en, st in segments:
        p = PHONEMES[ph]
        base_en = p['energy'] * en
        q = quantize_phoneme(ph, sign)
        for i in range(n):
            if base_en <= 0.0:
                frames.append(dict(eidx=0, pidx=0, kidx=None))
                prev_kidx = None
                continue
            # energy: gentle attack/release inside the segment
            shape = 1.0
            if n >= 4:
                if i == 0:
                    shape = 0.7
                elif i == n - 1:
                    shape = 0.8
            eb = min(255, int(round(base_en * shape * 170)))
            eidx = nearest_index(ENERGY_LUT[1:15], eb) + 1  # 1..14 only
            if p['voiced']:
                semi = pitch_at(st, i, n)
                period = BASE_PERIOD / (2.0 ** (semi / 12.0))
                pidx = nearest_index(PERIOD_LUT[1:], period) + 1
            else:
                pidx = 0
            kidx = q[0]
            # repeat-frame compression: same k's as previous frame
            if kidx == prev_kidx:
                frames.append(dict(eidx=eidx, pidx=pidx, kidx=None))
            else:
                frames.append(dict(eidx=eidx, pidx=pidx, kidx=list(kidx)))
                prev_kidx = kidx
    return frames


# ---------------------------------------------------------------------------
# Bitstream writer (inverse of BitStream/LoadNextWord in
# lpc_speech_synth_controller.cc)
# ---------------------------------------------------------------------------

class BitWriter:
    def __init__(self):
        self.bytes = bytearray()
        self.acc = 0
        self.nbits = 0

    def put(self, value, bits):
        for b in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> b) & 1)
            self.nbits += 1
            if self.nbits == 8:
                self.bytes.append(REVERSE[self.acc])
                self.acc = 0
                self.nbits = 0

    def flush(self):
        if self.nbits:
            self.acc <<= (8 - self.nbits)
            self.bytes.append(REVERSE[self.acc])
            self.acc = 0
            self.nbits = 0


def _reverse_byte(b):
    b = ((b >> 4) | (b << 4)) & 0xff
    b = (((b & 0xcc) >> 2) | ((b & 0x33) << 2)) & 0xff
    b = (((b & 0xaa) >> 1) | ((b & 0x55) << 1)) & 0xff
    return b


REVERSE = [_reverse_byte(i) for i in range(256)]


def encode_word(frames):
    w = BitWriter()
    for f in frames:
        w.put(f['eidx'], 4)
        if f['eidx'] == 0:
            continue
        repeat = 1 if f['kidx'] is None else 0
        w.put(repeat, 1)
        w.put(f['pidx'], 6)
        if not repeat:
            k = f['kidx']
            w.put(k[0], 5)
            w.put(k[1], 5)
            w.put(k[2], 4)
            w.put(k[3], 4)
            if f['pidx'] != 0:
                w.put(k[4], 4)
                w.put(k[5], 4)
                w.put(k[6], 4)
                w.put(k[7], 3)
                w.put(k[8], 3)
                w.put(k[9], 3)
    w.put(0xF, 4)  # end-of-word marker
    w.flush()
    return bytes(w.bytes)


# ---------------------------------------------------------------------------
# Reference decoder: literal port of LoadNextWord()/Load()
# ---------------------------------------------------------------------------

class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.available = 0
        self.bits = 0

    def get(self, num_bits):
        shift = num_bits
        if num_bits > self.available:
            self.bits = (self.bits << self.available) & 0xffff
            shift -= self.available
            self.bits |= REVERSE[self.data[self.pos]]
            self.pos += 1
            self.available += 8
        self.bits = (self.bits << shift) & 0xffff
        result = self.bits >> 8
        self.bits &= 0xff
        self.available -= num_bits
        return result

    def flush(self):
        while self.available:
            self.get(1)


def decode_word(data):
    """Returns (frames, consumed). Frame fields mirror LPCSpeechSynth::Frame."""
    r = BitReader(data)
    frame = dict(energy=0, period=0,
                 k=[0] * 10)  # persistent across frames, like upstream
    frames = []
    while True:
        energy = r.get(4)
        if energy == 0:
            frame['energy'] = 0
        elif energy == 0xF:
            r.flush()
            break
        else:
            frame['energy'] = ENERGY_LUT[energy]
            repeat = r.get(1)
            frame['period'] = PERIOD_LUT[r.get(6)]
            if not repeat:
                frame['k'][0] = K0_LUT[r.get(5)]
                frame['k'][1] = K1_LUT[r.get(5)]
                frame['k'][2] = K2_LUT[r.get(4)]
                frame['k'][3] = K3_LUT[r.get(4)]
                if frame['period']:
                    frame['k'][4] = K4_LUT[r.get(4)]
                    frame['k'][5] = K5_LUT[r.get(4)]
                    frame['k'][6] = K6_LUT[r.get(4)]
                    frame['k'][7] = K7_LUT[r.get(3)]
                    frame['k'][8] = K8_LUT[r.get(3)]
                    frame['k'][9] = K9_LUT[r.get(3)]
        frames.append(dict(energy=frame['energy'], period=frame['period'],
                           k=list(frame['k'])))
    return frames, r.pos


def decode_bank(data):
    words = []
    pos = 0
    while pos < len(data):
        frames, consumed = decode_word(data[pos:])
        words.append(frames)
        pos += consumed
    return words


# ---------------------------------------------------------------------------
# Synthesis smoke test (port of LPCSpeechSynth::Render essentials)
# ---------------------------------------------------------------------------

def synthesize(frames, samples_per_frame=200):
    """Rough render: pulse/noise excitation through the lattice."""
    import random
    rnd = random.Random(42)
    s = [0.0] * 11
    out = []
    phase = 0.0
    for fr in frames:
        energy = fr['energy'] / 256.0
        voiced = fr['period'] != 0
        f = (1.0 / fr['period']) if voiced else 0.0
        k = [fr['k'][0] / 32768.0, fr['k'][1] / 32768.0] + \
            [fr['k'][j] / 128.0 for j in range(2, 10)]
        for _ in range(samples_per_frame):
            if voiced:
                phase += f
                exc = 0.0
                if phase >= 1.0:
                    phase -= 1.0
                    exc = energy * 6.0
            else:
                exc = (energy if rnd.random() > 0.5 else -energy)
            e = [0.0] * 11
            e[10] = exc * 1.5
            for j in range(9, -1, -1):
                e[j] = e[j + 1] - k[j] * s[j]
            e[0] = max(-2.0, min(2.0, e[0]))
            for j in range(9, 0, -1):
                s[j] = s[j - 1] + k[j - 1] * e[j - 1]
            s[0] = e[0]
            out.append(e[0])
    return out


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else 'mussola_words.cc'

    sign = resolve_lattice_sign()
    print('lattice reflection sign: %+g' % sign)

    # verify each phoneme's lattice is stable and matches its direct form
    for name in PHONEMES:
        if PHONEMES[name]['energy'] == 0.0:
            continue
        a = formants_to_a(PHONEMES[name]['formants'])
        ks = step_down(a)
        assert all(abs(k) < 1.0 for k in ks), name

    bank_blobs = []
    total_bytes = 0
    for bank_name, words in BANKS:
        blob = bytearray()
        encoded_frames = []
        for w in words:
            frames = phrase_to_frames(PHRASES[w], sign)
            encoded_frames.append(frames)
            blob += encode_word(frames)
        # bounds
        n_frames = sum(len(f) for f in encoded_frames)
        assert n_frames <= MAX_FRAMES_PER_BANK, (bank_name, n_frames)
        assert len(words) <= MAX_WORDS_PER_BANK, bank_name
        # round-trip
        decoded = decode_bank(bytes(blob))
        assert len(decoded) == len(words), \
            (bank_name, len(decoded), len(words))
        for wi, (enc, dec) in enumerate(zip(encoded_frames, decoded)):
            assert len(enc) == len(dec), (bank_name, words[wi],
                                          len(enc), len(dec))
            stale_k = [0] * 10
            for fi, (ef, df) in enumerate(zip(enc, dec)):
                if ef['eidx'] == 0:
                    assert df['energy'] == 0
                    continue
                assert df['energy'] == ENERGY_LUT[ef['eidx']]
                assert df['period'] == PERIOD_LUT[ef['pidx']]
                if ef['kidx'] is not None:
                    for j in range(10):
                        if ef['pidx'] != 0 or j < 4:
                            assert df['k'][j] == K_LUTS[j][ef['kidx'][j]], \
                                (bank_name, words[wi], fi, j)
            # synthesis smoke test: finite, audible output
            audio = synthesize(dec)
            peak = max(abs(x) for x in audio)
            rms = math.sqrt(sum(x * x for x in audio) / len(audio))
            assert all(math.isfinite(x) for x in audio), (bank_name, words[wi])
            assert peak < 2.5, (bank_name, words[wi], peak)
            assert rms > 0.001, (bank_name, words[wi], rms)
        seconds = n_frames * FRAME_MS / 1000.0
        print('bank %-11s: %d words, %4d frames (%.2fs), %5d bytes' %
              (bank_name, len(words), n_frames, seconds, len(blob)))
        total_bytes += len(blob)
        bank_blobs.append((bank_name, words, bytes(blob)))
    print('total: %d bytes' % total_bytes)

    # ---- emit C++ ----
    lines = []
    lines.append('/*')
    lines.append(' * File: mussola_words.cc  (GENERATED - do not edit)')
    lines.append(' *')
    lines.append(' * Generated by tools/generate_lpc_words.py.')
    lines.append(' *')
    lines.append(' * Custom LPC10 word banks for the Mussola vocal synth,')
    lines.append(' * replacing the Plaits TI-ROM banks. Defines')
    lines.append(' * plaits::word_banks_, so the upstream')
    lines.append(' * lpc_speech_synth_words.cc must NOT be linked in.')
    lines.append(' *')
    for name, words, blob in bank_blobs:
        lines.append(' *   %-11s: %s' % (name, ', '.join('"%s"' % w
                                                         for w in words)))
    lines.append(' */')
    lines.append('')
    lines.append('#include "plaits/dsp/speech/lpc_speech_synth_words.h"')
    lines.append('')
    lines.append('namespace plaits {')
    lines.append('')
    for i, (name, words, blob) in enumerate(bank_blobs):
        lines.append('/* %s: %s */' % (name, ', '.join('"%s"' % w
                                                       for w in words)))
        lines.append('static const uint8_t kMussolaBank%d[%d] = {' %
                     (i, len(blob)))
        for off in range(0, len(blob), 12):
            chunk = blob[off:off + 12]
            lines.append('  ' + ' '.join('0x%02x,' % b for b in chunk))
        lines.append('};')
        lines.append('')
    lines.append('LPCSpeechSynthWordBankData '
                 'word_banks_[LPC_SPEECH_SYNTH_NUM_WORD_BANKS] = {')
    for i, (name, words, blob) in enumerate(bank_blobs):
        lines.append('  { kMussolaBank%d, sizeof(kMussolaBank%d) },' % (i, i))
    lines.append('};')
    lines.append('')
    lines.append('}  // namespace plaits')
    lines.append('')

    with open(out_path, 'w') as f:
        f.write('\n'.join(lines))
    print('wrote %s' % out_path)


if __name__ == '__main__':
    main()
