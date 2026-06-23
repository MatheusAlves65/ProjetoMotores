import can
import time
import sys
import struct
from intelhex import IntelHex

# --- CONFIGURAÇÕES ---
VECTOR_APP_NAME = "PythonCAN"
VECTOR_CHANNEL = 0
# [AJUSTE] Velocidade definida para 500kbps
CAN_BITRATE = 500000           

# IDs do Protocolo
CAN_ID_MCU_TO_REMOTE = 0x1FFFFF01
CAN_ID_REMOTE_TO_MCU = 0x1FFFFF02
IS_EXTENDED = True

# MCU ID e ID de RESET
TARGET_MCU_ID = 0x0042 
# ID que faz o Arduino reiniciar (conforme configuramos no loop do Arduino)
CAN_ID_RESET_TRIGGER = 0x0042 

# Comandos do Bootloader
CMD_BOOTLOADER_START    = 0x02
CMD_FLASH_INIT          = 0x06
CMD_FLASH_READY         = 0x04
CMD_FLASH_DATA          = 0x08
CMD_FLASH_DONE          = 0x10
CMD_START_APP           = 0x80
CMD_FLASH_ADDRESS_ERROR = 0x0B
CMD_FLASH_DATA_ERROR    = 0x0D

class VectorFlasher:
    def __init__(self, hex_file):
        self.hex_file = hex_file
        self.bus = None
        self.ih = IntelHex(hex_file)
        self.max_addr = self.ih.maxaddr()
        
    def connect(self):
        try:
            print(f"🔌 Conectando ao Vector Channel {VECTOR_CHANNEL} @ {CAN_BITRATE/1000} kbps...")
            self.bus = can.Bus(interface='vector', 
                               app_name=VECTOR_APP_NAME, 
                               channel=VECTOR_CHANNEL, 
                               bitrate=CAN_BITRATE)
            print("✅ Conectado ao hardware Vector.")
        except Exception as e:
            print(f"❌ Erro ao conectar no Vector: {e}")
            sys.exit(1)

    def wait_for_message(self, timeout=1.0):
        return self.bus.recv(timeout)

    # Função para enviar o comando de reset
    def send_reset_command(self):
        print(f"🔄 Enviando comando de RESET (ID: 0x{CAN_ID_RESET_TRIGGER:X})...")
        # Envia Standard ID 0x42 com payload dummy (0xAA)
        msg = can.Message(arbitration_id=CAN_ID_RESET_TRIGGER,
                          data=[0xAA], 
                          is_extended_id=False) 
        self.bus.send(msg)
        time.sleep(0.2) # Tempo para o Arduino reiniciar

    def send_message(self, cmd, data_bytes, flash_addr=0):
        payload = [0] * 8
        payload[0] = (TARGET_MCU_ID >> 8) & 0xFF
        payload[1] = TARGET_MCU_ID & 0xFF
        payload[2] = cmd
        
        data_len = len(data_bytes)
        addr_part = flash_addr & 0x1F 
        payload[3] = (data_len << 5) | addr_part
        
        for i in range(data_len):
            if i < 4:
                payload[4 + i] = data_bytes[i]
                
        msg = can.Message(arbitration_id=CAN_ID_REMOTE_TO_MCU,
                          data=payload,
                          is_extended_id=IS_EXTENDED)
        self.bus.send(msg)

    def start_flashing(self):
        # Chama o reset automático
        self.send_reset_command()
        
        print("⏳ Aguardando bootloader iniciar...")
        
        device_signature = [0, 0, 0] 
        start_time = time.time()
        connected = False
        
        # Loop para pegar o 'Hello' do Bootloader
        while time.time() - start_time < 5: 
            msg = self.wait_for_message(0.1)
            
            if msg and (msg.arbitration_id & 0x1FFFFFFF) == CAN_ID_MCU_TO_REMOTE:
                mcu_id_rx = (msg.data[0] << 8) | msg.data[1]
                if mcu_id_rx != TARGET_MCU_ID:
                    continue
                    
                cmd = msg.data[2]
                if cmd == CMD_BOOTLOADER_START:
                    print("🚀 Bootloader detectado!")
                    device_signature = list(msg.data[4:7])
                    print(f"ℹ️  Assinatura do MCU: {[hex(b) for b in device_signature]}")
                    connected = True
                    break
        
        if not connected:
            print("❌ Timeout: O Bootloader não respondeu após o reset.")
            return

        print("➡️ Enviando FLASH_INIT...")
        self.send_message(CMD_FLASH_INIT, device_signature) 
        
        current_addr = 0
        total_bytes = self.max_addr + 1
        print(f"📦 Iniciando flash de {total_bytes} bytes...")
        
        last_msg_time = time.time()
        
        while current_addr < total_bytes:
            ready = False
            while not ready:
                if (time.time() - last_msg_time) > 3.0:
                    print("❌ Timeout fatal esperando resposta do MCU.")
                    return

                msg = self.wait_for_message(0.5)
                if not msg: continue

                if (msg.arbitration_id & 0x1FFFFFFF) == CAN_ID_MCU_TO_REMOTE:
                    cmd = msg.data[2]
                    if cmd == CMD_FLASH_READY:
                        last_msg_time = time.time()
                        req_addr = struct.unpack('>I', bytes(msg.data[4:8]))[0]
                        if req_addr != current_addr:
                            current_addr = req_addr
                        ready = True
                    elif cmd == CMD_FLASH_ADDRESS_ERROR:
                        print(f"❌ Erro de endereço reportado pelo MCU: {current_addr}")
                        return
            
            chunk = []
            for i in range(4):
                if (current_addr + i) <= self.max_addr:
                    chunk.append(self.ih[current_addr + i])
                else:
                    chunk.append(0xFF)
            
            if current_addr > self.max_addr:
                break
                
            self.send_message(CMD_FLASH_DATA, chunk, current_addr)
            current_addr += 4
            
            if current_addr % 256 == 0 or current_addr >= total_bytes:
                percent = min((current_addr / total_bytes) * 100, 100.0)
                print(f"\rProgress: {percent:.1f}% ({current_addr}/{total_bytes})", end='',flush=True)

        print("\n✅ Flash concluído!")
        print("➡️ Enviando FLASH_DONE...")
        self.send_message(CMD_FLASH_DONE, [])
        time.sleep(1)
        print("🎉 Processo finalizado.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python flash_vector.py <arquivo_firmware.hex>")
    else:
        flasher = VectorFlasher(sys.argv[1])
        flasher.connect()
        flasher.start_flashing()