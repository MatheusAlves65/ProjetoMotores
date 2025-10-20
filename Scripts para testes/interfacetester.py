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

@dataclass
class SafetyConfigStructure:
    Monit_Enable: int = 0
    maxtemp: float = 40.0
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
    return tx_buf

def send_safety_config(config: SafetyConfigStructure) -> bytearray:
    tx_buf = bytearray(4)
    tx_buf[0] = 0x12
    tx_buf[1] = 0x30 | (config.Monit_Enable & 0x03)
    tx_buf[2] = int((config.maxtemp * 255) / 120)
    tx_buf[3] = int((config.timer * 255) / 10000)
    return tx_buf

def read_digital(buf: bytes) -> list[int]:
    return [
        (buf[0] >> 6) & 0x03,
        (buf[0] >> 4) & 0x03,
        (buf[0] >> 2) & 0x03,
        buf[0] & 0x03,
        (buf[1] >> 6) & 0x03,
        (buf[1] >> 4) & 0x03,
        (buf[1] >> 2) & 0x03,
        buf[1] & 0x03
    ]

def readPWMEnc(buf: bytes, index: int) -> int:
    value = buf[index]
    if value <= 250:
        return (value * 255) // 250
    elif value == 255:
        return -2
    elif value >= 251:
        return -1
    else:
        return 255

def aquisition_config(buf: bytes) -> AquisitionConfigStructure:
    timer = (buf[0] << 8) | buf[1]
    analog = bool(buf[2] & 0x01)
    enable = buf[3] & 0x03
    return AquisitionConfigStructure(
        Aquics_Enable_Continuous=buf[3] & 0x03,
        Aquics_Enable=enable,
        analog=analog,
        timer=timer
    )

def safety_config(buf: bytes) -> SafetyConfigStructure:
    check_id = (buf[0] << 4) | (buf[1] >> 4)
    monit = buf[1] & 0x03
    maxtemp = (buf[2] * 120) / 255
    timer = (buf[3] * 10000) // 255
    return SafetyConfigStructure(Monit_Enable=monit, maxtemp=maxtemp, timer=timer)


