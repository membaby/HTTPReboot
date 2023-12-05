import matplotlib.pyplot as plt

# Data
algorithms = ["20", "40", "60", "80", "100"]
avg_steps = [2509, 5019, 7472, 9976, 12567]
avg_expansions = [19.85, 39.92, 59.23, 79.22, 99.8]
avg_max_depth = [0.03, 0.03, 0.03, 0.02, 0.02]
avg_running_time = [0, 0.02, 0.03, 0.02, 0.02]

# Create subplots for each metric
fig, axs = plt.subplots(2, 2, figsize=(10, 8))

# Title
fig.suptitle('Performance Evaluation of HTTPReboot Web Server\nBased on Postman Runner Results', fontsize=12, fontweight='bold')

# Plot AVG Steps as a line graph
axs[0, 0].plot(algorithms, avg_steps, marker='o', color='lightcoral', linestyle='-')
axs[0, 0].set_title('Total Requests Sent')
axs[0, 0].set_ylabel('Requests Sent')

# Plot AVG Expansions as a line graph
axs[0, 1].plot(algorithms, avg_expansions, marker='o', color='lightgreen', linestyle='-')
axs[0, 1].set_title('Requests Sent per Second')
axs[0, 1].set_ylabel('Requests Sent')

# Plot AVG Max Depth as a line graph
axs[1, 0].plot(algorithms, avg_max_depth, marker='o', color='lightskyblue', linestyle='-')
axs[1, 0].set_title('Average Response Time')
axs[1, 0].set_ylabel('Response Time (s)')
axs[1, 0].set_xlabel('Number of Clients')

# Plot AVG Running Time as a line graph
axs[1, 1].plot(algorithms, avg_running_time, marker='o', color='lightseagreen', linestyle='-')
axs[1, 1].set_title('Error Rate %')
axs[1, 1].set_ylabel('Error Rate')
axs[1, 1].set_xlabel('Number of Clients')

for ax in axs.flat:
    # ax.set_xticklabels(ax.get_xticklabels(), rotation=45, ha='right')
    # fontsize
    ax.tick_params(axis='both', which='major', labelsize=6)
    # title fontsize
    ax.title.set_fontsize(10)

# Add a caption to the graph

# Adjust the layout
# plt.tight_layout()

# Save the line graphs as pictures
plt.savefig('line_graphs.png')  # Save as a PNG image
plt.savefig('line_graphs.jpg')  # Save as a JPG image

# Show the line graphs
plt.show()
