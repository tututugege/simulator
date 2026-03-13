import csv
import matplotlib.pyplot as plt
import os
import argparse

def main():
    parser = argparse.ArgumentParser(description="Plot FALCON Frontend Bottlenecks")
    parser.add_argument('--csv', type=str, default='./falcon_frontend.csv', help='Path to frontend csv file.')
    parser.add_argument('--out', type=str, default='frontend_bottleneck.png', help='Output plot image file path.')
    args = parser.parse_args()

    if not os.path.exists(args.csv):
        print(f"File not found: {args.csv}")
        return

    benchmarks = []
    data = {}
    categories = ["reset", "refetch", "icache_miss", "icache_latency", "bpu_stall", "fetch_addr_empty", "ptab_empty", "dummy_ptab", "inst_fifo_other", "other"]
    
    for cat in categories:
        data[cat] = []
    
    with open(args.csv, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            benchmarks.append(row['benchmark'])
            demand = float(row['demand'])
            for cat in categories:
                # Calculate percentage
                val = float(row[cat])
                pct = (val / demand) * 100.0 if demand > 0 else 0
                data[cat].append(pct)
                
    if not benchmarks:
        print("No data in CSV.")
        return

    fig, ax = plt.subplots(figsize=(12, 6))
    
    bottoms = [0.0] * len(benchmarks)
    # Different colors from a colormap
    colors = plt.cm.tab20.colors
    
    for i, cat in enumerate(categories):
        ax.bar(benchmarks, data[cat], label=cat, bottom=bottoms, color=colors[i % len(colors)])
        # Update bottoms
        for j in range(len(benchmarks)):
            bottoms[j] += data[cat][j]

    ax.set_ylabel('Percentage of Demand (%)')
    ax.set_title('FALCON Front-end Bottleneck Breakdown')
    plt.xticks(rotation=45, ha='right')
    
    # Legend
    plt.legend(title='Category', bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.tight_layout()
    
    plt.savefig(args.out, dpi=300)
    print(f"Plot saved to {args.out}")

if __name__ == "__main__":
    main()
