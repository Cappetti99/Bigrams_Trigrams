#!/usr/bin/env python3
"""
Script per analizzare i risultati delle performance dal folder results.
Legge automaticamente i file performance_stats.txt e genera grafici comparativi.
"""

import os
import re
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# ============================
# CONFIGURAZIONE
# ============================

RESULTS_DIR = "results"
OUTPUT_DIR = "grafici_analisi"

# ============================
# FUNZIONI DI PARSING
# ============================

def parse_performance_file(filepath):
    """
    Estrae i dati dal file performance_stats.txt
    Ritorna un dizionario con tutti i valori estratti
    """
    data = {
        'threads': None,
        'wall_mean': None,
        'wall_min': None,
        'wall_max': None,
        'wall_std': None,
        'wall_cv': None,
        'cpu_mean': None,
        'cpu_min': None,
        'cpu_max': None,
        'cpu_std': None,
        'cpu_cv': None,
        'word_bigrams': None,
        'word_trigrams': None,
        'char_bigrams': None,
        'char_trigrams': None,
    }
    
    try:
        with open(filepath, 'r') as f:
            content = f.read()
        
        # Estrai numero di thread
        thread_match = re.search(r'THREADS USED:\s*(\d+)', content)
        if thread_match:
            data['threads'] = int(thread_match.group(1))
        
        # Estrai Wall-Clock Time
        wall_mean = re.search(r'WALL-CLOCK TIME.*?Mean:\s*([\d.]+)\s*s', content, re.DOTALL)
        wall_min = re.search(r'WALL-CLOCK TIME.*?Minimum:\s*([\d.]+)\s*s', content, re.DOTALL)
        wall_max = re.search(r'WALL-CLOCK TIME.*?Maximum:\s*([\d.]+)\s*s', content, re.DOTALL)
        wall_std = re.search(r'WALL-CLOCK TIME.*?Std Deviation:\s*([\d.]+)\s*s', content, re.DOTALL)
        wall_cv = re.search(r'WALL-CLOCK TIME.*?Coeff\. Variation:\s*([\d.]+)\s*%', content, re.DOTALL)
        
        if wall_mean: data['wall_mean'] = float(wall_mean.group(1))
        if wall_min: data['wall_min'] = float(wall_min.group(1))
        if wall_max: data['wall_max'] = float(wall_max.group(1))
        if wall_std: data['wall_std'] = float(wall_std.group(1))
        if wall_cv: data['wall_cv'] = float(wall_cv.group(1))
        
        # Estrai CPU Time
        cpu_mean = re.search(r'CPU TIME.*?Mean:\s*([\d.]+)\s*s', content, re.DOTALL)
        cpu_min = re.search(r'CPU TIME.*?Minimum:\s*([\d.]+)\s*s', content, re.DOTALL)
        cpu_max = re.search(r'CPU TIME.*?Maximum:\s*([\d.]+)\s*s', content, re.DOTALL)
        cpu_std = re.search(r'CPU TIME.*?Std Deviation:\s*([\d.]+)\s*s', content, re.DOTALL)
        cpu_cv = re.search(r'CPU TIME.*?Coeff\. Variation:\s*([\d.]+)\s*%', content, re.DOTALL)
        
        if cpu_mean: data['cpu_mean'] = float(cpu_mean.group(1))
        if cpu_min: data['cpu_min'] = float(cpu_min.group(1))
        if cpu_max: data['cpu_max'] = float(cpu_max.group(1))
        if cpu_std: data['cpu_std'] = float(cpu_std.group(1))
        if cpu_cv: data['cpu_cv'] = float(cpu_cv.group(1))
        
        # Estrai N-gram statistics
        word_bi = re.search(r'Word Bigrams:\s*([\d]+)', content)
        word_tri = re.search(r'Word Trigrams:\s*([\d]+)', content)
        char_bi = re.search(r'Char Bigrams:\s*([\d]+)', content)
        char_tri = re.search(r'Char Trigrams:\s*([\d]+)', content)
        
        if word_bi: data['word_bigrams'] = int(word_bi.group(1))
        if word_tri: data['word_trigrams'] = int(word_tri.group(1))
        if char_bi: data['char_bigrams'] = int(char_bi.group(1))
        if char_tri: data['char_trigrams'] = int(char_tri.group(1))
        
    except Exception as e:
        print(f"Errore nel parsing di {filepath}: {e}")
    
    return data


