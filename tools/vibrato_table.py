import numpy as np


def midi_to_lfo_freq(v):
    fmin = 5  # Minimum frequency in Hz
    fmax = 14  # Maximum frequency in Hz
    return fmin + v / 127 * (fmax - fmin)


def vibrato_table(bits=16, length=256):
    # Generate a vibrato table based on MIDI LFO frequency
    num = np.arange(128)  # MIDI numbers from 0 to 127
    frequencies = np.array([midi_to_lfo_freq(i) for i in num])
    # Q8 format conversion
    readindex = np.int64(frequencies * 4096 / 40000 * 2**8)
    print("readindex: ", readindex)
    return readindex


# Generate the vibrato table
bits = 16
length = 128
vibrato_table = vibrato_table(bits, length)
# Write the vibrato table to a file
with open("vibrato_table.h", "w") as f:
    f.write("#ifndef VIBRATO_TABLE_H\n")
    f.write("#define VIBRATO_TABLE_H\n")
    f.write('#include "fp.h"\n\n')
    f.write("#define VIBRATO_TABLE_LENGTH {}\n".format(length))
    f.write("static const q8_t vibrato_table[] = {")
    f.write(",".join([str(x) for x in vibrato_table]))
    f.write("};\n")
    f.write("#endif // VIBRATO_TABLE_H\n")
