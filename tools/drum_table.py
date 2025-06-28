from pydub import AudioSegment
from pydub.silence import split_on_silence
import os
import struct

TARGET_SAMPLE_RATE = 40000  # Synthesizer sampling rate FS (40e3f)
TARGET_CHANNELS = 1  # Monaural
TARGET_SAMPLE_WIDTH = 2  # 16-bit (because fp_t is int16_t)
ADPCM_OUTPUT_BYTES_PER_LINE = 12  # Number of bytes per line in the generated C code

# ADPCM step size table (same as in C code)
ADPCM_STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
]

# ADPCM index adjustment table
ADPCM_INDEX_TABLE = [
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
]


class ADPCMEncoder:
    def __init__(self):
        self.predictor = 0
        self.step_index = 0
    
    def encode_sample(self, sample):
        """Encode a single 16-bit PCM sample to 4-bit ADPCM"""
        difference = sample - self.predictor
        sign = 0
        if difference < 0:
            sign = 8
            difference = -difference
        
        step = ADPCM_STEP_TABLE[self.step_index]
        delta = 0
        
        if difference >= step:
            delta |= 4
            difference -= step
        if difference >= step >> 1:
            delta |= 2
            difference -= step >> 1
        if difference >= step >> 2:
            delta |= 1
        
        # Apply the sign
        adpcm_sample = delta | sign
        
        # Update predictor
        diff = 0
        if delta & 4:
            diff += step
        if delta & 2:
            diff += step >> 1
        if delta & 1:
            diff += step >> 2
        diff += step >> 3
        
        if sign:
            self.predictor -= diff
        else:
            self.predictor += diff
        
        # Clamp predictor
        if self.predictor > 32767:
            self.predictor = 32767
        elif self.predictor < -32768:
            self.predictor = -32768
        
        # Update step index
        self.step_index += ADPCM_INDEX_TABLE[adpcm_sample]
        if self.step_index < 0:
            self.step_index = 0
        elif self.step_index > 88:
            self.step_index = 88
        
        return adpcm_sample
    
    def encode_samples(self, samples):
        """Encode an array of 16-bit PCM samples to ADPCM bytes"""
        adpcm_bytes = []
        for i in range(0, len(samples), 2):
            # Process two samples at a time (pack into one byte)
            sample1 = samples[i]
            sample2 = samples[i + 1] if i + 1 < len(samples) else 0
            
            nibble1 = self.encode_sample(sample1)
            nibble2 = self.encode_sample(sample2)
            
            # Pack two 4-bit nibbles into one byte (high nibble first)
            adpcm_byte = (nibble1 << 4) | nibble2
            adpcm_bytes.append(adpcm_byte)
        
        return bytes(adpcm_bytes)


def split_wav_on_silence(
    input_wav_path,
    output_dir,
    silence_thresh=-40,
    min_silence_len=500,
    keep_silence=100,
):
    """
    Splits a WAV file at silent parts and saves them to the specified directory.
    Returns a list of paths to the split chunk files.
    """
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    audio = AudioSegment.from_wav(input_wav_path)
    chunks = split_on_silence(
        audio,
        min_silence_len=min_silence_len,
        silence_thresh=silence_thresh,
        keep_silence=keep_silence,
    )

    chunk_paths = []
    for i, chunk in enumerate(chunks):
        output_filename = os.path.join(output_dir, f"chunk_{i}.wav")
        chunk.export(output_filename, format="wav")
        print(f"Exported {output_filename}")
        chunk_paths.append(output_filename)
    return chunk_paths


