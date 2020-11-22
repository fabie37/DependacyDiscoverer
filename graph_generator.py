import matplotlib.pyplot as plt
import numpy as np

# How to identify lines
thread_line = "Thread Count:"

# Number of threads
with open("test_results.txt", 'r') as f:
    for line in f:
        if line.startswith(thread_line):
            thread_number = int(line.split(" ")[2])
max_threads = thread_number

# Results Array[row][col]
# row: thread_number
# col: real[0]  user[1]  sys[2]
results = np.zeros((max_threads, 3), dtype=float)

# Sub-Experiment Array[row] [col]
# row: expriment number
# col: real[0]  user[1]  sys[2]
sub = np.zeros((3, 3), dtype=float)
sub = None

# Expriment number
experiment_numb = 0

# Get results
with open("test_results.txt", 'r') as f:
    for line in f:
        if line.startswith(thread_line):
            if sub is None:
                thread_number = int(line.split(" ")[2]) - 1
            else:
                medians = np.median(sub, axis=0)
                results[thread_number][0] = float(medians[0])
                results[thread_number][1] = float(medians[1])
                results[thread_number][2] = float(medians[2])
                thread_number = int(line.split(" ")[2]) - 1
                sub = np.zeros((3, 3), dtype=float)
                experiment_numb = 0
        elif line == '\n':
            pass
        else:
            if sub is None:
                sub = np.zeros((3, 3),  dtype=float)
            exp_sub = line.strip().split(" ")
            sub[experiment_numb][0] = float(exp_sub[1][-6:-1])
            sub[experiment_numb][1] = float(exp_sub[3][-6:-1])
            sub[experiment_numb][2] = float(exp_sub[5][-6:-1])
            experiment_numb += 1

# Misses final results
medians = np.median(sub, axis=0)
results[thread_number][0] = float(medians[0])
results[thread_number][1] = float(medians[1])
results[thread_number][2] = float(medians[2])

# Plot graph
fig = plt.figure()
ax = plt.axes()

# Extract data
threads = np.arange(1, max_threads+1)
real = results[:, 0] * 1000
user = results[:, 1] * 1000
sys = results[:, 2] * 1000

# Plot Lines
Lreal, = ax.plot(threads, real, color='blue', marker='o', label="real")
Luser = ax.plot(threads, user, color='red', marker='x', label="user")
Lsys = ax.plot(threads, sys, color='green', marker='^', label="sys")
ax.legend()

# Label axis
ax.set_xlabel("Number of Threads")
ax.set_ylabel("Time Taken (ms)")
ax.set_title("Median Runtime on Different Threads")

# Set Ticks
start, end = ax.get_ylim()
ax.yaxis.set_ticks(np.arange(0, end, 4))

# Show Graph
plt.show()
