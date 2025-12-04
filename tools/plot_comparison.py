#!/usr/bin/env python3
"""
Script to compare performance of parallel vs parallel_soa.
Generates comparative charts for:
- Mean time vs number of threads
- Speedup vs number of threads
- Efficiency vs threads
- Coefficient of variation (CV%) vs threads
- Percentage comparison parallel_soa vs parallel
"""

import os
import re
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# ============================
# CONFIGURATION
# ============================

RESULTS_DIR = "results"
OUTPUT_DIR = "charts"

# Chart colors
COLOR_PARALLEL = '#1f77b4'      # Blue
COLOR_SOA = '#ff7f0e'           # Orange
COLOR_IDEAL = '#7f7f7f'         # Gray
COLOR_SEQ = '#d62728'           # Red

# Labels
LABEL_PARALLEL = 'parallel'
LABEL_SOA = 'parallel_soa'

# ============================
# PARSING FUNCTIONS
# ============================

def parse_csv_file(filepath):
    """Reads a performance_stats.csv file"""
    data = {}
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if ',' not in line or line.startswith('metric'):
                    continue
                key, value = line.split(',', 1)
                key = key.strip()
                value = value.strip()
                
                if key in ['threads', 'word_bigrams', 'word_trigrams', 'char_bigrams', 'char_trigrams']:
                    data[key] = int(value)
                elif key == 'data_layout':
                    data[key] = value
                else:
                    data[key] = float(value)
    except Exception as e:
        print(f"Error parsing {filepath}: {e}")
    return data


def parse_txt_file(filepath):
    """Parser for backward compatibility with old TXT files"""
    data = {}
    try:
        with open(filepath, 'r') as f:
            content = f.read()
        
        thread_match = re.search(r'THREADS USED:\s*(\d+)', content)
        if thread_match:
            data['threads'] = int(thread_match.group(1))
        
        patterns = [
            (r'WALL-CLOCK TIME.*?Mean:\s*([\d.]+)\s*s', 'wall_mean'),
            (r'WALL-CLOCK TIME.*?Minimum:\s*([\d.]+)\s*s', 'wall_min'),
            (r'WALL-CLOCK TIME.*?Maximum:\s*([\d.]+)\s*s', 'wall_max'),
            (r'WALL-CLOCK TIME.*?Std Deviation:\s*([\d.]+)\s*s', 'wall_std'),
            (r'WALL-CLOCK TIME.*?Coeff\. Variation:\s*([\d.]+)\s*%', 'wall_cv'),
            (r'CPU TIME.*?Mean:\s*([\d.]+)\s*s', 'cpu_mean'),
        ]
        
        for pattern, key in patterns:
            match = re.search(pattern, content, re.DOTALL)
            if match:
                data[key] = float(match.group(1))
                
    except Exception as e:
        print(f"Error parsing {filepath}: {e}")
    return data


def load_results(results_dir, subdir):
    """Loads all results from a subdirectory (parallel or parallel_soa)"""
    data = {}
    results_path = Path(results_dir) / subdir
    
    if not results_path.exists():
        print(f"⚠ Directory not found: {results_path}")
        return data
    
    for thread_dir in results_path.iterdir():
        if thread_dir.is_dir() and thread_dir.name.startswith("threads_"):
            csv_file = thread_dir / "performance_stats.csv"
            txt_file = thread_dir / "performance_stats.txt"
            
            if csv_file.exists():
                result = parse_csv_file(csv_file)
            elif txt_file.exists():
                result = parse_txt_file(txt_file)
            else:
                continue
                
            if result.get('threads'):
                data[result['threads']] = result
                
    return data


def load_sequential(results_dir):
    """Loads sequential results"""
    seq_path = Path(results_dir) / "sequential"
    csv_file = seq_path / "performance_stats.csv"
    txt_file = seq_path / "performance_stats.txt"
    
    if csv_file.exists():
        return parse_csv_file(csv_file)
    elif txt_file.exists():
        return parse_txt_file(txt_file)
    return None


# ============================
# PLOTTING FUNCTIONS
# ============================

