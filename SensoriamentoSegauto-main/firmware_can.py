import can
import time
import sys
import struct
from intelhex import IntelHex

# --- CONFIGURAÇÕES ---
# Ajuste conforme seu hardware e config.h do firmware
VECTOR_APP_NAME = "PythonCAN"  # Nome da aplicação no Vector Hardware Config
VECTOR_CHANNEL = 0             # Índice do canal (0 = Canal 1 virtual ou real)
CAN_BITRATE = 500000           # 500kbps (confirme com seu config.h)

# IDs do Protocolo (Baseado no seu config.h e README.md)
# ATENÇÃO: Se usar Extended ID (EFF), ative o flag is_extended_id
CAN_ID_MCU_TO_REMOTE = 0x1FFFFF01
CAN_ID_REMOTE_TO_MCU = 0x1FFFFF02
IS_EXTENDED = True

# MCU ID (Deve bater com o #define MCU_ID no seu config.h)
# Exemplo: 0x0042
TARGET_MCU_ID = 0x0042 

# Comandos do Protocolo (Baseado no bootloader.h)
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
        # Carrega o hex
        self.ih = IntelHex(hex_file)
        self.max_addr = self.ih.maxaddr()
        
    def connect(self):
        try:
            print(f"🔌 Conectando ao Vector Channel {VECTOR_CHANNEL}...")
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

    def send_message(self, cmd, data_bytes, flash_addr=0):
        """
        Monta e envia o frame CAN conforme especificado no README.md e doc/can-msg-bytes.png
        Byte 0-1: MCU ID
        Byte 2: CMD
        Byte 3: Len & Addr Part (Bits 5-7: Len, Bits 0-4: Addr LSB)
        Byte 4-7: Data Payload
        """
        payload = [0] * 8
        
        # Bytes 0-1: MCU ID (Big Endian)
        payload[0] = (TARGET_MCU_ID >> 8) & 0xFF
        payload[1] = TARGET_MCU_ID & 0xFF
        
        # Byte 2: Command
        payload[2] = cmd
        
        # Byte 3: Len & Address Part
        # "Bits of byte 3 - data length and flash address part"
        data_len = len(data_bytes)
        addr_part = flash_addr & 0x1F # 5 bits LSB do endereço
        payload[3] = (data_len << 5) | addr_part
        
        # Bytes 4-7: Data
        for i in range(data_len):
            if i < 4:
                payload[4 + i] = data_bytes[i]
                
        msg = can.Message(arbitration_id=CAN_ID_REMOTE_TO_MCU,
                          data=payload,
                          is_extended_id=IS_EXTENDED)
        self.bus.send(msg)

    def start_flashing(self):
        print("⏳ Aguardando bootloader (Reinicie o ATmega2560 agora)...")
        
        # Variável para armazenar a assinatura capturada
        device_signature = [0, 0, 0] 
        
        # 1. Espera CMD_BOOTLOADER_START
        start_time = time.time()
        connected = False
        
        while time.time() - start_time < 10: # 10s timeout inicial
            msg = self.wait_for_message(0.1)
            # Verifica se msg existe e se o ID é o esperado (removendo flags de erro/RTR se houver)
            if msg and (msg.arbitration_id & 0x1FFFFFFF) == CAN_ID_MCU_TO_REMOTE:
                
                # Verifica se é para nós (MCU ID Check nos bytes 0 e 1)
                mcu_id_rx = (msg.data[0] << 8) | msg.data[1]
                if mcu_id_rx != TARGET_MCU_ID:
                    continue
                    
                cmd = msg.data[2]
                if cmd == CMD_BOOTLOADER_START:
                    print("🚀 Bootloader detectado!")
                    # CORREÇÃO IMPORTANTE: Captura a assinatura enviada pelo MCU (Bytes 4, 5 e 6)
                    device_signature = list(msg.data[4:7])
                    print(f"ℹ️  Assinatura do MCU: {[hex(b) for b in device_signature]}")
                    connected = True
                    break
        
        if not connected:
            print("❌ Timeout: Bootloader não detectado.")
            return

        # 2. Envia CMD_FLASH_INIT com a assinatura capturada
        # O bootloader compara esses bytes com a assinatura interna do chip. Se não bater, ele ignora.
        print("➡️ Enviando FLASH_INIT...")
        self.send_message(CMD_FLASH_INIT, device_signature) 
        
        # 3. Loop principal de envio de dados
        current_addr = 0
        total_bytes = self.max_addr + 1
        
        print(f"📦 Iniciando flash de {total_bytes} bytes...")
        
        # Reinicia timer para o loop de flash
        last_msg_time = time.time()
        
        while current_addr < total_bytes:
            # Espera READY
            ready = False
            while not ready:
                # Timeout de segurança no loop de espera
                if (time.time() - last_msg_time) > 3.0:
                    print("❌ Timeout fatal esperando resposta do MCU.")
                    return

                msg = self.wait_for_message(0.5)
                if not msg:
                    continue

                if (msg.arbitration_id & 0x1FFFFFFF) == CAN_ID_MCU_TO_REMOTE:
                    cmd = msg.data[2]
                    if cmd == CMD_FLASH_READY:
                        last_msg_time = time.time() # Reset watchdog software
                        
                        # O bootloader diz onde quer gravar nos bytes 4-7
                        req_addr = struct.unpack('>I', bytes(msg.data[4:8]))[0]
                        
                        if req_addr != current_addr:
                             # Em caso de reenvio ou pulo, ajustamos o ponteiro
                             # print(f"⚠️ Ajuste de endereço: {current_addr} -> {req_addr}")
                             current_addr = req_addr
                        ready = True
                    elif cmd == CMD_FLASH_ADDRESS_ERROR:
                        print(f"❌ Erro de endereço reportado pelo MCU: {current_addr}")
                        return
            
            # Pega 4 bytes do Hex (ou menos se acabar)
            chunk = []
            for i in range(4):
                if (current_addr + i) <= self.max_addr:
                    chunk.append(self.ih[current_addr + i])
                else:
                    # Preenche com 0xFF se o arquivo hex tiver buracos ou terminar desalinhado
                    chunk.append(0xFF)
            
            # Se já passamos do total real do arquivo, paramos
            if current_addr > self.max_addr:
                break
                
            # Envia CMD_FLASH_DATA
            self.send_message(CMD_FLASH_DATA, chunk, current_addr)
            
            # Avança endereço
            current_addr += 4
            
            # Barra de progresso
            if current_addr % 256 == 0 or current_addr >= total_bytes:
                percent = min((current_addr / total_bytes) * 100, 100.0)
                print(f"\rProgress: {percent:.1f}% ({current_addr}/{total_bytes})", end='')

        print("\n✅ Flash concluído!")
        
        # 4. Finaliza
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