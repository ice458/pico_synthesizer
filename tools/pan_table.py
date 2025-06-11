import math

# Constants
PAN_TABLE_SIZE = 128
FP_MAX = 32767

# Generate pan table using a natural curve (sine-based)
pan_table = []
for pan_value in range(PAN_TABLE_SIZE):
    angle = (pan_value / (PAN_TABLE_SIZE - 1)) * (math.pi / 2)  # Map pan_value to 0 to π/2
    left_gain = int(FP_MAX * math.cos(angle))
    right_gain = int(FP_MAX * math.sin(angle))
    pan_table.append((left_gain, right_gain))

# Write pan_table.h
with open("pan_table.h", "w") as header_file:
    header_file.write("#ifndef PAN_TABLE_H\n")
    header_file.write("#define PAN_TABLE_H\n\n")
    header_file.write("#include \"fp.h\"\n\n")
    header_file.write(f"#define PAN_TABLE_SIZE {PAN_TABLE_SIZE}\n\n")
    header_file.write("extern const fp_t pan_table[PAN_TABLE_SIZE][2];\n\n")
    header_file.write("#endif // PAN_TABLE_H\n")

# Write pan_table.c
with open("pan_table.c", "w") as source_file:
    source_file.write("#include \"pan_table.h\"\n")
    source_file.write("#include \"fp.h\"\n\n")
    source_file.write(f"const fp_t pan_table[PAN_TABLE_SIZE][2] = {{\n")
    for left, right in pan_table:
        source_file.write(f"    {{{left}, {right}}},\n")
    source_file.write("};\n")