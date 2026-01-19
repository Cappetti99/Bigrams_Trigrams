import matplotlib.pyplot as plt
import os

# =========================================================
# OBIETTIVO DELLO SCRIPT
# ---------------------------------------------------------
# Questo script prende dei tempi misurati (wall-time e cpu-time)
# ottenuti eseguendo lo stesso programma con diversi numeri di thread,
# calcola speedup ed efficienza, e poi salva una serie di grafici
# dentro una cartella dedicata.
# =========================================================


# =========================================================
# 1) CREA (SE SERVE) LA CARTELLA DI OUTPUT
# ---------------------------------------------------------
# Mettiamo i grafici in una cartella separata così non “sporchiamo”
# la directory di lavoro.
# exist_ok=True evita errori se la cartella esiste già.
# =========================================================
output_dir = "grafici_performance"
os.makedirs(output_dir, exist_ok=True)


# =========================================================
# 2) DATI RACCOLTI
# ---------------------------------------------------------
# threads: quanti thread sono stati usati in ogni prova.
# wall_times: tempo reale di esecuzione (quello che “vede” l’utente,
#             cioè il tempo trascorso da start a fine).
# cpu_times: tempo CPU totale consumato (spesso cresce con i thread,
#            perché somma il lavoro su più core + overhead).
# =========================================================
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


# =========================================================
# 3) CALCOLI: BASELINE, SPEEDUP, EFFICIENZA
# ---------------------------------------------------------
# baseline: tempo di riferimento per calcolare lo speedup.
# Di solito è il wall-time con 1 thread (o una media di più run).
# Qui è molto vicino al primo wall_time (32.767), quindi ok.
#
# speedup = T1 / Tp  (quanto miglioro rispetto alla versione 1-thread)
# efficiency = speedup / p  (quanto “bene” sto usando i thread)
#
# speedup_ideal: speedup perfetto lineare (p thread -> speedup p).
# Nella realtà NON succede quasi mai: overhead, sezioni seriali,
# contesa memoria, scheduling, ecc.
# =========================================================
baseline = 32.773

# Calcolo dello speedup reale: per ogni prova prendo baseline / wall_time
speedup_calc = [baseline /