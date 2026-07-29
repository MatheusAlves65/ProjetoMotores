import can
import time
import sys

# --- CONFIGURAÇÕES ---
VECTOR_APP_NAME = "PythonCAN"
VECTOR_CHANNEL = 1
CAN_BITRATE = 1000000  # 500 kbps

def main():
    bus = None
    try:
        print("======================================================")
        print(f"🔌 Iniciando Sniffer no Vector Channel {VECTOR_CHANNEL} @ {CAN_BITRATE/1000} kbps")
        print("======================================================")
        
        # Conecta ao hardware Vector
        bus = can.Bus(interface='vector', 
                      app_name=VECTOR_APP_NAME, 
                      channel=VECTOR_CHANNEL, 
                      bitrate=CAN_BITRATE)
        
        print("✅ Conectado com sucesso! Escutando o barramento...")
        print("Pressione [Ctrl+C] para sair.\n")
        print(" TIMESTAMP   | ID (HEX) | EXT | LEN | DADOS (HEX)")
        print("-------------|----------|-----|-----|-------------------------")

        # Loop infinito de escuta
        while True:
            # Aguarda uma mensagem por até 1 segundo
            msg = bus.recv(timeout=1.0)
            
            if msg is not None:
                # Formata o Timestamp
                timestamp = f"{msg.timestamp:.3f}"
                
                # Formata o ID
                msg_id = f"0x{msg.arbitration_id:08X}" if msg.is_extended_id else f"0x{msg.arbitration_id:03X}     "
                
                # Formata a flag de Extended
                is_ext = "SIM" if msg.is_extended_id else "NAO"
                
                # Formata os Dados
                data_hex = " ".join(f"{byte:02X}" for byte in msg.data)
                
                # Imprime na tela
                print(f" {timestamp:11} | {msg_id} | {is_ext:3} | {msg.dlc:3} | {data_hex}")
                
    except KeyboardInterrupt:
        print("\n🛑 Escuta interrompida pelo usuário.")
    except Exception as e:
        print(f"\n❌ Erro na comunicação com a Vector: {e}")
    finally:
        if bus is not None:
            bus.shutdown()
            print("🔌 Conexão encerrada.")
        sys.exit(0)

if __name__ == "__main__":
    # Força o print a não usar buffer (útil para PlatformIO)
    sys.stdout.reconfigure(line_buffering=True)
    main()