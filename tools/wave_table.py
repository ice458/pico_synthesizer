import numpy as np


def sin_table(bits=16, length=256):
    return np.round(
        (2 ** (bits - 1) - 1) * np.sin(np.linspace(0, 2 * np.pi, length)), 0
    ).astype(int)


def sawtooth_table(bits=16, length=256):
    return np.round((2 ** (bits - 1) - 1) * (np.linspace(-1, 1, length)), 0).astype(int)


def triangle_table(bits=16, length=256):
    return np.round(
        (2 ** (bits - 1) - 1) * (np.abs(np.linspace(-2, 2, length)) - 1), 0
    ).astype(int)


def square_table(bits=16, length=256):
    return np.round(
        (2 ** (bits - 1) - 1) * np.sign(np.sin(np.linspace(0, 2 * np.pi, length))), 0
    ).astype(int)


def noise_table(bits=16, length=256):
    return np.random.randint(-1 * (2 ** (bits - 1) - 1), 2 ** (bits - 1), length)


def midi_to_frequency(midi_note):
    # Calculate the frequency corresponding to the MIDI note number
    return 440.0 * 2.0 ** ((midi_note - 69) / 12.0)


# Generate a list of frequencies corresponding to MIDI note numbers 0 to 127
midi_notes = np.arange(128)  # MIDI note numbers from 0 to 127
frequencies = np.array(
    [midi_to_frequency(note) for note in midi_notes]
)  # Frequencies corresponding to MIDI note numbers

# generate the tables
bits = 16
length = 4096
sin_table = sin_table(bits, length)
sawtooth_table = sawtooth_table(bits, length)
triangle_table = triangle_table(bits, length)
square_table = square_table(bits, length)
noise_table = noise_table(bits, length)

# Convert to fixed point (8-bit fractional part)
readindex = np.int64(frequencies * length / 40000 * 2**8)
print("readindex: ", readindex)

# write the tables to file
with open("wave_table.h", "w") as f:
    f.write("#ifndef WAVE_TABLE_H\n")
    f.write("#define WAVE_TABLE_H\n")
    f.write("#include <stdint.h>\n")
    f.write('#include "fp.h"\n')
    f.write("#define TABLE_LENGTH {}\n".format(length))
    f.write("static const fp_t sin_table[] = {")
    f.write(",".join([str(x) for x in sin_table]))
    f.write("};")
    f.write("\n")
    f.write("static const fp_t sawtooth_table[] = {")
    f.write(",".join([str(x) for x in sawtooth_table]))
    f.write("};")
    f.write("\n")
    f.write("static const fp_t triangle_table[] = {")
    f.write(",".join([str(x) for x in triangle_table]))
    f.write("};")
    f.write("\n")
    f.write("static const fp_t square_table[] = {")
    f.write(",".join([str(x) for x in square_table]))
    f.write("};")
    f.write("\n")
    f.write("static const fp_t noise_table[] = {")
    f.write(",".join([str(x) for x in noise_table]))
    f.write("};")
    f.write("\n")
    f.write("static const q8_t increment_table[] = {")
    f.write(",".join([str(x) for x in readindex]))
    f.write("};")
    f.write("\n")

    f.write("#endif\\n")

print(
    "Total size of tables: {} bytes".format(
        sum(
            [
                x.nbytes
                for x in [
                    sin_table,
                    sawtooth_table,
                    triangle_table,
                    square_table,
                    noise_table,
                ]
            ]
        )
    )
)
print("Tables written to wave_table.h")
