import matplotlib.pyplot as plt
import csv

def plot_data_from_csv(filename):
    """Plots data from a CSV file with two sets of data on one graph."""

    try:
        with open(filename, 'r') as csvfile:
            reader = csv.reader(csvfile)
            header = next(reader)  # Skip header row

            speed_a = []
            ratio_a = []
            speed_b = []
            ratio_b = []

            for row in reader:
                try:
                    speed_a_val = row[0]
                    ratio_a_val = row[1]
                    speed_b_val = row[3]  # Index changed to 3 and 4
                    ratio_b_val = row[4]


                    if speed_a_val:  # Check if the cell is not empty
                        speed_a.append(float(speed_a_val))
                        ratio_a.append(float(ratio_a_val.replace(",", "."))) # Replace comma with dot for float conversion

                    if speed_b_val:  # Check if the cell is not empty
                        speed_b.append(float(speed_b_val))
                        ratio_b.append(float(ratio_b_val))

                except (ValueError, IndexError):
                    print(f"Warning: Skipping invalid row: {row}")

    except FileNotFoundError:
        print(f"Error: File '{filename}' not found.")
        return

    # Create the plot
    plt.figure(figsize=(10, 6))

    plt.plot(speed_a, ratio_a, marker='o', linestyle='-', label='Data A')
    plt.plot(speed_b, ratio_b, marker='x', linestyle='-', label='Data B')

    plt.xlabel("Compression Speed")
    plt.ylabel("Compression Ratio")
    plt.title("Compression Performance")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()


# Example usage:
plot_data_from_csv("test.csv")  # Replace with your CSV file name