def load_all_results(results_dir):
    """
    Carica tutti i risultati dalla cartella results.
    Ritorna due dizionari: sequential_data e parallel_data
    """
    sequential_data = None
    parallel_data = {}
    
    results_path = Path(results_dir)
    
    # Carica dati sequenziali
    seq_file = results_path / "sequential" / "performance_stats.txt"
    if seq_file.exists():
        sequential_data = parse_performance_file(seq_file)
        print(f"✓ Caricati dati sequenziali: {sequential_data['wall_mean']:.3f}s")
    
    # Carica dati paralleli
    parallel_dir = results_path / "parallel"
    if parallel_dir.exists():
        for thread_dir in parallel_dir.iterdir():
            if thread_dir.is_dir() and thread_dir.name.startswith("threads_"):
                stats_file = thread_dir / "performance_stats.txt"
                if stats_file.exists():
                    data = parse_performance_file(stats_file)
                    if data['threads'] is not None:
                        parallel_data[data['threads']] = data
                        print(f"✓ Caricati dati per {data['threads']} thread: {data['wall_mean']:.3f}s")
    
    return sequential_data, parallel_data


# ============================
# FUNZIONI DI ANALISI
# ============================

def calculate_metrics(sequential_data, parallel_data):
    """
    Calcola speedup, efficienza e altre metriche
    """
    threads = sorted(parallel_data.keys())
    
    # Usa il tempo sequenziale come baseline
    if sequential_data:
        baseline = sequential_data['wall_mean']
    else:
        # Se non c'è sequential, usa parallel con 1 thread
        baseline = parallel_data.get(1, {}).get('wall_mean', 1)
    
    metrics = {
        'threads': threads,
        'wall_times': [parallel_data[t]['wall_mean'] for t in threads],
        'wall_std': [parallel_data[t].get('wall_std', 0) for t in threads],
        'cpu_times': [parallel_data[t]['cpu_mean'] for t in threads],
        'cpu_std': [parallel_data[t].get('cpu_std', 0) for t in threads],
        'speedup': [baseline / parallel_data[t]['wall_mean'] for t in threads],
        'efficiency': [(baseline / parallel_data[t]['wall_mean']) / t for t in threads],
        'baseline': baseline
    }
    
    return metrics


# ============================
# FUNZIONI DI PLOTTING
# ============================