def generate_adpcm_c_files(chunk_files, project_c_h_output_dir, adpcm_start_note=35):
    """
    Generates adpcm_table.h and adpcm_table.c from the specified WAV chunk files.
    """
    h_file_path = os.path.join(project_c_h_output_dir, "adpcm_table.h")
    c_file_path = os.path.join(project_c_h_output_dir, "adpcm_table.c")

    adpcm_sample_c_definitions = []  # List of ADPCM data definitions
    adpcm_samples_array_entries = []  # List of array entries

    adpcm_note_count = len(chunk_files)

    if adpcm_note_count == 0:
        print("No split audio chunks found. Generating an empty ADPCM table.")

    for i, chunk_path in enumerate(chunk_files):
        current_note = adpcm_start_note + i
        array_name = f"adpcm_sample_data_note{current_note}"

        try:
            chunk_audio = AudioSegment.from_wav(chunk_path)
            chunk_audio = chunk_audio.set_channels(TARGET_CHANNELS)
            chunk_audio = chunk_audio.set_frame_rate(TARGET_SAMPLE_RATE)
            chunk_audio = chunk_audio.set_sample_width(TARGET_SAMPLE_WIDTH)

            samples = chunk_audio.get_array_of_samples()
            original_sample_count = len(samples)

            if original_sample_count > 0:
                # Encode to ADPCM
                encoder = ADPCMEncoder()
                adpcm_data = encoder.encode_samples(samples)
                adpcm_byte_count = len(adpcm_data)

                # Format ADPCM bytes for C code
                formatted_bytes_lines = []
                for j in range(0, adpcm_byte_count, ADPCM_OUTPUT_BYTES_PER_LINE):
                    line_bytes = adpcm_data[j : j + ADPCM_OUTPUT_BYTES_PER_LINE]
                    formatted_bytes_lines.append(
                        "    " + ", ".join(f"0x{byte:02X}" for byte in line_bytes)
                    )
                c_array_content = ",\n".join(formatted_bytes_lines) + "\n"

                adpcm_sample_c_definitions.append(
                    f"static const uint8_t {array_name}[{adpcm_byte_count}] = {{\n{c_array_content}}};"
                )
                adpcm_samples_array_entries.append(
                    f"    {{ .data = {array_name}, .length = {adpcm_byte_count}, .sample_count = {original_sample_count} }}"
                )
            else:
                # Empty sample
                adpcm_sample_c_definitions.append(
                    f"static const uint8_t {array_name}[1] = {{0}}; // Empty sample"
                )
                adpcm_samples_array_entries.append(
                    f"    {{ .data = {array_name}, .length = 1, .sample_count = 0 }}"
                )

        except Exception as e:
            print(f"Error: An error occurred while processing chunk {chunk_path}: {e}")
            # Register as an empty sample in case of an error
            adpcm_sample_c_definitions.append(
                f"static const uint8_t {array_name}[1] = {{0}}; // Error placeholder"
            )
            adpcm_samples_array_entries.append(
                f"    {{ .data = {array_name}, .length = 1, .sample_count = 0 }} // Error placeholder"
            )

    # Generate adpcm_table.h
    adpcm_end_note = (
        adpcm_start_note + adpcm_note_count - 1
        if adpcm_note_count > 0
        else adpcm_start_note - 1
    )

    with open(h_file_path, "w") as h_file:
        h_file.write("#ifndef ADPCM_TABLE_H\n")
        h_file.write("#define ADPCM_TABLE_H\n\n")
        h_file.write('#include "fp.h" // For fp_t\n')
        h_file.write("#include <stdint.h>\n\n")
        h_file.write("#define ADPCM_ZERO_THRESHOLD 5\n")
        h_file.write(f"#define ADPCM_START_NOTE {adpcm_start_note}\n")
        h_file.write(f"#define ADPCM_END_NOTE {adpcm_end_note}\n")
        h_file.write(f"#define ADPCM_NOTE_COUNT {adpcm_note_count}\n\n")
        h_file.write("// ADPCM decoder state\n")
        h_file.write("typedef struct {\n")
        h_file.write("    int16_t predictor;\n")
        h_file.write("    int8_t step_index;\n")
        h_file.write("} adpcm_state_t;\n\n")
        h_file.write("typedef struct {\n")
        h_file.write("    const uint8_t* data;    // Pointer to the ADPCM compressed data\n")
        h_file.write("    uint32_t length;        // Length of the ADPCM data in bytes\n")
        h_file.write("    uint32_t sample_count;  // Number of PCM samples when decompressed\n")
        h_file.write("} adpcm_sample_t;\n\n")
        h_file.write("// ADPCM step size table\n")
        h_file.write("extern const int16_t adpcm_step_table[89];\n\n")
        h_file.write("// ADPCM index adjustment table\n")
        h_file.write("extern const int8_t adpcm_index_table[16];\n\n")
        if adpcm_note_count > 0:
            h_file.write("extern const adpcm_sample_t adpcm_samples[ADPCM_NOTE_COUNT];\n")
        else:
            h_file.write("// No ADPCM samples defined (ADPCM_NOTE_COUNT is 0)\n")
        h_file.write("\n// ADPCM decoder function\n")
        h_file.write("fp_t adpcm_decode_sample(uint8_t adpcm_sample, adpcm_state_t* state);\n")
        h_file.write("\n#endif // ADPCM_TABLE_H\n")
    print(f"Generated {h_file_path}")

    # Generate adpcm_table.c
    with open(c_file_path, "w") as c_file:
        c_file.write('#include "adpcm_table.h"\n\n')
        
        # Write ADPCM tables
        c_file.write("// ADPCM step size table (89 entries)\n")
        c_file.write("const int16_t adpcm_step_table[89] = {\n")
        for i in range(0, len(ADPCM_STEP_TABLE), 10):
            line_values = ADPCM_STEP_TABLE[i:i+10]
            c_file.write("    " + ", ".join(map(str, line_values)) + ",\n")
        c_file.write("};\n\n")
        
        c_file.write("// ADPCM index adjustment table (16 entries)\n")
        c_file.write("const int8_t adpcm_index_table[16] = {\n")
        c_file.write("    " + ", ".join(map(str, ADPCM_INDEX_TABLE)) + "\n")
        c_file.write("};\n\n")
        
        # Write ADPCM decoder function
        c_file.write("// ADPCM decoder function\n")
        c_file.write("fp_t adpcm_decode_sample(uint8_t adpcm_sample, adpcm_state_t* state) {\n")
        c_file.write("    int16_t step = adpcm_step_table[state->step_index];\n")
        c_file.write("    int16_t difference = 0;\n")
        c_file.write("    \n")
        c_file.write("    // Calculate difference\n")
        c_file.write("    if (adpcm_sample & 4) difference += step;\n")
        c_file.write("    if (adpcm_sample & 2) difference += step >> 1;\n")
        c_file.write("    if (adpcm_sample & 1) difference += step >> 2;\n")
        c_file.write("    difference += step >> 3;\n")
        c_file.write("    \n")
        c_file.write("    // Apply sign\n")
        c_file.write("    if (adpcm_sample & 8) {\n")
        c_file.write("        state->predictor -= difference;\n")
        c_file.write("    } else {\n")
        c_file.write("        state->predictor += difference;\n")
        c_file.write("    }\n")
        c_file.write("    \n")
        c_file.write("    // Clamp predictor\n")
        c_file.write("    if (state->predictor > 32767) state->predictor = 32767;\n")
        c_file.write("    else if (state->predictor < -32768) state->predictor = -32768;\n")
        c_file.write("    \n")
        c_file.write("    // Update step index\n")
        c_file.write("    state->step_index += adpcm_index_table[adpcm_sample];\n")
        c_file.write("    if (state->step_index < 0) state->step_index = 0;\n")
        c_file.write("    else if (state->step_index > 88) state->step_index = 88;\n")
        c_file.write("    \n")
        c_file.write("    return (fp_t)state->predictor;\n")
        c_file.write("}\n\n")
        
        if adpcm_note_count > 0:
            for definition in adpcm_sample_c_definitions:
                c_file.write(f"{definition}\n\n")

            c_file.write("const adpcm_sample_t adpcm_samples[ADPCM_NOTE_COUNT] = {\n")
            c_file.write(",\n".join(adpcm_samples_array_entries))
            c_file.write("\n};\n")
        else:
            c_file.write("// No ADPCM data as no chunks were generated.\n")
            c_file.write("const adpcm_sample_t adpcm_samples[0] = {}; // Definition for empty array\n")
    print(f"Generated {c_file_path}")


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    input_file = os.path.join(script_dir, "drum", "drum.wav")

    # Output destination for split chunks (temporary folder)
    chunk_output_directory = os.path.join(script_dir, "drum", "output_chunks")

    # Output destination for generated .c/.h files (project root = same directory as script)
    project_c_h_output_dir = script_dir

    silence_threshold = -60
    minimum_silence_length = 150
    keep_silence_at_ends = 50
    adpcm_start_midi_note = 35  # Starting MIDI note number for drum samples

    if os.path.exists(input_file):
        print(f"Processing input file {input_file}...")
        chunk_file_paths = split_wav_on_silence(
            input_file,
            chunk_output_directory,
            silence_thresh=silence_threshold,
            min_silence_len=minimum_silence_length,
            keep_silence=keep_silence_at_ends,
        )

        if chunk_file_paths:
            print(
                f"{len(chunk_file_paths)} chunks were generated in {chunk_output_directory}."
            )
            generate_adpcm_c_files(
                chunk_file_paths,
                project_c_h_output_dir,
                adpcm_start_note=adpcm_start_midi_note,
            )
            print(f"ADPCM C files were generated in {project_c_h_output_dir}.")
        else:
            print("No chunks were generated from the input WAV file.")
            # Even if there are no chunks, generate empty ADPCM table files
            generate_adpcm_c_files(
                [], project_c_h_output_dir, adpcm_start_note=adpcm_start_midi_note
            )

    else:
        print(f"Error: Input file not found - {input_file}")
        print(
            f"Please ensure that the {os.path.join('drum', 'drum.wav')} file exists in the 'drum' folder within the same directory as the script."
        )
