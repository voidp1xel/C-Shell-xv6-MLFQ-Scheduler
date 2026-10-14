import matplotlib.pyplot as plt
import numpy as np

AUTHOR_NAME = "Aniket Goel"

def add_watermark():
    """Adds author attribution watermark to generated figures."""
    plt.text(
        0.95, 0.95, AUTHOR_NAME,
        ha='right', va='top',
        transform=plt.gca().transAxes,
        fontsize=10, color="gray", alpha=0.7
    )

def plot_mlfq_timeline():
    """Generates scatter plot of MLFQ process queue demotion and periodic priority boosting."""
    fig, ax = plt.subplots(figsize=(10, 6))

    p1_x = [1, 5, 13, 29]                  # Short burst process
    p1_y = [0, 1, 2, 3]

    p2_x = [2, 6, 14, 30, 48, 52]          # Medium burst process (boosted at tick 48)
    p2_y = [0, 1, 2, 3, 0, 1]

    p3_x = [3, 7, 15, 31, 48, 53, 61, 77] # Long burst process
    p3_y = [0, 1, 2, 3, 0, 1, 2, 3]

    ax.scatter(p1_x, p1_y, label='Process 1 (Short Burst)', color='blue', alpha=0.7)
    ax.scatter(p2_x, p2_y, label='Process 2 (Medium Burst)', color='green', alpha=0.7)
    ax.scatter(p3_x, p3_y, label='Process 3 (Long Burst)', color='red', alpha=0.7)

    # Periodic priority boost marker
    ax.axvline(x=48, color='purple', linestyle='--', alpha=0.5, label='Priority Boost (48 ticks)')

    ax.set_yticks([0, 1, 2, 3])
    ax.set_ylabel('Queue ID (Priority Level)')
    ax.set_xlabel('Elapsed Time (Ticks)')
    ax.set_title('MLFQ Process Queue Demotion & Priority Boosting')
    ax.invert_yaxis()  # Queue 0 at the top (highest priority)
    ax.legend()
    ax.grid(True, linestyle=':', alpha=0.6)

    add_watermark()
    fig.tight_layout()
    plt.savefig('mlfq_timeline.png', dpi=300)
    print("Saved 'mlfq_timeline.png'")
    plt.close(fig)

def plot_scheduler_comparison():
    """Generates performance comparison chart across Round Robin, FCFS, and MLFQ."""
    schedulers = ['Round Robin (RR)', 'FCFS', 'MLFQ']

    avg_turnaround_time = [150, 140, 145]
    avg_waiting_time    = [120, 110, 115]
    avg_response_time   = [10,  110, 12]

    x = np.arange(len(schedulers))
    width = 0.25

    fig, ax = plt.subplots(figsize=(9, 6))
    ax.bar(x - width, avg_turnaround_time, width, label='Avg Turnaround Time', color='skyblue')
    ax.bar(x, avg_waiting_time, width, label='Avg Waiting Time', color='salmon')
    ax.bar(x + width, avg_response_time, width, label='Avg Response Time', color='lightgreen')

    ax.set_ylabel('Time (Ticks)')
    ax.set_title('Scheduler Performance Comparison: RR vs FCFS vs MLFQ')
    ax.set_xticks(x)
    ax.set_xticklabels(schedulers)
    ax.legend()

    add_watermark()
    fig.tight_layout()
    plt.savefig('scheduler_comparison.png', dpi=300)
    print("Saved 'scheduler_comparison.png'")
    plt.close(fig)

if __name__ == "__main__":
    plot_mlfq_timeline()
    plot_scheduler_comparison()