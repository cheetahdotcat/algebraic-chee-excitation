import numpy as np

# Define parameters
N = 512  # Window length
alpha = 0.54
beta = 1 - alpha

# Generate full symmetric Hamming window
n = np.arange(N)
hamming_full = alpha - beta * np.cos(2 * np.pi * n / (N - 1))

# Create asymmetric window:
# First half: standard Hamming
# Second half: exponential decay from midpoint value
first_half = hamming_full[:N // 2]

# Gaussian-like exponential decay
mid_val = hamming_full[N // 2]
decay = mid_val * np.exp(-5 * (np.linspace(0, 1, N // 2))**2)

# Combine both halves
asymmetric_window = np.concatenate([first_half, decay])

# Scale to Q15 format (16-bit signed int, max 32767)
asymmetric_q15 = np.round(asymmetric_window * 32767).astype(int)

# Print in C array format
print("#define L_window 512\n")
print("static Word16 window[L_window] = {")
for i in range(0, N, 8):
    line = ", ".join(f"{val:5d}" for val in asymmetric_q15[i:i+8])
    print(f"  {line},")
print("};")