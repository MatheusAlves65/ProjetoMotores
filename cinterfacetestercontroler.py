import tkinter as tk
from tkinter import ttk
from dataclasses import dataclass
import can
import threading

# ==== CAN Structs ====
@dataclass
class AquisitionConfigStructure:
    Aquics_Enable_Continuous: int = 0
    Aquics_Enable: int = 0
    analog: bool = False
    timer: int = 1000  # milliseconds

def send_aquisition_config(aquisc: AquisitionConfigStructure) -> bytearray:
    tx_buf = bytearray(8)
    tx_buf[0] = (aquisc.timer >> 8) & 0xFF
    tx_buf[1] = aquisc.timer & 0xFF
    tx_buf[2] = int(aquisc.analog) & 0x01
    tx_buf[3] = aquisc.Aquics_Enable_Continuous & 0x03
    for i in range(4, 8):   
        tx_buf[i] = 0x00
    return tx_buf

def send_digital(digital_command, PWM1, PWM2, Enc):
    tx_buf = bytearray(5)
    tx_buf[0] = (digital_command[0] << 6) | (digital_command[1] << 4) | (digital_command[2] << 2) | digital_command[3]
    tx_buf[1] = (digital_command[4] << 6) | (digital_command[5] << 4) | (digital_command[6] << 2) | digital_command[7]
    tx_buf[2] = PWM1
    tx_buf[3] = PWM2
    tx_buf[4] = Enc
    tx_buf[5] = 0
    tx_buf[6] = 0
    tx_buf[7] = 0
    return tx_buf

# ==== GUI App ====
class CANConfigGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Aquisição + LEDs Configurador CAN")

        self.timer_var = tk.IntVar(value=1000)
        self.analog_var = tk.BooleanVar(value=False)
        self.enable_continuous_var = tk.IntVar(value=0)
        self.enable_once_var = tk.IntVar(value=0)
        self.pwm1_var = tk.IntVar(value=0)
        self.pwm2_var = tk.IntVar(value=0)
        self.enc_var = tk.IntVar(value=0)
        self.led_vars = [tk.IntVar(value=0) for _ in range(8)]

        self.build_gui()
        self.start_can_listener()

    def build_gui(self):
        frame = ttk.Frame(self.root, padding=10)
        frame.grid(row=0, column=0)

        ttk.Label(frame, text="Timer (ms):").grid(row=0, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.timer_var).grid(row=0, column=1)

        ttk.Checkbutton(frame, text="Analog Input", variable=self.analog_var).grid(row=1, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(frame, text="Enable Once", variable=self.enable_once_var).grid(row=2, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(frame, text="Enable Continuous", variable=self.enable_continuous_var).grid(row=3, column=0, columnspan=2, sticky="w")

        ttk.Button(frame, text="Enviar Config Aquisição", command=self.send_can_config).grid(row=4, column=0, columnspan=2, pady=10)

        ttk.Label(frame, text="PWM1:").grid(row=5, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.pwm1_var).grid(row=5, column=1)

        ttk.Label(frame, text="PWM2:").grid(row=6, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.pwm2_var).grid(row=6, column=1)

        ttk.Label(frame, text="Encoder:").grid(row=7, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.enc_var).grid(row=7, column=1)

        ttk.Label(frame, text="LEDs:").grid(row=8, column=0, sticky="w")
        led_frame = ttk.Frame(frame)
        led_frame.grid(row=8, column=1, sticky="w")
        for i in range(8):
            ttk.Checkbutton(led_frame, text=str(i+1), variable=self.led_vars[i]).grid(row=0, column=i)

        ttk.Button(frame, text="Enviar LEDs + PWM", command=self.send_digital_cmd).grid(row=9, column=0, columnspan=2, pady=10)

        self.status = ttk.Label(frame, text="Status: Aguarde")
        self.status.grid(row=10, column=0, columnspan=2)

        self.msg_box = tk.Text(frame, height=10, width=60)
        self.msg_box.grid(row=11, column=0, columnspan=2, pady=5)
        self.msg_box.insert(tk.END, "Mensagens CAN recebidas:\n")
        self.msg_box.configure(state='disabled')

    def send_can_config(self):
        config = AquisitionConfigStructure(
            Aquics_Enable_Continuous=self.enable_continuous_var.get(),
            Aquics_Enable=self.enable_once_var.get(),
            analog=self.analog_var.get(),
            timer=self.timer_var.get()
        )
        data = send_aquisition_config(config)
        try:
            bus = can.interface.Bus(channel='can0', bustype='socketcan')
            msg = can.Message(arbitration_id=0x404, data=data, is_extended_id=False)
            bus.send(msg)
            self.status.config(text="Status: Configuração enviada", foreground="green")
        except can.CanError as e:
            self.status.config(text=f"Erro ao enviar: {e}", foreground="red")

    def send_digital_cmd(self):
        digital_command = [var.get() for var in self.led_vars]
        pwm1 = self.pwm1_var.get()
        pwm2 = self.pwm2_var.get()
        enc = self.enc_var.get()
        data = send_digital(digital_command, pwm1, pwm2, enc)
        try:
            bus = can.interface.Bus(channel='can0', bustype='socketcan')
            msg = can.Message(arbitration_id=0x402, data=data, is_extended_id=False)
            bus.send(msg)
            self.status.config(text="Status: LEDs + PWM enviados", foreground="green")
        except can.CanError as e:
            self.status.config(text=f"Erro ao enviar: {e}", foreground="red")

    def start_can_listener(self):
        def listener():
            bus = can.interface.Bus(channel='can0', bustype='socketcan')
            for msg in bus:
                self.msg_box.configure(state='normal')
                self.msg_box.insert(tk.END, f"ID: {hex(msg.arbitration_id)} | Data: {msg.data.hex()}\n")
                self.msg_box.see(tk.END)
                self.msg_box.configure(state='disabled')

        thread = threading.Thread(target=listener, daemon=True)
        thread.start()

# ==== Run ====
if __name__ == "__main__":
    root = tk.Tk()
    app = CANConfigGUI(root)
    root.mainloop()
