from pydub import AudioSegment
from pydub.silence import split_on_silence
import os

TARGET_SAMPLE_RATE = 40000  # Synthesizer sampling rate FS (40e3f)
TARGET_CHANNELS = 1  # Monaural
TARGET_SAMPLE_WIDTH = 2  # 16-bit (because fp_t is int16_t)
PCM_OUTPUT_SAMPLES_PER_LINE = 12  # Number of samples per line in the generated C code


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


def generate_pcm_c_files(chunk_files, project_c_h_output_dir, pcm_start_note=35):
    """
    Generates pcm_table.h and pcm_data.c from the specified WAV chunk files.
    """
    h_file_path = os.path.join(project_c_h_output_dir, "pcm_table.h")
    c_file_path = os.path.join(project_c_h_output_dir, "pcm_table.c")

    pcm_sample_c_definitions = (
        []
    )  # List of "static const fp_t pcm_sample_data_noteXX[] = { ... };"
    pcm_samples_array_entries = (
        []
    )  # List of "{ .data = pcm_sample_data_noteXX, .length = YY },"

    pcm_note_count = len(chunk_files)

    if pcm_note_count == 0:
        print("No split audio chunks found. Generating an empty PCM table.")

    for i, chunk_path in enumerate(chunk_files):
        current_note = pcm_start_note + i
        array_name = f"pcm_sample_data_note{current_note}"

        try:
            chunk_audio = AudioSegment.from_wav(chunk_path)
            chunk_audio = chunk_audio.set_channels(TARGET_CHANNELS)
            chunk_audio = chunk_audio.set_frame_rate(TARGET_SAMPLE_RATE)
            chunk_audio = chunk_audio.set_sample_width(TARGET_SAMPLE_WIDTH)

            samples = chunk_audio.get_array_of_samples()
            sample_length = len(samples)

            formatted_samples_lines = []
            if sample_length > 0:
                for j in range(0, sample_length, PCM_OUTPUT_SAMPLES_PER_LINE):
                    line_samples = samples[j : j + PCM_OUTPUT_SAMPLES_PER_LINE]
                    formatted_samples_lines.append(
                        "    " + ", ".join(map(str, line_samples))
                    )
                c_array_content = ",\n".join(formatted_samples_lines) + "\n"
            else:
                c_array_content = ""  # If the array is empty

            pcm_sample_c_definitions.append(
                f"static const fp_t {array_name}[{sample_length}] = {{\\n{c_array_content}}};"
            )
            pcm_samples_array_entries.append(
                f"    {{ .data = {array_name}, .length = {sample_length} }}"
            )

        except Exception as e:
            print(f"Error: An error occurred while processing chunk {chunk_path}: {e}")
            # Register as an empty sample in case of an error
            pcm_sample_c_definitions.append(
                f"static const fp_t {array_name}[1] = {{0}}; // Error placeholder"
            )
            pcm_samples_array_entries.append(
                f"    {{ .data = {array_name}, .length = 1 }} // Error placeholder"
            )

    # Generate pcm_table.h
    pcm_end_note = (
        pcm_start_note + pcm_note_count - 1
        if pcm_note_count > 0
        else pcm_start_note - 1
    )

    with open(h_file_path, "w") as h_file:
        h_file.write("#ifndef PCM_TABLE_H\n")
        h_file.write("#define PCM_TABLE_H\n\n")
        h_file.write('#include "fp.h" // For fp_t\n')
        h_file.write("#include <stdint.h>\n\n")
        h_file.write("#define PCM_ZERO_THRESHOLD 5\n")
        h_file.write(f"#define PCM_START_NOTE {pcm_start_note}\n")
        h_file.write(f"#define PCM_END_NOTE {pcm_end_note}\n")
        h_file.write(f"#define PCM_NOTE_COUNT {pcm_note_count}\n\n")
        h_file.write("typedef struct {\n")
        h_file.write("    const fp_t* data;   // Pointer to the sample data array\n")
        h_file.write("    uint32_t length;    // Length of the sample data array\n")
        h_file.write("} pcm_sample_t;\n\n")
        if pcm_note_count > 0:
            h_file.write("extern const pcm_sample_t pcm_samples[PCM_NOTE_COUNT];\n")
        else:
            h_file.write("// No PCM samples defined (PCM_NOTE_COUNT is 0)\\n")
        h_file.write("\\n#endif // PCM_TABLE_H\\n")
    print(f"Generated {h_file_path}")

    # Generate pcm_table.c
    with open(c_file_path, "w") as c_file:
        c_file.write('#include "pcm_table.h"\\n\\n')
        if pcm_note_count > 0:
            for definition in pcm_sample_c_definitions:
                c_file.write(f"{definition}\n\n")

            c_file.write("const pcm_sample_t pcm_samples[PCM_NOTE_COUNT] = {\n")
            c_file.write(",\n".join(pcm_samples_array_entries))
            c_file.write("\n};\n")
        else:
            c_file.write("// No PCM data as no chunks were generated.\\n")
            c_file.write(
                "const pcm_sample_t pcm_samples[0] = {}; // Definition for empty array\\n"
            )
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
    pcm_start_midi_note = 35  # Starting MIDI note number for drum samples

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
            generate_pcm_c_files(
                chunk_file_paths,
                project_c_h_output_dir,
                pcm_start_note=pcm_start_midi_note,
            )
            print(f"PCM C files were generated in {project_c_h_output_dir}.")
        else:
            print("No chunks were generated from the input WAV file.")
            # Even if there are no chunks, generate empty PCM table files
            generate_pcm_c_files(
                [], project_c_h_output_dir, pcm_start_note=pcm_start_midi_note
            )

    else:
        print(f"Error: Input file not found - {input_file}")
        print(
            f"Please ensure that the {os.path.join('drum', 'drum.wav')} file exists in the 'drum' folder within the same directory as the script."
        )
