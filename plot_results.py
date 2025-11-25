import matplotlib.pyplot as plt
import os

# ============================
# CREA CARTELLA DI OUTPUT
# ============================

output_dir = "grafici_performance"
os.makedirs(output_dir, exist_ok=True)

# ============================
# DATI RACCOLTI
# ============================

threads = [1, 2, 4, 8, 16, 32]

wall_times = [
    32.767,
    22.949,
    17.460,
    16.750,
    17.666,
    18.384
]

cpu_times = [
    32.613,
    36.280,
    40.805,
    55.908,
    60.381,
    64.749
]

baseline = 32.773
speedup_calc = [baseline / t for t in wall_times]
efficiency = [s / t for s, t in zip(speedup_calc, threads)]
speedup_ideal = threads

# ============================
# 1) SPEEDUP vs THREAD
# ============================

plt.figure(figsize=(8,5))
plt.plot(threads, speedup_calc, marker='o')
plt.xlabel("Numero di thread")
plt.ylabel("Speedup (x)")
plt.title("Speedup vs Numero di Thread")
plt.grid(True)
plt.xticks(threads)
plt.savefig(f"{output_dir}/speedup_vs_thread.png", dpi=200)
plt.close()

# ============================
# 2) EFFICIENZA DEL PARALLELISMO
# ============================

plt.figure(figsize=(8,5))
plt.plot(threads, efficiency, marker='o')
plt.xlabel("Numero di thread")
plt.ylabel("Efficienza")
plt.title("Efficienza vs Numero di Thread")
plt.grid(True)
plt.ylim(0,1)
plt.xticks(threads)
plt.savefig(f"{output_dir}/efficienza_vs_thread.png", dpi=200)
plt.close()

# ============================
# 3) WALL-CLOCK TIME
# ============================

plt.figure(figsize=(8,5))
plt.plot(threads, wall_times, marker='o')
plt.xlabel("Numero di thread")
plt.ylabel("Tempo (s)")
plt.title("Wall-Clock Time vs Numero di Thread")
plt.grid(True)
plt.xticks(threads)
plt.savefig(f"{output_dir}/wall_clock_time_vs_thread.png", dpi=200)
plt.close()

# ============================
# 4) CPU TIME
# ============================

plt.figure(figsize=(8,5))
plt.plot(threads, cpu_times, marker='o')
plt.xlabel("Numero di thread")
plt.ylabel("CPU Time (s)")
plt.title("CPU Time vs Numero di Thread")
plt.grid(True)
plt.xticks(threads)
plt.savefig(f"{output_dir}/cpu_time_vs_thread.png", dpi=200)
plt.close()

# ============================
# 5) SPEEDUP IDEALE vs REALE
# ============================

plt.figure(figsize=(8,5))
plt.plot(threads, speedup_calc, marker='o', label="Speedup reale")
plt.plot(threads, speedup_ideal, marker='x', linestyle='--', label="Speedup ideale (lineare)")
plt.xlabel("Numero di thread")
plt.ylabel("Speedup (x)")
plt.title("Speedup: reale vs ideale")
plt.grid(True)
plt.legend()
plt.xticks(threads)
plt.savefig(f"{output_dir}/speedup_reale_vs_ideale.png", dpi=200)
plt.close()

print(f"Tutti i grafici salvati in: {output_dir}/")
