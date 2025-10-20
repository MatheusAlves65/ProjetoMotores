#!/usr/bin/env python3
# teste_reles_gui.py
# Interface gráfica para teste de relés da Placa ARC

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import can
import threading
import time
from datetime import datetime

class PlacaARCGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Teste de Relés - Placa ARC")
        self.root.geometry("900x700")
        self.root.resizable(False, False)
        
        # Estado dos relés (0=OFF, 1=ON)
        self.rele_states = [0] * 8
        
        # Estado da conexão
        self.connected = False
        self.bus = None
        self.monitor_thread = None
        self.monitor_running = False
        
        # Cores
        self.color_on = "#4CAF50"   # Verde
        self.color_off = "#F44336"  # Vermelho
        self.color_bg = "#2C3E50"   # Cinza escuro
        self.color_fg = "#ECF0F1"   # Branco gelo
        
        self.setup_ui()
        
    def setup_ui(self):
        """Configura a interface"""
        # ===================================================================
        # BARRA SUPERIOR - STATUS E CONEXÃO
        # ===================================================================
        top_frame = tk.Frame(self.root, bg=self.color_bg, height=60)
        top_frame.pack(fill=tk.X, padx=10, pady=10)
        top_frame.pack_propagate(False)
        
        # Status de conexão
        self.status_label = tk.Label(
            top_frame, 
            text="● DESCONECTADO", 
            font=("Arial", 14, "bold"),
            bg=self.color_bg,
            fg=self.color_off
        )
        self.status_label.pack(side=tk.LEFT, padx=10)
        
        # Botão conectar/desconectar
        self.btn_connect = tk.Button(
            top_frame,
            text="CONECTAR",
            font=("Arial", 12, "bold"),
            bg="#3498DB",
            fg="white",
            width=15,
            height=2,
            command=self.toggle_connection
        )
        self.btn_connect.pack(side=tk.RIGHT, padx=10)
        
        # ===================================================================
        # FRAME PRINCIPAL - CONTROLE DE RELÉS
        # ===================================================================
        main_frame = tk.Frame(self.root, bg="#ECF0F1")
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Título
        title_label = tk.Label(
            main_frame,
            text="CONTROLE DE RELÉS",
            font=("Arial", 16, "bold"),
            bg="#ECF0F1",
            fg="#2C3E50"
        )
        title_label.pack(pady=10)
        
        # Grid de relés (2 linhas x 4 colunas)
        reles_frame = tk.Frame(main_frame, bg="#ECF0F1")
        reles_frame.pack(pady=10)
        
        self.rele_buttons = []
        self.rele_indicators = []
        
        for i in range(8):
            row = i // 4
            col = i % 4
            
            # Frame individual do relé
            rele_frame = tk.Frame(reles_frame, bg="white", relief=tk.RAISED, bd=2)
            rele_frame.grid(row=row, column=col, padx=10, pady=10)
            
            # Label do relé
            label = tk.Label(
                rele_frame,
                text=f"D{i+1}",
                font=("Arial", 14, "bold"),
                bg="white",
                fg="#2C3E50"
            )
            label.pack(pady=5)
            
            # Indicador visual (círculo)
            canvas = tk.Canvas(rele_frame, width=60, height=60, bg="white", highlightthickness=0)
            canvas.pack(pady=5)
            indicator = canvas.create_oval(10, 10, 50, 50, fill=self.color_off, outline="#34495E", width=2)
            self.rele_indicators.append((canvas, indicator))
            
            # Status text
            status_text = tk.Label(
                rele_frame,
                text="OFF",
                font=("Arial", 10, "bold"),
                bg="white",
                fg=self.color_off
            )
            status_text.pack()
            
            # Botão ON/OFF
            btn = tk.Button(
                rele_frame,
                text="LIGAR",
                font=("Arial", 10, "bold"),
                bg=self.color_on,
                fg="white",
                width=10,
                height=1,
                command=lambda idx=i: self.toggle_rele(idx),
                state=tk.DISABLED
            )
            btn.pack(pady=10, padx=10)
            self.rele_buttons.append((btn, status_text))
        
        # ===================================================================
        # BOTÕES DE CONTROLE GERAL
        # ===================================================================
        control_frame = tk.Frame(main_frame, bg="#ECF0F1")
        control_frame.pack(pady=15)
        
        btn_all_on = tk.Button(
            control_frame,
            text="🔆 LIGAR TODOS",
            font=("Arial", 12, "bold"),
            bg=self.color_on,
            fg="white",
            width=18,
            height=2,
            command=self.ligar_todos,
            state=tk.DISABLED
        )
        btn_all_on.grid(row=0, column=0, padx=10)
        self.btn_all_on = btn_all_on
        
        btn_all_off = tk.Button(
            control_frame,
            text="🔅 DESLIGAR TODOS",
            font=("Arial", 12, "bold"),
            bg=self.color_off,
            fg="white",
            width=18,
            height=2,
            command=self.desligar_todos,
            state=tk.DISABLED
        )
        btn_all_off.grid(row=0, column=1, padx=10)
        self.btn_all_off = btn_all_off
        
        btn_sequencial = tk.Button(
            control_frame,
            text="⚡ TESTE SEQUENCIAL",
            font=("Arial", 12, "bold"),
            bg="#9B59B6",
            fg="white",
            width=18,
            height=2,
            command=self.teste_sequencial,
            state=tk.DISABLED
        )
        btn_sequencial.grid(row=0, column=2, padx=10)
        self.btn_sequencial = btn_sequencial
        
        # ===================================================================
        # LOG / MONITOR
        # ===================================================================
        log_frame = tk.LabelFrame(
            main_frame,
            text="LOG DE EVENTOS",
            font=("Arial", 10, "bold"),
            bg="#ECF0F1",
            fg="#2C3E50"
        )
        log_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        self.log_text = scrolledtext.ScrolledText(
            log_frame,
            height=8,
            font=("Consolas", 9),
            bg="#2C3E50",
            fg="#00FF00",
            insertbackground="white"
        )
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Botão limpar log
        btn_clear_log = tk.Button(
            log_frame,
            text="Limpar Log",
            command=self.clear_log,
            bg="#95A5A6",
            fg="white"
        )
        btn_clear_log.pack(pady=5)
        
        # Log inicial
        self.log("Sistema inicializado. Clique em CONECTAR para começar.")
    
    # =======================================================================
    # FUNÇÕES DE LOG
    # =======================================================================
    
    def log(self, message, level="INFO"):
        """Adiciona mensagem ao log"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        colors = {
            "INFO": "#00FF00",
            "SUCCESS": "#00FF00",
            "ERROR": "#FF0000",
            "WARNING": "#FFA500",
            "CAN": "#00BFFF"
        }
        
        self.log_text.insert(tk.END, f"[{timestamp}] ", "timestamp")
        self.log_text.insert(tk.END, f"{message}\n", level)
        
        # Configurar cores
        self.log_text.tag_config("timestamp", foreground="#FFD700")
        self.log_text.tag_config(level, foreground=colors.get(level, "#FFFFFF"))
        
        self.log_text.see(tk.END)
    
    def clear_log(self):
        """Limpa o log"""
        self.log_text.delete(1.0, tk.END)
    
    # =======================================================================
    # FUNÇÕES DE CONEXÃO CAN
    # =======================================================================
    
    def toggle_connection(self):
        """Conecta ou desconecta do barramento CAN"""
        if not self.connected:
            self.connect_can()
        else:
            self.disconnect_can()
    
    def connect_can(self):
        """Conecta ao barramento CAN"""
        try:
            self.log("Tentando conectar ao CAN0...", "INFO")
            self.bus = can.interface.Bus(
                channel='can0',
                bustype='socketcan',
                bitrate=500000
            )
            test = [0xDD]
            # Testa heartbeat
            self.log("Enviando heartbeat...", "INFO")
            msg = can.Message(
                arbitration_id=0x401,
                is_remote_frame=False,
                is_extended_id=False,
                data = test
                
            )
            self.bus.send(msg)
            
            # Aguarda resposta
            response = self.bus.recv(timeout=2.0)
            
            if response and response.arbitration_id == 0x401:
                self.connected = True
                self.log("✓ Placa ARC conectada com sucesso!", "SUCCESS")
                self.log(f"  ID: 0x{response.arbitration_id:03X}", "SUCCESS")
                
                # Atualiza UI
                self.status_label.config(text="● CONECTADO", fg=self.color_on)
                self.btn_connect.config(text="DESCONECTAR", bg=self.color_off)
                
                # Habilita botões
                for btn, _ in self.rele_buttons:
                    btn.config(state=tk.NORMAL)
                self.btn_all_on.config(state=tk.NORMAL)
                self.btn_all_off.config(state=tk.NORMAL)
                self.btn_sequencial.config(state=tk.NORMAL)
                
                # Inicia monitor
                self.start_monitor()
                
            else:
                raise Exception("Placa não respondeu ao heartbeat")
                
        except Exception as e:
            self.log(f"✗ Erro ao conectar: {e}", "ERROR")
            messagebox.showerror("Erro de Conexão", 
                f"Não foi possível conectar à placa.\n\n"
                f"Verifique:\n"
                f"• CAN0 está ativo? (sudo ip link set can0 up)\n"
                f"• Placa está energizada?\n"
                f"• Cabo CAN conectado?\n\n"
                f"Erro: {e}"
            )
            if self.bus:
                self.bus.shutdown()
                self.bus = None
    
    def disconnect_can(self):
        """Desconecta do barramento CAN"""
        self.log("Desconectando...", "INFO")
        
        # Para monitor
        self.stop_monitor()
        
        # Desliga todos os relés
        self.desligar_todos()
        time.sleep(0.5)
        
        # Fecha barramento
        if self.bus:
            self.bus.shutdown()
            self.bus = None
        
        self.connected = False
        
        # Atualiza UI
        self.status_label.config(text="● DESCONECTADO", fg=self.color_off)
        self.btn_connect.config(text="CONECTAR", bg="#3498DB")
        
        # Desabilita botões
        for btn, _ in self.rele_buttons:
            btn.config(state=tk.DISABLED)
        self.btn_all_on.config(state=tk.DISABLED)
        self.btn_all_off.config(state=tk.DISABLED)
        self.btn_sequencial.config(state=tk.DISABLED)
        
        self.log("✓ Desconectado", "INFO")
    
    # =======================================================================
    # FUNÇÕES DE CONTROLE DOS RELÉS
    # =======================================================================
    
    def toggle_rele(self, rele_idx):
        """Liga/desliga um relé específico"""
        if not self.connected:
            return
        
        # Inverte estado
        new_state = 1 - self.rele_states[rele_idx]
        self.set_rele(rele_idx, new_state)
    
    def set_rele(self, rele_idx, state):
        """Define estado de um relé (0=OFF, 1=ON)"""
        if not self.connected:
            return
        
        try:
            # Prepara mensagem CAN
            data = [0x00] * 8
            
            # Calcula byte e bit
            byte_idx = 0 if rele_idx < 4 else 1
            bit_pos = (rele_idx % 4) * 2
            
            # Define estado (01=ON, 00=OFF)
            if state:
                data[byte_idx] |= (0x01 << bit_pos)
            else:
                data[byte_idx] |= (0x00 << bit_pos)
            
            # Envia mensagem 0x402
            msg = can.Message(
                arbitration_id=0x402,
                data=data,
                is_extended_id=False
            )
            self.bus.send(msg)
            
            # Atualiza estado local
            self.rele_states[rele_idx] = state
            
            # Atualiza UI
            self.update_rele_ui(rele_idx, state)
            
            # Log
            action = "LIGADO" if state else "DESLIGADO"
            self.log(f"D{rele_idx+1} {action}", "SUCCESS")
            
            # Aguarda confirmação (0x422)
            response = self.bus.recv(timeout=0.5)
            if response and response.arbitration_id == 0x422:
                self.log(f"  ✓ Confirmação recebida (0x422)", "CAN")
            
        except Exception as e:
            self.log(f"✗ Erro ao controlar D{rele_idx+1}: {e}", "ERROR")
    
    def update_rele_ui(self, rele_idx, state):
        """Atualiza interface visual do relé"""
        btn, status_label = self.rele_buttons[rele_idx]
        canvas, indicator = self.rele_indicators[rele_idx]
        
        if state:
            # Relé LIGADO
            canvas.itemconfig(indicator, fill=self.color_on)
            status_label.config(text="ON", fg=self.color_on)
            btn.config(text="DESLIGAR", bg=self.color_off)
        else:
            # Relé DESLIGADO
            canvas.itemconfig(indicator, fill=self.color_off)
            status_label.config(text="OFF", fg=self.color_off)
            btn.config(text="LIGAR", bg=self.color_on)
    
    def ligar_todos(self):
        """Liga todos os relés"""
        if not self.connected:
            return
        
        try:
            self.log("Ligando todos os relés...", "INFO")
            
            # Mensagem: todos em 01 (ligado)
            # Byte 0: D1-D4 = 01 01 01 01 = 0x55
            # Byte 1: D5-D8 = 01 01 01 01 = 0x55
            data = [0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
            
            msg = can.Message(
                arbitration_id=0x402,
                data=data,
                is_extended_id=False
            )
            self.bus.send(msg)
            
            # Atualiza estados
            for i in range(8):
                self.rele_states[i] = 1
                self.update_rele_ui(i, 1)
            
            self.log("✓ Todos os relés LIGADOS", "SUCCESS")
            
        except Exception as e:
            self.log(f"✗ Erro: {e}", "ERROR")
    
    def desligar_todos(self):
        """Desliga todos os relés"""
        if not self.connected:
            return
        
        try:
            self.log("Desligando todos os relés...", "INFO")
            
            # Mensagem: todos em 00 (desligado)
            data = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
            
            msg = can.Message(
                arbitration_id=0x402,
                data=data,
                is_extended_id=False
            )
            self.bus.send(msg)
            
            # Atualiza estados
            for i in range(8):
                self.rele_states[i] = 0
                self.update_rele_ui(i, 0)
            
            self.log("✓ Todos os relés DESLIGADOS", "SUCCESS")
            
        except Exception as e:
            self.log(f"✗ Erro: {e}", "ERROR")
    
    def teste_sequencial(self):
        """Teste sequencial de todos os relés"""
        if not self.connected:
            return
        
        # Desabilita botões durante teste
        self.btn_sequencial.config(state=tk.DISABLED, text="TESTE EM ANDAMENTO...")
        
        def run_test():
            try:
                self.log("═══ INICIANDO TESTE SEQUENCIAL ═══", "WARNING")
                
                for i in range(8):
                    self.log(f"Testando D{i+1}...", "INFO")
                    
                    # Liga
                    self.set_rele(i, 1)
                    time.sleep(0.5)
                    
                    # Desliga
                    self.set_rele(i, 0)
                    time.sleep(0.3)
                
                self.log("═══ TESTE CONCLUÍDO ═══", "SUCCESS")
                
            except Exception as e:
                self.log(f"✗ Erro no teste: {e}", "ERROR")
            
            finally:
                # Reabilita botão
                self.btn_sequencial.config(state=tk.NORMAL, text="⚡ TESTE SEQUENCIAL")
        
        # Executa em thread separada
        threading.Thread(target=run_test, daemon=True).start()
    
    # =======================================================================
    # MONITOR CAN
    # =======================================================================
    
    def start_monitor(self):
        """Inicia thread de monitoramento CAN"""
        self.monitor_running = True
        self.monitor_thread = threading.Thread(target=self.monitor_can, daemon=True)
        self.monitor_thread.start()
        self.log("Monitor CAN iniciado", "INFO")
    
    def stop_monitor(self):
        """Para thread de monitoramento"""
        self.monitor_running = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=1.0)
        self.log("Monitor CAN parado", "INFO")
    
    def monitor_can(self):
        """Thread que monitora mensagens CAN"""
        while self.monitor_running and self.bus:
            try:
                msg = self.bus.recv(timeout=0.1)
                if msg:
                    # Formata dados
                    data_str = ' '.join(f'{b:02X}' for b in msg.data)
                    
                    # Log apenas mensagens relevantes
                    if msg.arbitration_id in [0x422, 0x423, 0x424, 0x425, 0x426]:
                        self.log(
                            f"RX: 0x{msg.arbitration_id:03X} [{msg.dlc}] {data_str}",
                            "CAN"
                        )
            except Exception:
                pass
    
    # =======================================================================
    # CLEANUP
    # =======================================================================
    
    def on_closing(self):
        """Chamado ao fechar a janela"""
        if self.connected:
            if messagebox.askokcancel("Sair", "Desconectar e sair?"):
                self.disconnect_can()
                self.root.destroy()
        else:
            self.root.destroy()

# ===========================================================================
# MAIN
# ===========================================================================

def main():
    root = tk.Tk()
    app = PlacaARCGUI(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()

if __name__ == "__main__":
    main()
