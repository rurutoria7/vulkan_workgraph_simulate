import csv
import matplotlib.pyplot as plt

edges = []
with open('koch_vertices.csv', 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        edges.append((float(row['x1']), float(row['y1']), float(row['x2']), float(row['y2'])))

fig, ax = plt.subplots(1, 1, figsize=(10, 10))
for x1, y1, x2, y2 in edges:
    ax.plot([x1, x2], [y1, y2], 'b-', linewidth=0.5)

ax.set_aspect('equal')
ax.set_title(f'Koch Snowflake (depth 4, {len(edges)} edges)')
plt.tight_layout()
plt.savefig('koch_snowflake_verify.png', dpi=150)
print(f'Saved koch_snowflake_verify.png with {len(edges)} edges')
