import serial
import time
import csv
import os

# ---------------- CONFIG ----------------
PORT = "COM4"
BAUD_RATE = 9600
# ----------------------------------------

def main():

    # Save file in same directory as script
    base_dir = os.path.dirname(os.path.abspath(__file__))
    output_file = os.path.join(base_dir, "data_log.csv")

    print("Opening serial port...")
    ser = serial.Serial(PORT, BAUD_RATE, timeout=1)

    # Allow Arduino to reset after port opens
    time.sleep(2)

    # Clear startup garbage
    ser.reset_input_buffer()

    print(f"Connected to {PORT} at {BAUD_RATE} baud.")
    print("Logging to:", output_file)
    print("Press CTRL+C to stop.\n")

    start_time = time.time()

    # Open CSV file immediately
    with open(output_file, mode="w", newline="") as file:
        writer = csv.writer(file)

        # Write header
        writer.writerow([
            "time_sec",
            "pressure_bar",
            "error",
            "voltage_V",
            "pwm",
            "control"
        ])
        file.flush()

        try:
            while True:
                raw = ser.readline()

                if not raw:
                    continue

                # Decode safely (ignore garbage bytes)
                line = raw.decode("utf-8", errors="ignore").strip()

                if not line:
                    continue

                parts = line.split("\t")

                # Expect exactly 5 values
                if len(parts) != 5:
                    continue

                try:
                    # Apply correct scaling
                    pressure = float(parts[0]) / 10.0
                    error = float(parts[1])
                    voltage = float(parts[2]) / 10.0
                    pwm = float(parts[3])
                    control = int(parts[4])

                    current_time = time.time() - start_time

                    writer.writerow([
                        current_time,
                        pressure,
                        error,
                        voltage,
                        pwm,
                        control
                    ])

                    # Force write to disk
                    file.flush()

                    print(
                        f"t={current_time:6.2f}s | "
                        f"P={pressure:6.2f} bar | "
                        f"E={error:6.3f} | "
                        f"V={voltage:5.2f} V | "
                        f"PWM={pwm:4.2f} | "
                        f"C={control}"
                    )

                except ValueError:
                    # Ignore corrupted numeric data
                    continue

        except KeyboardInterrupt:
            print("\nStopping logger...")

    ser.close()
    print("Serial port closed.")
    print("Data saved successfully.")


if __name__ == "__main__":
    main()