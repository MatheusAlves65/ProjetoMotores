import pandas as pd

df = pd.read_csv("data_log.csv")

# Show the real column names with quotes to catch spaces/invisible chars
print("Columns in CSV:", [repr(c) for c in df.columns])