# ==== GUI App ====
class CANConfigGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("SegurAuto Motor Config")
        self.root.geometry("1000x600")

        self.timer_var = tk.IntVar(value=1000)
        self.analog_var = tk.BooleanVar(value=False)
        self.enable_continuous_var = tk.IntVar(value=0)
        self.pwm1_var = tk.IntVar(value=0)
        self.pwm2_var = tk.IntVar(value=0)
        self.enc_var = tk.IntVar(value=0)
        self.led_vars = [tk.IntVar(value=0) for _ in range(8)]

        # Safety Config 1 and 2 variables
        self.monit_enable_var1 = tk.IntVar(value=0)
        self.safety_maxtemp_var1 = tk.DoubleVar(value=40.0)
        self.safety_timer_var1 = tk.IntVar(value=1000)

        self.monit_enable_var2 = tk.IntVar(value=0)
        self.safety_maxtemp_var2 = tk.DoubleVar(value=40.0)
        self.safety_timer_var2 = tk.IntVar(value=1000)

        self.enable_once_state = tk.BooleanVar(value=False)
        self.save_to_eeprom1 = tk.BooleanVar(value=False)
        self.save_to_eeprom2 = tk.BooleanVar(value=False)

        self.build_gui()
        self.start_can_listener()

    def build_gui(self):
        frame = ttk.Frame(self.root, padding=10)
        frame.grid(row=0, column=0)

        ttk.Label(frame, text="Timer (ms):").grid(row=0, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.timer_var).grid(row=0, column=1)

        ttk.Checkbutton(frame, text="Analog Input", variable=self.analog_var).grid(row=1, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(frame, text="Enable Continuous", variable=self.enable_continuous_var).grid(row=2, column=0, columnspan=2, sticky="w")

        ttk.Checkbutton(frame, text="Enable Once State", variable=self.enable_once_state).grid(row=3, column=0, columnspan=2, sticky="w")
        ttk.Button(frame, text="Enviar Enable Once", command=self.send_enable_once).grid(row=4, column=0, columnspan=2, pady=5)

        ttk.Button(frame, text="Enviar Config Aquisição", command=self.send_can_config).grid(row=5, column=0, columnspan=2, pady=10)

        ttk.Label(frame, text="PWM1:").grid(row=6, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.pwm1_var).grid(row=6, column=1)
        ttk.Label(frame, text="PWM2:").grid(row=7, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.pwm2_var).grid(row=7, column=1)
        ttk.Label(frame, text="Encoder:").grid(row=8, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.enc_var).grid(row=8, column=1)

        ttk.Label(frame, text="LEDs:").grid(row=9, column=0, sticky="w")
        led_frame = ttk.Frame(frame)
        led_frame.grid(row=9, column=1, sticky="w")
        for i in range(8):
            ttk.Checkbutton(led_frame, text=str(i+1), variable=self.led_vars[i]).grid(row=0, column=i)

        ttk.Button(frame, text="Enviar LEDs + PWM", command=self.send_digital_cmd).grid(row=10, column=0, columnspan=2, pady=10)

        # Safety Config Frames
        safety_frame1 = ttk.LabelFrame(frame, text="Safety Config 1", padding=10)
        safety_frame1.grid(row=11, column=0, padx=5, pady=5, sticky="nsew")
        ttk.Label(safety_frame1, text="Monit_Enable:").grid(row=0, column=0)
        ttk.Entry(safety_frame1, textvariable=self.monit_enable_var1).grid(row=0, column=1)
        ttk.Label(safety_frame1, text="Max Temp (°C):").grid(row=1, column=0)
        ttk.Entry(safety_frame1, textvariable=self.safety_maxtemp_var1).grid(row=1, column=1)
        ttk.Label(safety_frame1, text="Timer (ms):").grid(row=2, column=0)
        ttk.Entry(safety_frame1, textvariable=self.safety_timer_var1).grid(row=2, column=1)
        ttk.Checkbutton(safety_frame1, text="Save to EEPROM 1", variable=self.save_to_eeprom1).grid(row=3, column=0, columnspan=2, sticky="w")

        safety_frame2 = ttk.LabelFrame(frame, text="Safety Config 2", padding=10)
        safety_frame2.grid(row=11, column=1, padx=5, pady=5, sticky="nsew")
        ttk.Label(safety_frame2, text="Monit_Enable:").grid(row=0, column=0)
        ttk.Entry(safety_frame2, textvariable=self.monit_enable_var2).grid(row=0, column=1)
        ttk.Label(safety_frame2, text="Max Temp (°C):").grid(row=1, column=0)
        ttk.Entry(safety_frame2, textvariable=self.safety_maxtemp_var2).grid(row=1, column=1)
        ttk.Label(safety_frame2, text="Timer (ms):").grid(row=2, column=0)
        ttk.Entry(safety_frame2, textvariable=self.safety_timer_var2).grid(row=2, column=1)
        ttk.Checkbutton(safety_frame2, text="Save to EEPROM 2", variable=self.save_to_eeprom2).grid(row=3, column=0, columnspan=2, sticky="w")

        ttk.Button(frame, text="Enviar Safety Config (1+2)", command=self.send_safety_config_cmd).grid(row=12, column=0, columnspan=2, pady=10)

        self.status = ttk.Label(frame, text="Status: Aguarde")
        self.status.grid(row=13, column=0, columnspan=2)

        self.msg_box = tk.Text(frame, height=10, width=80)
        self.msg_box.grid(row=14, column=0, columnspan=2, pady=5)
        self.msg_box.insert(tk.END, "Mensagens CAN recebidas:\n")
        self.msg_box.configure(state='disabled')

    def send_can_config(self):
        config = AquisitionConfigStructure(
            Aquics_Enable_Continuous=self.enable_continuous_var.get(),
            Aquics_Enable=0,
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

    def send_enable_once(self):
        value = 0x40 if self.enable_once_state.get() else 0x00
        data = bytearray([value])
        try:
            bus = can.interface.Bus(channel='can0', bustype='socketcan')
            msg = can.Message(arbitration_id=0x405, data=data, is_extended_id=False)
            bus.send(msg)
            self.status.config(text=f"Status: Enable Once enviado ({hex(value)})", foreground="green")
        except can.CanError as e:
            self.status.config(text=f"Erro ao enviar Enable Once: {e}", foreground="red")

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

    def send_safety_config_cmd(self):
        cfg1 = SafetyConfigStructure(
            Monit_Enable=self.monit_enable_var1.get(),
            maxtemp=self.safety_maxtemp_var1.get(),
            timer=self.safety_timer_var1.get()
        )
        cfg2 = SafetyConfigStructure(
            Monit_Enable=self.monit_enable_var2.get(),
            maxtemp=self.safety_maxtemp_var2.get(),
            timer=self.safety_timer_var2.get()
        )
        data = bytearray(8)
        data[0:4] = send_safety_config(cfg1)
        data[4:8] = send_safety_config(cfg2)

        # Adjust internal ID if EEPROM save selected
        if self.save_to_eeprom1.get():
            data[0] = 0x12
            data[1] = (cfg1.Monit_Enable & 0x03)
        if self.save_to_eeprom2.get():
            data[4] = 0x12
            data[5] = (cfg2.Monit_Enable & 0x03)

        try:
            bus = can.interface.Bus(channel='can0', bustype='socketcan')
            msg = can.Message(arbitration_id=0x403, data=data, is_extended_id=False)
            bus.send(msg)
            self.status.config(text="Status: Safety Config 1+2 enviada", foreground="green")
        except can.CanError as e:
            self.status.config(text=f"Erro ao enviar: {e}", foreground="red")

    def start_can_listener(self):
        def listener():
            bus = can.interface.Bus(channel='can0', bustype='socketcan')
            for msg in bus:
                self.msg_box.configure(state='normal')
                self.msg_box.insert(tk.END, f"ID: {hex(msg.arbitration_id)} | Data: {msg.data.hex()}\n")
                self.msg_box.see(tk.END)
                try:
                    parsed = ""
                    if msg.arbitration_id == 0x424:
                        cfg = aquisition_config(msg.data)
                        parsed = f"Aquisição → Timer: {cfg.timer}ms, Analog: {cfg.analog}, Enable: {cfg.Aquics_Enable}, Continuous: {cfg.Aquics_Enable_Continuous}"
                    elif msg.arbitration_id == 0x423:
                        cfg1 = safety_config(msg.data[:4])
                        cfg2 = safety_config(msg.data[4:])
                        parsed = (
                            f"Safety1 → Enable: {cfg1.Monit_Enable}, MaxTemp: {cfg1.maxtemp:.1f}°C, Timer: {cfg1.timer}ms\n"
                            f"Safety2 → Enable: {cfg2.Monit_Enable}, MaxTemp: {cfg2.maxtemp:.1f}°C, Timer: {cfg2.timer}ms"
                        )
                    elif msg.arbitration_id == 0x422:
                        digital = read_digital(msg.data[:2])
                        pwm1 = readPWMEnc(msg.data, 2)
                        pwm2 = readPWMEnc(msg.data, 3)
                        enc = readPWMEnc(msg.data, 4)
                        parsed = f"Digital: {digital}, PWM1: {pwm1}, PWM2: {pwm2}, Encoder: {enc}"
                    elif msg.arbitration_id == 0x425:
                        state = "Ativado" if msg.data[0] & 0x40 else "Desativado"
                        parsed = f"Enable Once Estado → {state}"
                    else:
                        parsed = msg.data.hex()
                    self.msg_box.insert(tk.END, f"ID: {hex(msg.arbitration_id)} | {parsed}\n")
                except Exception as e:
                    self.msg_box.insert(tk.END, f"Erro ao interpretar {hex(msg.arbitration_id)}: {e}\n")
                self.msg_box.see(tk.END)
                self.msg_box.configure(state='disabled')

        thread = threading.Thread(target=listener, daemon=True)
        thread.start()

# ==== Run ====
if __name__ == "__main__":
    root = tk.Tk()
    app = CANConfigGUI(root)
    root.mainloop()
