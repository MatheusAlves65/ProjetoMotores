#!/usr/bin/env python3
import json
import shutil
from datetime import datetime
from pathlib import Path
from SCons.Script import Import

Import("env")

# --- CONFIGURAÇÕES ---
OUTPUT_DIR_NAME = "firmwares"
VERSION_FILE_NAME = "version.json"
# Defina se quer incrementar a versão a cada build (True) ou não (False)
AUTO_INCREMENT = True 

class FirmwareVersion:
    def __init__(self, file_path):
        self.file_path = Path(file_path)
        self.major = 1
        self.minor = 0
        self.patch = 0
        self.load()

    def load(self):
        if self.file_path.exists():
            try:
                with open(self.file_path, 'r') as f:
                    data = json.load(f)
                    self.major = data.get('major', 1)
                    self.minor = data.get('minor', 0)
                    self.patch = data.get('patch', 0)
            except (json.JSONDecodeError, ValueError):
                print("⚠️  Arquivo de versão corrompido. Reiniciando para 1.0.0")

    def save(self):
        # Cria o diretório pai se não existir
        self.file_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.file_path, 'w') as f:
            json.dump({
                'major': self.major, 
                'minor': self.minor, 
                'patch': self.patch
            }, f, indent=4)

    def increment_patch(self):
        self.patch += 1
        # Removemos a lógica de resetar no 100 para seguir o padrão SemVer.
        # Se quiser mudar Major ou Minor, edite o version.json manualmente.
        self.save()

    def __str__(self):
        return f"v{self.major}.{self.minor}.{self.patch}"

def clean_old_files(output_dir):
    """Remove arquivos .bin, .hex e .txt antigos, mantendo version.json"""
    if not output_dir.exists():
        return

    # Padrões de arquivos para remover
    patterns = ["firmware_v*.bin", "firmware_v*.hex", "info_v*.txt"]
    
    deleted_count = 0
    for pattern in patterns:
        for file in output_dir.glob(pattern):
            try:
                file.unlink()
                deleted_count += 1
            except Exception as e:
                print(f"⚠️  Erro ao deletar {file.name}: {e}")
    
    if deleted_count > 0:
        print(f"🗑️  Limpeza: {deleted_count} arquivos antigos removidos.")

def generate_info_file(file_path, version, board_name, size_kb):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    content = (
        f"Firmware Info\n"
        f"-------------\n"
        f"Versão:  {version}\n"
        f"Build:   {timestamp}\n"
        f"Placa:   {board_name}\n"
        f"Tamanho: {size_kb:.2f} KB\n"
    )
    with open(file_path, 'w') as f:
        f.write(content)

def finalize_firmware(source, target, env):
    """Função principal executada pelo PlatformIO"""
    
    # 1. Definição de Caminhos
    project_dir = Path(env.subst("$PROJECT_DIR"))
    output_dir = project_dir / OUTPUT_DIR_NAME
    version_file = output_dir / VERSION_FILE_NAME
    
    source_firmware = Path(str(target[0]))
    extension = source_firmware.suffix # .bin ou .hex
    
    # 2. Gerenciamento de Versão
    fw_ver = FirmwareVersion(version_file)
    if AUTO_INCREMENT:
        fw_ver.increment_patch()
    
    version_str = str(fw_ver)
    
    # 3. Limpeza
    clean_old_files(output_dir)
    
    # 4. Definição de Nomes de Destino
    new_fw_name = f"firmware_{version_str}{extension}"
    target_fw = output_dir / new_fw_name
    latest_fw = output_dir / f"firmware_latest{extension}"
    info_txt = output_dir / f"info_{version_str}.txt"
    
    # 5. Operações de Cópia e Geração
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
        
        # Copia versão numerada
        shutil.copy2(source_firmware, target_fw)
        # Copia versão 'latest'
        shutil.copy2(source_firmware, latest_fw)
        
        # Calcula tamanho
        size_kb = target_fw.stat().st_size / 1024
        
        # Gera TXT
        board = env.subst("$BOARD")
        generate_info_file(info_txt, version_str, board, size_kb)
        
        # 6. Feedback Visual
        print("\n" + "="*50)
        print(f"✅ SUCESSO! Nova versão gerada: {version_str}")
        print(f"📂 Destino: {output_dir}")
        print(f"📄 Arquivo: {new_fw_name} ({size_kb:.2f} KB)")
        print("="*50 + "\n")
        
    except Exception as e:
        print(f"\n❌ ERRO CRÍTICO ao mover firmware: {e}\n")

# Registra o script no PlatformIO
env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", finalize_firmware)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", finalize_firmware)