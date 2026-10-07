import matplotlib.pyplot as plt

data = [
    {"Archieved Size": 69132, "PA": 0.870, "GA": 0.870},
    {"Archieved Size": 91952, "PA": 0.878, "GA": 0.878},
    {"Archieved Size": 55388, "PA": 0.916, "GA": 0.916},
    {"Archieved Size": 63424, "PA": 0.599, "GA": 0.599},
    {"Archieved Size": 55164, "PA": 0.870, "GA": 0.870},
]


archived_sizes = [entry["Archieved Size"] for entry in data]
pa_values = [entry["PA"] for entry in data]
ga_values = [entry["GA"] for entry in data]
indices = range(len(data))

font_size = 14


plt.figure(figsize=(10, 6))

plt.subplot(2, 1, 1)
plt.bar(indices, archived_sizes, color='steelblue', alpha=0.7, label='Archived Size')
plt.ylabel('Archived Size', fontsize=font_size)
plt.title('Archived Size and Accuracy Metrics', fontsize=font_size)
plt.xticks(indices, [f'Log {i+1}' for i in indices], fontsize=font_size)
plt.grid(axis='y', linestyle='--')


plt.subplot(2, 1, 2)
plt.plot(indices, pa_values, marker='o', linestyle='-', color='orange', label='PA', linewidth=2)
plt.plot(indices, ga_values, marker='x', linestyle='-', color='green', label='GA', linewidth=2)
plt.ylabel('Accuracy Metrics (PA, GA)', fontsize=font_size)
plt.xlabel('Log Entries', fontsize=font_size)
plt.xticks(indices, [f'Log {i+1}' for i in indices], fontsize=font_size)
plt.ylim(0, 1)
plt.grid(axis='y', linestyle='--')
plt.legend(fontsize=font_size)


plt.tight_layout()


plt.savefig('log_analysis.pdf', format='pdf', bbox_inches='tight')

plt.show()