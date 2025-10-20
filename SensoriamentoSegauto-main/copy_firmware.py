#!/usr/bin/env python3
# copy_firmware.py (versão com controle de versão)

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
            
            # Incrementa patch
            patch += 1
            
            # Se patch >= 100, incrementa minor
            if patch >= 100:
                patch = 0
                minor += 1
            
            # Se minor >= 100, incrementa major
            if minor >= 100:
                minor = 0
                major += 1
    else:
        major, minor, patch = 1, 0, 0
    
    # Salva nova versão
    with open(version_file, 'w') as f:
        json.dump({'major': major, 'minor': minor, 'patch': patch}, f, indent=2)
    
    return f"v{major}.{minor}.{patch}"

def copy_firmware_versioned(source, target, env):
    """Copia firmware com versionamento"""
    firmware_source = str(target[0])
    output_dir = os.path.join(env.subst("$PROJECT_DIR"), "firmwares")
    version_file = os.path.join(output_dir, "version.json")
    
    # Criar pasta se não existir
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Obter próxima versão
    version = get_next_version(version_file)
    
    # Gerar nomes
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    board_name = env.subst("$BOARD")
    
    firmware_name = f"firmware_{version}.hex"
    firmware_dest = os.path.join(output_dir, firmware_name)
    firmware_latest = os.path.join(output_dir, "firmware_latest.hex")
    
    try:
        # Copia com versão
        shutil.copy2(firmware_source, firmware_dest)
        print(f"\n{'='*60}")
        print(f"✓ BUILD COMPLETO - {version}")
        print(f"{'='*60}")
        print(f"Arquivo: {firmware_name}")
        
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

env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", copy_firmware_versioned)