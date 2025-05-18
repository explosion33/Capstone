import matplotlib.pyplot as plt

def get_data(filename):
    data = {}

    with open(filename, 'r') as file:
        lines = file.readlines()

    start_time = int(lines[0].strip().split(",")[3].strip())

    for line in lines:
        set = line.strip().split(",")
        key = set[0][1:-1]
        if key not in data:
            data[key] = {"X": [], "Y": []}

        data[key]["Y"].append(float(set[1].strip()))
        data[key]["X"].append(int(set[3].strip()) - start_time)
    return data

def rolling_avg(data, window=100):
    out = []
    for i in range(len(data)-window):
        out.append(sum(data[i:i+window]) / window)

sensor = "FTPT"
log = "log.txt"

data = get_data(log)[sensor]

dp  = 0
dpi = 0

for i in range(len(data["Y"])-100):
    delta = data["Y"][i+100] - data["Y"][i]
    if (delta > dp):
        dp  = delta
        dpi = i 

print(dpi, dp)

s = dpi#int(len(data["X"]) / 2) + 15000
f = len(data["X"])

plt.plot(data["X"][s:f], data["Y"][s:f])
plt.xlabel("Time (ms)")
plt.ylabel("Pressure (PSI)")
plt.title(sensor)
plt.grid(True)  # optional
plt.show()