def plot_wall_time(parallel_data, soa_data, seq_data, output_dir):
    """Chart 1: Mean time vs number of threads"""
    plt.figure(figsize=(12, 7))
    
    threads_p = sorted(parallel_data.keys())
    threads_s = sorted(soa_data.keys())
    
    times_p = [parallel_data[t]['wall_mean'] for t in threads_p]
    times_s = [soa_data[t]['wall_mean'] for t in threads_s]
    
    std_p = [parallel_data[t].get('wall_std', 0) for t in threads_p]
    std_s = [soa_data[t].get('wall_std', 0) for t in threads_s]
    
    plt.errorbar(threads_p, times_p, yerr=std_p, marker='o', linewidth=2, 
                 markersize=8, capsize=5, label=LABEL_PARALLEL, color=COLOR_PARALLEL)
    plt.errorbar(threads_s, times_s, yerr=std_s, marker='s', linewidth=2, 
                 markersize=8, capsize=5, label=LABEL_SOA, color=COLOR_SOA)
    
    if seq_data:
        plt.axhline(y=seq_data['wall_mean'], color=COLOR_SEQ, linestyle='--', 
                   linewidth=2, label=f'Sequential ({seq_data["wall_mean"]:.2f}s)')
    
    plt.xlabel("Number of Threads", fontsize=12)
    plt.ylabel("Mean Time (s)", fontsize=12)
    plt.title("Execution Time vs Number of Threads", fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.legend(fontsize=10)
    plt.xticks(sorted(set(threads_p + threads_s)))
    plt.tight_layout()
    plt.savefig(f"{output_dir}/01_time_vs_threads.png", dpi=200)
    plt.close()
    print("  → Saved: 01_time_vs_threads.png")


def plot_speedup(parallel_data, soa_data, seq_data, output_dir):
    """Chart 2: Speedup vs number of threads"""
    plt.figure(figsize=(12, 7))
    
    if seq_data:
        baseline = seq_data['wall_mean']
    else:
        baseline = parallel_data.get(1, soa_data.get(1, {})).get('wall_mean', 1)
    
    threads_p = sorted(parallel_data.keys())
    threads_s = sorted(soa_data.keys())
    all_threads = sorted(set(threads_p + threads_s))
    
    speedup_p = [baseline / parallel_data[t]['wall_mean'] for t in threads_p]
    speedup_s = [baseline / soa_data[t]['wall_mean'] for t in threads_s]
    
    plt.plot(threads_p, speedup_p, marker='o', linewidth=2, markersize=8, 
             label=LABEL_PARALLEL, color=COLOR_PARALLEL)
    plt.plot(threads_s, speedup_s, marker='s', linewidth=2, markersize=8, 
             label=LABEL_SOA, color=COLOR_SOA)
    plt.plot(all_threads, all_threads, linestyle='--', linewidth=2, 
             label='Ideal speedup', color=COLOR_IDEAL, alpha=0.7)
    
    plt.xlabel("Number of Threads", fontsize=12)
    plt.ylabel("Speedup", fontsize=12)
    plt.title("Speedup vs Number of Threads", fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.legend(fontsize=10)
    plt.xticks(all_threads)
    plt.tight_layout()
    plt.savefig(f"{output_dir}/02_speedup_vs_threads.png", dpi=200)
    plt.close()
    print("  → Saved: 02_speedup_vs_threads.png")


def plot_efficiency(parallel_data, soa_data, seq_data, output_dir):
    """Chart 3: Efficiency (speedup/threads) vs threads"""
    plt.figure(figsize=(12, 7))
    
    if seq_data:
        baseline = seq_data['wall_mean']
    else:
        baseline = parallel_data.get(1, soa_data.get(1, {})).get('wall_mean', 1)
    
    threads_p = sorted(parallel_data.keys())
    threads_s = sorted(soa_data.keys())
    
    efficiency_p = [(baseline / parallel_data[t]['wall_mean']) / t * 100 for t in threads_p]
    efficiency_s = [(baseline / soa_data[t]['wall_mean']) / t * 100 for t in threads_s]
    
    plt.plot(threads_p, efficiency_p, marker='o', linewidth=2, markersize=8, 
             label=LABEL_PARALLEL, color=COLOR_PARALLEL)
    plt.plot(threads_s, efficiency_s, marker='s', linewidth=2, markersize=8, 
             label=LABEL_SOA, color=COLOR_SOA)
    plt.axhline(y=100, color=COLOR_IDEAL, linestyle='--', linewidth=2, 
               label='Ideal efficiency (100%)', alpha=0.7)
    
    plt.xlabel("Number of Threads", fontsize=12)
    plt.ylabel("Efficiency (%)", fontsize=12)
    plt.title("Parallel Efficiency vs Number of Threads", fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.legend(fontsize=10)
    plt.xticks(sorted(set(threads_p + threads_s)))
    plt.ylim(0, max(max(efficiency_p), max(efficiency_s), 100) * 1.1)
    plt.tight_layout()
    plt.savefig(f"{output_dir}/03_efficiency_vs_threads.png", dpi=200)
    plt.close()
    print("  → Saved: 03_efficiency_vs_threads.png")


def plot_cv(parallel_data, soa_data, output_dir):
    """Chart 4: Coefficient of variation (CV%) vs threads"""
    plt.figure(figsize=(12, 7))
    
    threads_p = sorted(parallel_data.keys())
    threads_s = sorted(soa_data.keys())
    
    x = np.arange(len(sorted(set(threads_p + threads_s))))
    width = 0.35
    
    all_threads = sorted(set(threads_p + threads_s))
    
    cv_p_dict = {t: parallel_data[t].get('wall_cv', 0) for t in threads_p}
    cv_s_dict = {t: soa_data[t].get('wall_cv', 0) for t in threads_s}
    
    cv_p_vals = [cv_p_dict.get(t, 0) for t in all_threads]
    cv_s_vals = [cv_s_dict.get(t, 0) for t in all_threads]
    
    plt.bar(x - width/2, cv_p_vals, width, label=LABEL_PARALLEL, color=COLOR_PARALLEL, alpha=0.8)
    plt.bar(x + width/2, cv_s_vals, width, label=LABEL_SOA, color=COLOR_SOA, alpha=0.8)
    
    plt.xlabel("Number of Threads", fontsize=12)
    plt.ylabel("Coefficient of Variation (%)", fontsize=12)
    plt.title("Measurement Variability (CV%) vs Number of Threads", fontsize=14, fontweight='bold')
    plt.xticks(x, all_threads)
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3, axis='y')
    plt.tight_layout()
    plt.savefig(f"{output_dir}/04_cv_vs_threads.png", dpi=200)
    plt.close()
    print("  → Saved: 04_cv_vs_threads.png")


def plot_soa_comparison(parallel_data, soa_data, output_dir):
    """Chart 5: Percentage comparison parallel_soa vs parallel"""
    plt.figure(figsize=(12, 7))
    
    common_threads = sorted(set(parallel_data.keys()) & set(soa_data.keys()))
    
    if not common_threads:
        print("  ⚠ No common threads between parallel and parallel_soa")
        return
    
    # Calculate percentage difference: (parallel - soa) / parallel * 100
    # Positive = parallel_soa is faster
    diff_percent = []
    for t in common_threads:
        time_parallel = parallel_data[t]['wall_mean']
        time_soa = soa_data[t]['wall_mean']
        diff = (time_parallel - time_soa) / time_parallel * 100
        diff_percent.append(diff)
    
    colors = [COLOR_SOA if d > 0 else COLOR_PARALLEL for d in diff_percent]
    
    x = np.arange(len(common_threads))
    bars = plt.bar(x, diff_percent, color=colors, alpha=0.8, edgecolor='black')
    
    for bar, diff in zip(bars, diff_percent):
        height = bar.get_height()
        plt.annotate(f'{diff:.1f}%',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3 if height >= 0 else -15),
                    textcoords="offset points",
                    ha='center', va='bottom' if height >= 0 else 'top',
                    fontsize=9, fontweight='bold')
    
    plt.axhline(y=0, color='black', linestyle='-', linewidth=0.5)
    plt.xlabel("Number of Threads", fontsize=12)
    plt.ylabel("parallel_soa improvement vs parallel (%)", fontsize=12)
    plt.title("Percentage Comparison: parallel_soa vs parallel\n(Positive = parallel_soa faster)", fontsize=14, fontweight='bold')
    plt.xticks(x, common_threads)
    plt.grid(True, alpha=0.3, axis='y')
    
    from matplotlib.patches import Patch
    legend_elements = [Patch(facecolor=COLOR_SOA, label='parallel_soa faster'),
                      Patch(facecolor=COLOR_PARALLEL, label='parallel faster')]
    plt.legend(handles=legend_elements, fontsize=10)
    
    plt.tight_layout()
    plt.savefig(f"{output_dir}/05_comparison_soa_vs_parallel.png", dpi=200)
    plt.close()
    print("  → Saved: 05_comparison_soa_vs_parallel.png")


def plot_combined_summary(parallel_data, soa_data, seq_data, output_dir):
    """Combined 2x2 summary chart"""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    
    if seq_data:
        baseline = seq_data['wall_mean']
    else:
        baseline = parallel_data.get(1, soa_data.get(1, {})).get('wall_mean', 1)
    
    threads_p = sorted(parallel_data.keys())
    threads_s = sorted(soa_data.keys())
    all_threads = sorted(set(threads_p + threads_s))
    
    # 1. Time
    ax1 = axes[0, 0]
    times_p = [parallel_data[t]['wall_mean'] for t in threads_p]
    times_s = [soa_data[t]['wall_mean'] for t in threads_s]
    ax1.plot(threads_p, times_p, marker='o', linewidth=2, label=LABEL_PARALLEL, color=COLOR_PARALLEL)
    ax1.plot(threads_s, times_s, marker='s', linewidth=2, label=LABEL_SOA, color=COLOR_SOA)
    if seq_data:
        ax1.axhline(y=seq_data['wall_mean'], color=COLOR_SEQ, linestyle='--', alpha=0.7)
    ax1.set_xlabel("Threads")
    ax1.set_ylabel("Time (s)")
    ax1.set_title("Execution Time")
    ax1.grid(True, alpha=0.3)
    ax1.legend(fontsize=8)
    ax1.set_xticks(all_threads)
    
    # 2. Speedup
    ax2 = axes[0, 1]
    speedup_p = [baseline / parallel_data[t]['wall_mean'] for t in threads_p]
    speedup_s = [baseline / soa_data[t]['wall_mean'] for t in threads_s]
    ax2.plot(threads_p, speedup_p, marker='o', linewidth=2, label=LABEL_PARALLEL, color=COLOR_PARALLEL)
    ax2.plot(threads_s, speedup_s, marker='s', linewidth=2, label=LABEL_SOA, color=COLOR_SOA)
    ax2.plot(all_threads, all_threads, linestyle='--', color=COLOR_IDEAL, alpha=0.7, label='Ideal')
    ax2.set_xlabel("Threads")
    ax2.set_ylabel("Speedup")
    ax2.set_title("Speedup")
    ax2.grid(True, alpha=0.3)
    ax2.legend(fontsize=8)
    ax2.set_xticks(all_threads)
    
    # 3. Efficiency
    ax3 = axes[1, 0]
    efficiency_p = [(baseline / parallel_data[t]['wall_mean']) / t * 100 for t in threads_p]
    efficiency_s = [(baseline / soa_data[t]['wall_mean']) / t * 100 for t in threads_s]
    ax3.plot(threads_p, efficiency_p, marker='o', linewidth=2, label=LABEL_PARALLEL, color=COLOR_PARALLEL)
    ax3.plot(threads_s, efficiency_s, marker='s', linewidth=2, label=LABEL_SOA, color=COLOR_SOA)
    ax3.axhline(y=100, color=COLOR_IDEAL, linestyle='--', alpha=0.7)
    ax3.set_xlabel("Threads")
    ax3.set_ylabel("Efficiency (%)")
    ax3.set_title("Efficiency")
    ax3.grid(True, alpha=0.3)
    ax3.legend(fontsize=8)
    ax3.set_xticks(all_threads)
    
    # 4. Comparison %
    ax4 = axes[1, 1]
    common_threads = sorted(set(threads_p) & set(threads_s))
    diff_percent = [(parallel_data[t]['wall_mean'] - soa_data[t]['wall_mean']) / parallel_data[t]['wall_mean'] * 100 
                    for t in common_threads]
    colors = [COLOR_SOA if d > 0 else COLOR_PARALLEL for d in diff_percent]
    ax4.bar(range(len(common_threads)), diff_percent, color=colors, alpha=0.8)
    ax4.axhline(y=0, color='black', linestyle='-', linewidth=0.5)
    ax4.set_xlabel("Threads")
    ax4.set_ylabel("parallel_soa improvement (%)")
    ax4.set_title("parallel_soa vs parallel")
    ax4.set_xticks(range(len(common_threads)))
    ax4.set_xticklabels(common_threads)
    ax4.grid(True, alpha=0.3, axis='y')
    
    plt.suptitle("Performance Comparison: parallel vs parallel_soa", 
                 fontsize=16, fontweight='bold')
    plt.tight_layout()
    plt.savefig(f"{output_dir}/06_summary.png", dpi=200)
    plt.close()
    print("  → Saved: 06_summary.png")


def generate_report(parallel_data, soa_data, seq_data, output_dir):
    """Generates a text report with summary"""
    
    if seq_data:
        baseline = seq_data['wall_mean']
    else:
        baseline = parallel_data.get(1, soa_data.get(1, {})).get('wall_mean', 1)
    
    common_threads = sorted(set(parallel_data.keys()) & set(soa_data.keys()))
    
    report = []
    report.append("=" * 80)
    report.append("        COMPARISON REPORT: parallel vs parallel_soa")
    report.append("=" * 80)
    report.append("")
    
    if seq_data:
        report.append(f"Baseline (sequential): {baseline:.3f} s")
        report.append("")
    
    report.append(f"{'Threads':>8} | {'parallel':>12} | {'parallel_soa':>12} | {'Diff %':>10} | {'Speedup p':>12} | {'Speedup soa':>12}")
    report.append("-" * 80)
    
    for t in common_threads:
        time_p = parallel_data[t]['wall_mean']
        time_soa = soa_data[t]['wall_mean']
        diff = (time_p - time_soa) / time_p * 100
        speedup_p = baseline / time_p
        speedup_soa = baseline / time_soa
        report.append(f"{t:>8} | {time_p:>12.3f} | {time_soa:>12.3f} | {diff:>+9.2f}% | {speedup_p:>12.2f}x | {speedup_soa:>12.2f}x")
    
    report.append("")
    report.append("=" * 80)
    
    avg_improvement = np.mean([(parallel_data[t]['wall_mean'] - soa_data[t]['wall_mean']) / parallel_data[t]['wall_mean'] * 100 
                               for t in common_threads])
    
    best_p_t = min(parallel_data.keys(), key=lambda t: parallel_data[t]['wall_mean'])
    best_soa_t = min(soa_data.keys(), key=lambda t: soa_data[t]['wall_mean'])
    
    report.append("")
    report.append("SUMMARY:")
    report.append(f"  Average parallel_soa improvement: {avg_improvement:+.2f}%")
    report.append(f"  Best parallel time: {parallel_data[best_p_t]['wall_mean']:.3f}s ({best_p_t} threads)")
    report.append(f"  Best parallel_soa time: {soa_data[best_soa_t]['wall_mean']:.3f}s ({best_soa_t} threads)")
    report.append(f"  Best parallel speedup: {baseline / parallel_data[best_p_t]['wall_mean']:.2f}x")
    report.append(f"  Best parallel_soa speedup: {baseline / soa_data[best_soa_t]['wall_mean']:.2f}x")
    report.append("")
    report.append("=" * 80)
    
    report_text = "\n".join(report)
    report_path = f"{output_dir}/comparison_report.txt"
    with open(report_path, 'w') as f:
        f.write(report_text)
    
    print(f"\n{report_text}")
    print(f"\n  → Report saved: {report_path}")


# ============================
# MAIN
# ============================

def main():
    print("=" * 60)
    print("  PERFORMANCE COMPARISON: parallel vs parallel_soa")
    print("=" * 60)
    print()
    
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    print("📂 Loading data...")
    parallel_data = load_results(RESULTS_DIR, "parallel")
    soa_data = load_results(RESULTS_DIR, "parallel_soa")
    seq_data = load_sequential(RESULTS_DIR)
    
    if seq_data:
        print(f"✓ Sequential data: {seq_data['wall_mean']:.3f}s")
    
    print(f"✓ Found {len(parallel_data)} configurations for parallel")
    print(f"✓ Found {len(soa_data)} configurations for parallel_soa")
    
    if not parallel_data or not soa_data:
        print("❌ Insufficient data for comparison")
        return
    
    print("\n📈 Generating charts...")
    plot_wall_time(parallel_data, soa_data, seq_data, OUTPUT_DIR)
    plot_speedup(parallel_data, soa_data, seq_data, OUTPUT_DIR)
    plot_efficiency(parallel_data, soa_data, seq_data, OUTPUT_DIR)
    plot_cv(parallel_data, soa_data, OUTPUT_DIR)
    plot_soa_comparison(parallel_data, soa_data, OUTPUT_DIR)
    plot_combined_summary(parallel_data, soa_data, seq_data, OUTPUT_DIR)
    
    print("\n📝 Generating report...")
    generate_report(parallel_data, soa_data, seq_data, OUTPUT_DIR)
    
    print(f"\n✅ Analysis complete! Charts saved in: {OUTPUT_DIR}/")


if __name__ == "__main__":
    main()
