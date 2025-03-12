#!/usr/bin/env -S uv run --script

# /// script
# dependencies = ["numpy", "pyvista"]
# ///

import pyvista as pv
import numpy as np

# Create plotter
pl = pv.Plotter()

pl.add_axes()

# add the origin as a point
pl.add_point_labels([0,0,0], ['origin'])

# Read file and plot
with open('test.txt', 'r') as file:
    for line in file:
        # Parse sphere info
        light_start = line.find('light (')
        light_end = line.find(')', light_start)
        light_info = line[light_start+7:light_end].split(',')

        x = float(light_info[0])
        y = float(light_info[1])
        z = float(light_info[2].split('r:')[0])
        r = float(light_info[3].split(':')[1])

        # Parse cluster info
        is_in = not 'not in' in line
        color = 'green' if is_in else 'red'
        opacity = 0.3

        cluster_id = line.split('cluster  (')[1].split(')')[0] if 'cluster  (' in line else None

        # Add sphere
        sphere = pv.Sphere(radius=r, center=(x, y, z))
        pl.add_mesh(sphere, color='blue', opacity=opacity)

        # Parse and add box
        cluster_start = line.rfind('((')
        cluster_end = line.rfind('))')
        if cluster_start != -1 and cluster_end != -1:
            cluster_info = line[cluster_start+2:cluster_end].split('), (')
            min_point = np.array(list(map(float, cluster_info[0].split(', '))))
            max_point = np.array(list(map(float, cluster_info[1].split(', '))))

            box = pv.Box(bounds=[min_point[0], max_point[0],
                               min_point[1], max_point[1],
                               min_point[2], max_point[2]])
            pl.add_mesh(box, color=color, opacity=opacity, style='wireframe')

            if cluster_id is not None:
                center = (min_point + max_point) / 2
                pl.add_point_labels(
                    points=[center],
                    labels=[f'ID: {cluster_id}'],
                    point_size=0,  # Hide the point marker
                    text_color='green' if is_in else 'red',
                    font_size=12,
                    bold=True,
                    shadow=False
                )

pl.show()
