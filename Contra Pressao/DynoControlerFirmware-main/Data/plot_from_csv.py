import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

df = pd.read_csv("data/data_log.csv")
df.columns = df.columns.str.strip()
time = df["time_sec"]

fig = plt.figure(figsize=(12, 14))
gs = fig.add_gridspec(5, 1, height_ratios=[2, 2, 2, 2, 1])

def nice_yticks(ax, data, num=7, decimals=2, padding=0.05):
    """Set y-limits with padding, reduce ticks to num, format labels."""
    vmin, vmax = data.min(), data.max()
    span = vmax - vmin
    ax.set_ylim(vmin - span*padding, vmax + span*padding)
    ticks = np.linspace(vmin, vmax, num)
    ax.set_yticks(ticks)
    ax.set_yticklabels([f"{t:.{decimals}f}" for t in ticks])

# Pressure
ax0 = fig.add_subplot(gs[0, 0])
ax0.plot(time, df["pressure_bar"], color='tab:blue')
ax0.set_ylabel("Pressure (bar)")
ax0.grid(True)
nice_yticks(ax0, df["pressure_bar"], num=7, decimals=3)

# Error
ax1 = fig.add_subplot(gs[1, 0])
ax1.plot(time, df["error"], color='tab:orange')
ax1.set_ylabel("Error")
ax1.grid(True)
nice_yticks(ax1, df["error"], num=7, decimals=4)

# Voltage
ax2 = fig.add_subplot(gs[2, 0])
ax2.plot(time, df["voltage_V"], color='tab:green')
ax2.set_ylabel("Voltage (V)")
ax2.grid(True)
nice_yticks(ax2, df["voltage_V"], num=7, decimals=3)

# PWM
ax3 = fig.add_subplot(gs[3, 0])
ax3.plot(time, df["pwm"], color='tab:red')
ax3.set_ylabel("PWM")
ax3.grid(True)
nice_yticks(ax3, df["pwm"], num=7, decimals=3)

# Control (discrete)
ax4 = fig.add_subplot(gs[4, 0])
ax4.step(time, df["control"], where='post', color='tab:purple')
ax4.set_ylabel("Control")
ax4.set_yticks([0, 1, 2])
ax4.set_xlabel("Time (s)")
ax4.grid(True)
ax4.set_ylim(-0.5, 2.5)

plt.tight_layout()
plt.savefig("data_plot.png", dpi=300)
plt.show()