def plot_wall_time(metrics, output_dir, sequential_data=None):
    """Grafico Wall-Clock Time vs Thread"""
    plt.figure(figsize=(10, 6))
    
    plt.errorbar(metrics['threads'], metrics['wall_times'], 
                 yerr=metrics['wall_std'], marker='o', capsize=5, 
                 linewidth=2, markersize=8, label='Parallelo')
    
    if sequential_data:
        plt.axhline(y=sequential_data['wall_mean'], color='r', 
                   linestyle='--', linewidth=2, label=f'Sequenziale ({sequential_data["wall_mean"]:.2f}s)')
    
    plt.xlabel("Numero di Thread", fontsize=12)
    plt.ylabel("Wall-Clock Time (s)", fontsize=12)
    plt.title("Wall-Clock Time vs Numero di Thread", fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.legend(fontsize=10)
    plt.xticks(metrics['threads'])
    plt.tight_layout()
    plt.savefig(f"{output_dir}/wall_time_vs_threads.png", dpi=200)
    plt.close()
    print(f"  → Salvato: wall_time_vs_threads.png")


def plot_cpu_time(metrics, output_dir):
    """Grafico CPU Time vs Thread"""
    plt.figure(figsize=(10, 6))
    
    plt.errorbar(metrics['threads'], metrics['cpu_times'], 
                 yerr=metrics['cpu_std'], marker='s', capsize=5,
                 linewidth=2, markersize=8, color='green')
    
    plt.xlabel("Numero di Thread", fontsize=12)
    plt.ylabel("CPU Time (s)", fontsize=12)
    plt.title("CPU Time vs Numero di Thread", fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.xticks(metrics['threads'])
    plt.tight_layout()
    plt.savefig(f"{output_dir}/cpu_time_vs_threads.png", dpi=200)
    plt.close()
    print(f"  → Salvato: cpu_time_vs_threads.png")


def plot_speedup(metrics, output_dir):
    """Grafico Speedup vs Thread"""
    plt.figure(figsize=(10, 6))
    
    threads = metrics['threads']
    speedup_ideal = threads
    
    plt.plot(threads, metrics['speedup'], marker='o', linewidth=2, 
             markersize=8, label='Speedup reale', color='blue')
    plt.plot(threads, speedup_ideal, linestyle='--', linewidth=2,
             label='Speedup ideale (lineare)', color='gray', alpha=0.7)
    
    plt.xlabel("Numero di Thread", fontsize=12)
    plt.ylabel("Speedup", fontsize=12)
    plt.title("Speedup vs Numero di Thread", fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.legend(fontsize=10)
    plt.xticks(threads)
    plt.tight_layout()
    plt.savefig(f"{output_dir}/speedup_vs_threads.png", dpi=200)
    plt.close()
    print(f"  → Salvato: speedup_vs_threads.png")


def plot_efficiency(metrics, output_dir):
    """Grafico Efficienza vs Thread"""
    plt.figure(figsize=(10, 6))
    
    plt.plot(metrics['threads'], metrics['efficiency'], marker='D', 
             linewidth=2, markersize=8, color='purple')
    plt.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5, label='Efficienza ideale (100%)')
    
    plt.xlabel("Numero di Thread", fontsize=12)
    plt.ylabel("Efficienza", fontsize=12)
    plt.title("Efficienza del Parallelismo vs Numero di Thread", fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.ylim(0, max(1.1, max(metrics['efficiency']) * 1.1))
    plt.legend(fontsize=10)
    plt.xticks(metrics['threads'])
    plt.tight_layout()
    plt.savefig(f"{output_dir}/efficiency_vs_threads.png", dpi=200)
    plt.close()
    print(f"  → Salvato: efficiency_vs_threads.png")


def plot_combined(metrics, output_dir, sequential_data=None):
    """Grafico combinato con tutti i dati"""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    
    threads = metrics['threads']
    
    # Wall-Clock Time
    ax1 = axes[0, 0]
    ax1.errorbar(threads, metrics['wall_times'], yerr=metrics['wall_std'], 
                 marker='o', capsize=5, linewidth=2, markersize=6)
    if sequential_data:
        ax1.axhline(y=sequential_data['wall_mean'], color='r', linestyle='--', alpha=0.7)
    ax1.set_xlabel("Thread")
    ax1.set_ylabel("Wall-Clock Time (s)")
    ax1.set_title("Wall-Clock Time")
    ax1.grid(True, alpha=0.3)
    ax1.set_xticks(threads)
    
    # CPU Time
    ax2 = axes[0, 1]
    ax2.errorbar(threads, metrics['cpu_times'], yerr=metrics['cpu_std'],
                 marker='s', capsize=5, linewidth=2, markersize=6, color='green')
    ax2.set_xlabel("Thread")
    ax2.set_ylabel("CPU Time (s)")
    ax2.set_title("CPU Time")
    ax2.grid(True, alpha=0.3)
    ax2.set_xticks(threads)
    
    # Speedup
    ax3 = axes[1, 0]
    ax3.plot(threads, metrics['speedup'], marker='o', linewidth=2, markersize=6, label='Reale')
    ax3.plot(threads, threads, linestyle='--', color='gray', alpha=0.7, label='Ideale')
    ax3.set_xlabel("Thread")
    ax3.set_ylabel("Speedup")
    ax3.set_title("Speedup")
    ax3.grid(True, alpha=0.3)
    ax3.legend(fontsize=8)
    ax3.set_xticks(threads)
    
    # Efficienza
    ax4 = axes[1, 1]
    ax4.plot(threads, metrics['efficiency'], marker='D', linewidth=2, markersize=6, color='purple')
    ax4.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5)
    ax4.set_xlabel("Thread")
    ax4.set_ylabel("Efficienza")
    ax4.set_title("Efficienza")
    ax4.grid(True, alpha=0.3)
    ax4.set_ylim(0, max(1.1, max(metrics['efficiency']) * 1.1))
    ax4.set_xticks(threads)
    
    plt.suptitle("Analisi Performance Parallele - Bigrams/Trigrams", fontsize=16, fontweight='bold')
    plt.tight_layout()
    plt.savefig(f"{output_dir}/analisi_combinata.png", dpi=200)
    plt.close()
    print(f"  → Salvato: analisi_combinata.png")


def plot_scalability(metrics, output_dir):
    """Grafico di scalabilità forte e debole"""
    plt.figure(figsize=(10, 6))
    
    threads = np.array(metrics['threads'])
    speedup = np.array(metrics['speedup'])
    
    # Calcola scalabilità (percentuale rispetto a ideale)
    scalability = (speedup / threads) * 100
    
    plt.bar(range(len(threads)), scalability, tick_label=[str(t) for t in threads],
            color='steelblue', edgecolor='black', alpha=0.8)
    plt.axhline(y=100, color='red', linestyle='--', linewidth=2, label='Scalabilità ideale (100%)')
    
    plt.xlabel("Numero di Thread", fontsize=12)
    plt.ylabel("Scalabilità (%)", fontsize=12)
    plt.title("Scalabilità del Sistema", fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3, axis='y')
    plt.legend(fontsize=10)
    plt.ylim(0, 110)
    plt.tight_layout()
    plt.savefig(f"{output_dir}/scalability.png", dpi=200)
    plt.close()
    print(f"  → Salvato: scalability.png")


def generate_summary_report(sequential_data, parallel_data, metrics, output_dir):
    """Genera un report testuale con il riepilogo delle analisi"""
    
    report = []
    report.append("=" * 70)
    report.append("        REPORT ANALISI PERFORMANCE - BIGRAMS/TRIGRAMS")
    report.append("=" * 70)
    report.append("")
    
    # Info sequenziale
    if sequential_data:
        report.append("VERSIONE SEQUENZIALE")
        report.append("-" * 40)
        report.append(f"  Wall-Clock Time: {sequential_data['wall_mean']:.3f} s (±{sequential_data.get('wall_std', 0):.3f})")
        report.append(f"  CPU Time:        {sequential_data['cpu_mean']:.3f} s (±{sequential_data.get('cpu_std', 0):.3f})")
        report.append("")
    
    # Info parallela
    report.append("VERSIONE PARALLELA")
    report.append("-" * 40)
    report.append(f"{'Thread':>8} | {'Wall Time (s)':>14} | {'Speedup':>10} | {'Efficienza':>10}")
    report.append("-" * 50)
    
    for i, t in enumerate(metrics['threads']):
        wall = metrics['wall_times'][i]
        speedup = metrics['speedup'][i]
        eff = metrics['efficiency'][i]
        report.append(f"{t:>8} | {wall:>14.3f} | {speedup:>10.2f}x | {eff:>10.2%}")
    
    report.append("")
    
    # Migliori risultati
    best_idx = np.argmin(metrics['wall_times'])
    report.append("RISULTATI MIGLIORI")
    report.append("-" * 40)
    report.append(f"  Thread ottimale:    {metrics['threads'][best_idx]}")
    report.append(f"  Tempo minimo:       {metrics['wall_times'][best_idx]:.3f} s")
    report.append(f"  Speedup massimo:    {max(metrics['speedup']):.2f}x")
    report.append(f"  Efficienza max:     {max(metrics['efficiency']):.2%}")
    report.append("")
    
    # N-gram statistics
    sample_data = list(parallel_data.values())[0]
    if sample_data.get('word_bigrams'):
        report.append("STATISTICHE N-GRAM")
        report.append("-" * 40)
        report.append(f"  Word Bigrams:  {sample_data['word_bigrams']:,}")
        report.append(f"  Word Trigrams: {sample_data['word_trigrams']:,}")
        report.append(f"  Char Bigrams:  {sample_data['char_bigrams']:,}")
        report.append(f"  Char Trigrams: {sample_data['char_trigrams']:,}")
    
    report.append("")
    report.append("=" * 70)
    
    # Salva report
    report_text = "\n".join(report)
    report_path = f"{output_dir}/summary_report.txt"
    with open(report_path, 'w') as f:
        f.write(report_text)
    
    print(f"\n{report_text}")
    print(f"\n  → Report salvato: {report_path}")


# ============================
# MAIN
# ============================

def main():
    print("=" * 60)
    print("  ANALIZZATORE RISULTATI - Bigrams/Trigrams")
    print("=" * 60)
    print()
    
    # Crea cartella output
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    # Carica dati
    print("📂 Caricamento dati...")
    sequential_data, parallel_data = load_all_results(RESULTS_DIR)
    
    if not parallel_data:
        print("❌ Nessun dato parallelo trovato nella cartella results/")
        return
    
    print(f"\n✓ Trovati risultati per {len(parallel_data)} configurazioni di thread")
    
    # Calcola metriche
    print("\n📊 Calcolo metriche...")
    metrics = calculate_metrics(sequential_data, parallel_data)
    
    # Genera grafici
    print("\n📈 Generazione grafici...")
    plot_wall_time(metrics, OUTPUT_DIR, sequential_data)
    plot_cpu_time(metrics, OUTPUT_DIR)
    plot_speedup(metrics, OUTPUT_DIR)
    plot_efficiency(metrics, OUTPUT_DIR)
    plot_scalability(metrics, OUTPUT_DIR)
    plot_combined(metrics, OUTPUT_DIR, sequential_data)
    
    # Genera report
    print("\n📝 Generazione report...")
    generate_summary_report(sequential_data, parallel_data, metrics, OUTPUT_DIR)
    
    print(f"\n✅ Analisi completata! Grafici salvati in: {OUTPUT_DIR}/")


if __name__ == "__main__":
    main()
