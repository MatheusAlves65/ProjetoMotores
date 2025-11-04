import tkinter as tk
from tkinter import ttk
import subprocess
import os

# --- Mapeamento dos Comandos CAN ---
# Estrutura: {Número_Rele: {'ON': 'comando cansend', 'OFF': 'comando cansend'}}
COMANDOS_CAN = {
    1: {'ON': 'cansend can0 402#7F', 'OFF': 'cansend can0 402#3F'},
    2: {'ON': 'cansend can0 402#DF', 'OFF': 'cansend can0 402#CF'},
    3: {'ON': 'cansend can0 402#F7', 'OFF': 'cansend can0 402#F3'},
    4: {'ON': 'cansend can0 402#FD', 'OFF': 'cansend can0 402#FC'},
    5: {'ON': 'cansend can0 402#FF7F', 'OFF': 'cansend can0 402#FF3F'},
    6: {'ON': 'cansend can0 402#FFDF', 'OFF': 'cansend can0 402#FFCF'},
    7: {'ON': 'cansend can0 402#FFF7', 'OFF': 'cansend can0 402#FFF3'},
    8: {'ON': 'cansend can0 402#FFFD', 'OFF': 'cansend can0 402#FFFC'},
}

# --- Configurações da GUI ---
NUM_RELES = 8
COR_ON = "green"
COR_OFF = "red"

class ControleRelesApp:
    def __init__(self, master):
        self.master = master
        master.title("Controle de Relés CAN")
        
        # Estado inicial dos relés (Todos desligados)
        self.estados = {i: False for i in range(1, NUM_RELES + 1)} 
        # Referências aos "LEDs" (Labels) para poder mudar a cor
        self.leds = {} 
        # Referências aos Textos de status (Labels)
        self.textos_status = {}

        self.criar_widgets()

    def criar_widgets(self):
        """Cria e posiciona os botões, LEDs e status para cada relé."""
        
        # Cabeçalho da Tabela
        ttk.Label(self.master, text="Relé", font=('Arial', 10, 'bold')).grid(row=0, column=0, padx=10, pady=5)
        ttk.Label(self.master, text="Botão ON/OFF", font=('Arial', 10, 'bold')).grid(row=0, column=1, padx=10, pady=5)
        ttk.Label(self.master, text="Status (LED)", font=('Arial', 10, 'bold')).grid(row=0, column=2, padx=10, pady=5)
        ttk.Label(self.master, text="Comando Executado", font=('Arial', 10, 'bold')).grid(row=0, column=3, padx=10, pady=5)

        for i in range(1, NUM_RELES + 1):
            rele_num = i
            linha = i
            
            # 1. Label de Identificação do Relé
            ttk.Label(self.master, text=f"Relé {rele_num}:").grid(row=linha, column=0, padx=10, pady=5, sticky="w")
            
            # 2. Botão ON/OFF
            # O botão irá alternar o estado do relé. Usamos um lambda para passar o número do relé.
            botao = ttk.Button(self.master, text="Alternar", command=lambda r=rele_num: self.alternar_rele(r))
            botao.grid(row=linha, column=1, padx=10, pady=5)
            
            # 3. "LED" (Label com cor de fundo)
            led = tk.Label(self.master, text="  ", background=COR_OFF, relief="raised", width=4)
            led.grid(row=linha, column=2, padx=10, pady=5)
            self.leds[rele_num] = led  # Armazena a referência
            
            # 4. Texto de Status (Ligado/Desligado)
            texto_status = ttk.Label(self.master, text="DESLIGADO")
            texto_status.grid(row=linha, column=3, padx=10, pady=5, sticky="w")
            self.textos_status[rele_num] = texto_status # Armazena a referência

    def alternar_rele(self, rele_num):
        """Alterna o estado do relé, muda a GUI e executa o comando cansend."""
        
        novo_estado = not self.estados[rele_num] # Inverte o estado atual
        self.estados[rele_num] = novo_estado
        
        comando_tipo = 'ON' if novo_estado else 'OFF'
        comando = COMANDOS_CAN[rele_num][comando_tipo]
        
        # Atualiza a GUI
        self.atualizar_gui(rele_num, novo_estado)
        
        # Executa o comando cansend
        self.executar_cansend(comando)

    def atualizar_gui(self, rele_num, estado):
        """Muda a cor do LED e o texto de status."""
        
        cor = COR_ON if estado else COR_OFF
        texto = "LIGADO" if estado else "DESLIGADO"
        
        self.leds[rele_num].config(background=cor)
        self.textos_status[rele_num].config(text=f"{texto}")

    def executar_cansend(self, comando):
        """Executa o comando cansend no sistema."""
        
        # Executa o comando em um shell. O 'shell=True' é necessário para comandos simples como este.
        try:
            # O 'sudo' pode ser necessário, dependendo da sua configuração do CAN/SocketCAN
            # Se for usar sudo, descomente a linha abaixo e comente a linha 'subprocess.run'
            # comando_com_sudo = "sudo " + comando 
            # subprocess.run(comando_com_sudo, shell=True, check=True, text=True) 

            subprocess.run(comando, shell=True, check=True, text=True) 
            print(f"Comando executado com sucesso: {comando}")
            
        except subprocess.CalledProcessError as e:
            # Captura erro se o comando 'cansend' falhar
            print(f"Erro ao executar o comando '{comando}': {e}")
            print("Verifique se o 'cansend' está instalado e se a interface CAN 'can0' está ativa.")
        except FileNotFoundError:
            # Captura erro se o executável 'cansend' não for encontrado
            print(f"Erro: O comando 'cansend' não foi encontrado. Instale o 'can-utils'.")
            
        # Opcional: Você pode querer exibir o comando executado em algum lugar da GUI

# --- Inicialização da Aplicação ---
if __name__ == "__main__":
    # Verifica se estamos em um sistema baseado em Linux (onde cansend é comum)
    if os.name != 'posix':
        print("Aviso: O comando 'cansend' é tipicamente usado em sistemas Linux (com SocketCAN).")
        print("A aplicação GUI irá funcionar, mas os comandos 'cansend' provavelmente falharão em outros sistemas (e.g., Windows).")
        
    root = tk.Tk()
    app = ControleRelesApp(root)
    root.mainloop()