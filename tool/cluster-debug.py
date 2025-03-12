#!/usr/bin/env -S uv run --script

# /// script
# dependencies = ["pandas", "matplotlib", "numpy", "mplcursors"]
# ///

import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import numpy as np
import mplcursors

import argparse

parser = argparse.ArgumentParser(description='Plot boxes from a CSV file')
parser.add_argument('csv_file', type=str, help='Path to the CSV file')
parser.add_argument('matrix_file', type=str, help='Path to the inverse projection matrix file')

parsed_args = parser.parse_args()

# Read the CSV file
df = pd.read_csv(parsed_args.csv_file, skipinitialspace=True)

# Create a new figure
fig = plt.figure(figsize=(10, 10))
ax = fig.add_subplot(111, projection='3d')

# Function to parse matrix row from string
def parse_matrix_row(row_str):
    arr = np.array([float(x.strip()) for x in row_str.split(',')])
    return arr

# Function to read inverse projection matrix
def read_inverse_projection(file_path):
    df = pd.read_csv(file_path)
    matrix = np.zeros((4, 4))
    for i in range(4):
        row_data = df[df['Name'] == f'inv_proj.row{i}']['Value'].iloc[0]
        matrix[i] = parse_matrix_row(row_data)
    return matrix

# Function to generate frustum vertices
def get_frustum_vertices(inv_proj):
    # Near and far plane points in NDC space
    ndc_points = np.array([
        [-1, -1, 0, 1],  # near bottom left
        [1, -1, 0, 1],   # near bottom right
        [1, 1, 0, 1],    # near top right
        [-1, 1, 0, 1],   # near top left
        [-1, -1, 1, 1],  # far bottom left
        [1, -1, 1, 1],   # far bottom right
        [1, 1, 1, 1],    # far top right
        [-1, 1, 1, 1],   # far top left
    ])

    # Transform to world space
    vertices = []
    for ndc_point in ndc_points:
        clip_space = np.dot(inv_proj, ndc_point)
        world_point = clip_space[:3] / clip_space[3]
        world_point[2] *= -1  # Invert Z axis
        vertices.append(world_point)

    return np.array(vertices)

# Function to create vertices of a box given min and max points
def get_box_vertices(min_point, max_point):
    vertices = np.array([
        [min_point[0], min_point[1], min_point[2]],
        [max_point[0], min_point[1], min_point[2]],
        [max_point[0], max_point[1], min_point[2]],
        [min_point[0], max_point[1], min_point[2]],
        [min_point[0], min_point[1], max_point[2]],
        [max_point[0], min_point[1], max_point[2]],
        [max_point[0], max_point[1], max_point[2]],
        [min_point[0], max_point[1], max_point[2]]
    ])
    return vertices

# Function to plot a single box
def plot_box(ax, vertices, color='b', alpha=0.2):
    # Define the faces of the box
    faces = [
        [vertices[j] for j in [0, 1, 2, 3]],  # bottom
        [vertices[j] for j in [4, 5, 6, 7]],  # top
        [vertices[j] for j in [0, 1, 5, 4]],  # front
        [vertices[j] for j in [2, 3, 7, 6]],  # back
        [vertices[j] for j in [1, 2, 6, 5]],  # right
        [vertices[j] for j in [0, 3, 7, 4]]   # left
    ]

    # Create 3D collection
    collection = Poly3DCollection(faces, alpha=alpha)
    collection.set_facecolor(color)
    collection.set_edgecolor('black')
    ax.add_collection3d(collection)

# Plot each box
for idx, row in df.iterrows():
    min_point = np.array([row['clusters.bounds_min.x'], row['clusters.bounds_min.y'], row['clusters.bounds_min.z']])
    max_point = np.array([row['clusters.bounds_max.x'], row['clusters.bounds_max.y'], row['clusters.bounds_max.z']])
    vertices = get_box_vertices(min_point, max_point)
    plot_box(ax, vertices, color=plt.cm.viridis(idx/len(df)))

    element_id = row['Element']

    center = (min_point + max_point) / 2

    ax.text(center[0], center[1], center[2], str(element_id), color='black')

# Plot frustum if matrix file is provided
if parsed_args.matrix_file:
    inv_proj = read_inverse_projection(parsed_args.matrix_file)
    frustum_vertices = get_frustum_vertices(inv_proj)

    # Define frustum faces
    faces = [
        [frustum_vertices[j] for j in [0, 1, 2, 3]],  # near
        [frustum_vertices[j] for j in [4, 5, 6, 7]],  # far
        [frustum_vertices[j] for j in [0, 1, 5, 4]],  # bottom
        [frustum_vertices[j] for j in [2, 3, 7, 6]],  # top
        [frustum_vertices[j] for j in [1, 2, 6, 5]],  # right
        [frustum_vertices[j] for j in [0, 3, 7, 4]]   # left
    ]

    # Create 3D collection for frustum
    frustum_collection = Poly3DCollection(faces, alpha=0.1)
    frustum_collection.set_facecolor('red')
    frustum_collection.set_edgecolor('black')
    ax.add_collection3d(frustum_collection)

# Set labels
ax.set_xlabel('X')
ax.set_ylabel('Y')
ax.set_zlabel('Z')

# Set equal aspect ratio
ax.set_box_aspect([1,1,1])

# Set view to isometric (-Z to left, Y up)
ax.view_init(elev=30, azim=60, vertical_axis='y')

plt.show()
