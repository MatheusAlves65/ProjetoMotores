#!/usr/bin/env python3
# copy_firmware.py

Import("env")
import os
import shutil
import json
from datetime import datetime

def get_next_version(version_file):
    """Lê e incrementa a versão"""
    if os.path.exists(version_file):
        with open(version_file, 'r') as f:
            data = json.load(f)
            major = data.get('major', 1)
            minor = data.get('minor', 0)
            patch = data.get('patch', 0)
            
            patch += 1
            if patch >= 100:
                patch = 0
                minor += 1
            if minor >= 100:
                minor = 0
                major += 1
    else:
        major, minor, patch = 1, 0, 0
    
    with open(version_file, 'w') as f:
        json.dump({'major': major, 'minor': minor, 'patch': patch}, f, indent=2)
    
    return f"v{major}.{minor}.{patch}"

def copy_firmware_versioned(source, target, env):
    """Limpa anteriores e copia novo firmware com versionamento"""
    
    # Pega o caminho do arquivo gerado e detecta a extensão (.bin ou .hex)
    firmware_source = str(target[0])
    file_extension = os.path.splitext(firmware_source)[1]
    
    output_dir = os.path.join(env.subst("$PROJECT_DIR"), "firmwares")
    version_file = os.path.join(output_dir, "version.json")
    
    # Criar pasta se não existir
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # --- LIMPEZA DE ARQUIVOS ANTIGOS ---
    try:
        # Lista todos os arquivos na pasta
        for filename in os.listdir(output_dir):
            file_path = os.path.join(output_dir, filename)
            
            # Deleta se começar com 'firmware_' ou 'info_'
            # O 'version.json' será IGNORADO e preservado
            if filename.startswith("firmware_") or filename.startswith("info_"):
                if os.path.isfile(file_path):
                    os.remove(file_path)
                    print(f"🗑️  Removido antigo: {filename}")
    except Exception as e:
        print(f"⚠️ Aviso: Não foi possível limpar arquivos antigos: {e}")
    # -----------------------------------
    
    # Obter próxima versão
    version = get_next_version(version_file)
    
    # Gerar nomes
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    board_name = env.subst("$BOARD")
    
    # Usa a extensão detectada (.bin ou .hex)
    firmware_name = f"firmware_{version}{file_extension}"
    firmware_dest = os.path.join(output_dir, firmware_name)
    firmware_latest = os.path.join(output_dir, f"firmware_latest{file_extension}")
    
    try:
        # Copia com versão
        shutil.copy2(firmware_source, firmware_dest)
        print(f"\n{'='*60}")
        print(f"✓ BUILD COMPLETO - {version}")
        print(f"{'='*60}")
        print(f"Arquivo gerado: {firmware_name}")
        
        # Copia versão "latest"
        shutil.copy2(firmware_source, firmware_latest)
        
        # Mostra informações
        size_kb = os.path.getsize(firmware_dest) / 1024
        print(f"Tamanho: {size_kb:.2f} KB")
        print(f"Localização: {output_dir}")
        print(f"{'='*60}\n")
        
        # Cria arquivo de info
        info_file = os.path.join(output_dir, f"info_{version}.txt")
        with open(info_file, 'w') as f:
            f.write(f"Versão: {version}\n")
            f.write(f"Data: {timestamp}\n")
            f.write(f"Board: {board_name}\n")
            f.write(f"Tamanho: {size_kb:.2f} KB\n")
        
    except Exception as e:
        print(f"✗ Erro ao copiar firmware: {e}")

# Registra a ação para .hex e .bin (cobre Arduino e ESP32/STM32)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", copy_firmware_versioned)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_firmware_versioned)