import math

# Q format (8 fractional bits)
Q = 8
ORIGINAL_TABLE_SIZE = 16384  # 2^14 for MIDI pitch bend (0 to 16383)
CENTER_BEND_VALUE_ORIGINAL = ORIGINAL_TABLE_SIZE / 2.0  # Should be 8192.0

INTERPOLATED_TABLE_SIZE = 256  # Interpolated table size (e.g., 256)
# INTERPOLATED_TABLE_SIZE = 128 # If you want to make it smaller

output_filename = "pitch_bend_table_interpolated.h"  # New header file name
all_tables_data_interpolated = []

# Calculate all tables first
for current_max_semitones in range(25):  # Loop from 0 to 24 semitones
    MAX_SEMITONES_PER_DIRECTION = current_max_semitones

    current_interpolated_table = []
    for i in range(INTERPOLATED_TABLE_SIZE):
        # Map interpolated index 'i' back to an effective original bend_value
        # This ensures the interpolated table covers the full bend range correctly.
        if INTERPOLATED_TABLE_SIZE == 1:  # Avoid division by zero if table size is 1
            effective_bend_value = CENTER_BEND_VALUE_ORIGINAL
        else:
            # Ensures i=0 maps to 0, i=INTERPOLATED_TABLE_SIZE-1 maps to ORIGINAL_TABLE_SIZE-1
            effective_bend_value = (
                i * (ORIGINAL_TABLE_SIZE - 1) / (INTERPOLATED_TABLE_SIZE - 1)
            )

        normalized_bend_factor = (
            effective_bend_value - CENTER_BEND_VALUE_ORIGINAL
        ) / CENTER_BEND_VALUE_ORIGINAL

        semitones = normalized_bend_factor * MAX_SEMITONES_PER_DIRECTION

        if MAX_SEMITONES_PER_DIRECTION == 0:
            frequency_factor = 1.0
        else:
            frequency_factor = 2 ** (semitones / 12.0)

        q8_factor = int(round(frequency_factor * (2**Q)))
        current_interpolated_table.append(q8_factor)
    all_tables_data_interpolated.append(current_interpolated_table)

# Write directly to the C header file
try:
    with open(output_filename, "w") as f:
        f.write("#ifndef PITCH_BEND_TABLE_INTERPOLATED_H\n")
        f.write("#define PITCH_BEND_TABLE_INTERPOLATED_H\n\n")
        f.write('#include "fp.h"\n\n')
        f.write(
            f"#define PITCH_BEND_INTERPOLATED_TABLE_SIZE {INTERPOLATED_TABLE_SIZE}\n"
        )
        f.write(f"#define PITCH_BEND_ORIGINAL_TABLE_SIZE {ORIGINAL_TABLE_SIZE}\n\n")

        f.write(f"// Pitch bend factors (interpolated): Q{Q} format\n")
        f.write(
            f"// 2D Array: pitch_bend_factors_interpolated[max_semitones_0_to_24][bend_value_0_to_{INTERPOLATED_TABLE_SIZE-1}]\n"
        )
        f.write(f"// Dimensions: [25][{INTERPOLATED_TABLE_SIZE}]\n")
        # Make sure the new table name is used here
        f.write(
            f"static const q8_t pitch_bend_factors_interpolated[25][{INTERPOLATED_TABLE_SIZE}] = {{\n"
        )

        for semitone_idx, table_data in enumerate(all_tables_data_interpolated):
            f.write(f"    {{ // Table for +/- {semitone_idx} semitones\n")
            f.write("        ")
            for i, factor in enumerate(table_data):
                f.write(f"{factor}")
                if i < len(table_data) - 1:
                    f.write(", ")
                # Adjust line breaks for potentially smaller number of elements per line
                if (
                    (i + 1) % 8 == 0
                    and i < len(table_data) - 1
                    and INTERPOLATED_TABLE_SIZE > 8
                ):
                    f.write("\n        ")
            f.write("\n    }")
            if semitone_idx < len(all_tables_data_interpolated) - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n\n")
        f.write("#endif // PITCH_BEND_TABLE_INTERPOLATED_H\n")

    print(
        f"Interpolated pitch bend C header file generated successfully: {output_filename}"
    )

except IOError as e:
    print(f"Error writing to file {output_filename}: {e}")
