# Upload do Bootloader MCP-CAN-Boot via Arduino Nano ISP - ATmega2560

## Hardware Necessário
- **Arduino Nano** (como programmer ISP)
- **ATmega2560** ou Arduino Mega (target)
- **Capacitor 10µF** (obrigatório!)
- **6 jumpers** para conexões ISP
- **Cabo USB** para o Nano
- **MCP2515** (para depois do bootloader)

## Passo 1: Preparar Arduino Nano como ISP

### 1.1 Carregar ArduinoISP no Nano
```
1. Conecte Arduino Nano ao PC via USB
2. Abra Arduino IDE
3. Vá em File → Examples → 11.ArduinoISP → ArduinoISP
4. Selecione Board: "Arduino Nano"
5. Selecione Processor: "ATmega328P (Old Bootloader)" ou "ATmega328P"
6. Selecione Port: sua porta COM/ttyUSB
7. Upload o sketch ArduinoISP
8. Aguarde "Done uploading"
```

### 1.2 Conexões Arduino Nano → ATmega2560

#### 🔧 Diagrama de Conexão
```
ARDUINO NANO (Programmer) → ATMEGA2560/ARDUINO MEGA (Target)

Nano Pin    Mega Pin    Função      Cor Sugerida
D13      ←→ D52         SCK         🟡 Amarelo
D12      ←→ D50         MISO        🟢 Verde  
D11      ←→ D51         MOSI        🔵 Azul
D10      ←→ RESET       RESET       ⚪ Branco
5V       ←→ 5V          VCC         🔴 Vermelho
GND      ←→ GND         GND         ⚫ Preto

🔧 CAPACITOR 10µF: + no RESET do Nano, - no GND do Nano
   (OBRIGATÓRIO - evita reset do Nano durante programação)
```

### 1.3 Instalar Ferramentas
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install avrdude gcc-avr avr-libc

# Windows - Arduino IDE já inclui avrdude
# Localizar em: C:\Users\[user]\AppData\Local\Arduino15\packages\arduino\tools\avrdude\

# MacOS
brew install avrdude
```

### 1.4 Testar Arduino Nano ISP
```bash
# Linux/Mac
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -v

# Windows (ajustar COM port)
avrdude -c arduino -P COM3 -b19200 -p m2560 -v

# Deve retornar algo como:
# Device signature = 0x1e9801 (ATmega2560)
```

## Passo 2: Compilar o Bootloader

### Usando PlatformIO (Recomendado)
```bash
# Clonar repositório
git clone https://github.com/crycode-de/mcp-can-boot.git
cd mcp-can-boot

# Instalar PlatformIO se não tiver
pip install platformio

# Compilar para ATmega2560
pio run -e ATmega2560

# Bootloader compilado estará em:
# .pio/build/ATmega2560/firmware.hex
```

### Verificar Arquivo .hex
```bash
# Verificar se existe
ls -la .pio/build/ATmega2560/firmware.hex

# Ver tamanho do arquivo
avr-size .pio/build/ATmega2560/firmware.hex
```

## Passo 3: Configurar Fuses (CRÍTICO! ⚠️)

### Fuses Corretos para ATmega2560 + Bootloader
```bash
# LFUSE: 0xFF - External Crystal 16MHz, fast startup
# HFUSE: 0xDA - Boot Reset Vector + Bootloader Size 2048 words  
# EFUSE: 0xFC - Brown-out detection 2.7V
```

### ⚠️ BACKUP DOS FUSES ORIGINAIS PRIMEIRO!
```bash
# Linux/Mac
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -U lfuse:r:lfuse_backup.hex:h
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -U hfuse:r:hfuse_backup.hex:h  
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -U efuse:r:efuse_backup.hex:h

# Windows (ajustar porta COM)
avrdude -c arduino -P COM3 -b19200 -p m2560 -U lfuse:r:lfuse_backup.hex:h
avrdude -c arduino -P COM3 -b19200 -p m2560 -U hfuse:r:hfuse_backup.hex:h
avrdude -c arduino -P COM3 -b19200 -p m2560 -U efuse:r:efuse_backup.hex:h
```

### Entendendo HFUSE = 0xDA (Mais Importante)
```
Bit 7: RSTDISBL = 1 (Reset habilitado)
Bit 6: WDTON = 1    (Watchdog por software)
Bit 5: SPIEN = 0    (SPI programming habilitado)
Bit 4: JTAGEN = 1   (JTAG habilitado)
Bit 3: EESAVE = 1   (EEPROM não apagada em chip erase)
Bit 2-1: BOOTSZ = 01 (2048 words bootloader) ← CRÍTICO!
Bit 0: BOOTRST = 0  (Boot reset vector habilitado) ← CRÍTICO!
```

## Passo 4: Programar Fuses

### ⚠️ AVISO CRÍTICO
**Programar fuses errados pode "bricar" o microcontrolador!**

### Comando de Programação
```bash
# Linux/Mac
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 \
  -U lfuse:w:0xFF:m \
  -U hfuse:w:0xDA:m \
  -U efuse:w:0xFC:m \
  -v

# Windows (ajustar porta COM)
avrdude -c arduino -P COM3 -b19200 -p m2560 \
  -U lfuse:w:0xFF:m \
  -U hfuse:w:0xDA:m \
  -U efuse:w:0xFC:m \
  -v
```

### Verificar Fuses Programados
```bash
# Linux/Mac
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 \
  -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h

# Windows
avrdude -c arduino -P COM3 -b19200 -p m2560 \
  -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h

# Deve retornar:
# lfuse: 0xff
# hfuse: 0xda  ← DEVE SER 0xDA!
# efuse: 0xfc
```

## Passo 5: Upload do Bootloader

### Upload via avrdude
```bash
# Linux/Mac
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 \
  -U flash:w:.pio/build/ATmega2560/firmware.hex:i \
  -v

# Windows
avrdude -c arduino -P COM3 -b19200 -p m2560 \
  -U flash:w:.pio/build/ATmega2560/firmware.hex:i \
  -v

# Se der erro, tente com -F (force)
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -F \
  -U flash:w:.pio/build/ATmega2560/firmware.hex:i
```

## Passo 6: Verificação

### 1. Verificar Upload
```bash
# Linux/Mac - Ler flash e comparar
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 \
  -U flash:r:flash_readback.hex:i

# Windows
avrdude -c arduino -P COM3 -b19200 -p m2560 \
  -U flash:r:flash_readback.hex:i

# Comparar arquivos (Linux/Mac)
diff .pio/build/ATmega2560/firmware.hex flash_readback.hex
```

### 2. Teste Básico do Bootloader
```
1. Reset manual do ATmega2560
2. Bootloader deve inicializar MCP2515
3. Enviar mensagem "Bootloader start" via CAN
4. Aguardar 250ms por comando "flash init"
5. Se não receber, tentar iniciar aplicação principal
```

## Configuração PlatformIO para Arduino Nano ISP

### platformio.ini Otimizado
```ini
[env:ATmega2560_ArduinoNanoISP]
board = ATmega2560
platform = atmelavr
framework = arduino

# Configuração Arduino Nano como ISP
upload_protocol = arduino
upload_port = /dev/ttyUSB0    ; Linux/Mac
; upload_port = COM3          ; Windows - descomentar e ajustar
upload_speed = 19200
upload_flags = 
  -F  ; force se houver problemas

# Fuses corretos para bootloader
board_fuses.lfuse = 0xFF
board_fuses.hfuse = 0xDA  ; ← CRÍTICO!
board_fuses.efuse = 0xFC

# Build flags para bootloader na posição correta
build_flags =
  -Os
  -Wl,--section-start=.text=0x03F000
```

### Comandos PlatformIO Específicos
```bash
# 1. Programar fuses (fazer backup primeiro!)
pio run -e ATmega2560_ArduinoNanoISP --target fuses

# 2. Upload bootloader  
pio run -e ATmega2560_ArduinoNanoISP --target upload

# 3. Fazer tudo junto (fuses + upload)
pio run -e ATmega2560_ArduinoNanoISP --target fuses --target upload

# 4. Se der problemas de porta
pio run -e ATmega2560_ArduinoNanoISP --target upload --upload-port /dev/ttyUSB0
```

## Troubleshooting

### Erros Comuns com Arduino Nano ISP

**1. "Device signature = 0x000000" ou "Device not responding"**
```
Possíveis causas:
❌ Falta capacitor 10µF no reset do Nano
❌ Conexões ISP incorretas
❌ Arduino Mega não alimentado
❌ Sketch ArduinoISP não carregado no Nano

Soluções:
✅ Verificar capacitor 10µF (+ no RESET, - no GND do Nano)
✅ Refazer conexões conforme diagrama
✅ Verificar alimentação 5V no target
✅ Recarregar sketch ArduinoISP
```

**2. "Verification error" ou "Flash write error"**
```bash
# Tentar com force e velocidade menor
avrdude -c arduino -P /dev/ttyUSB0 -b9600 -p m2560 -F \
  -U flash:w:firmware.hex:i
```

**3. "Can't find programmer" ou "Port not found"**
```
Linux:
- Verificar porta: ls /dev/ttyUSB*
- Permissões: sudo usermod -a -G dialout $USER

Windows:
- Device Manager → Ports
- Instalar driver CH340/CP2102 se necessário
```

**4. Nano reseta durante programação**
```
❌ FALTA CAPACITOR 10µF!
✅ Capacitor 10µF entre RESET e GND do Arduino Nano
   (lado positivo no RESET)
```

**5. Fuses não programam**
```bash
# Tentar uma vez cada fuse separadamente
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -U lfuse:w:0xFF:m
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -U hfuse:w:0xDA:m  
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -U efuse:w:0xFC:m
```

### Dicas de Debug
```bash
# Verbose para ver detalhes
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560 -v -v -v

# Testar comunicação básica
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m2560

# Verificar se Nano responde
avrdude -c arduino -P /dev/ttyUSB0 -b19200 -p m328p  # target = nano
```

## Próximos Passos (Após Bootloader)

### 1. Conectar MCP2515 ao ATmega2560
```
ATmega2560    MCP2515
MISO (PB3) ← MISO
MOSI (PB2) → MOSI  
SCK  (PB1) → SCK
SS   (PB0) → CS
5V         → VCC
GND        → GND
```

### 2. Configurar Interface CAN
```bash
# Linux
sudo modprobe can
sudo ip link set can0 type can bitrate 500000
sudo ip link set up can0
```

### 3. Instalar Flash App
```bash
npm install -g mcp-can-boot-flash-app
```

### 4. Flash Firmware via CAN
```bash
mcp-can-boot-flash-app -f firmware.hex -p m2560 -m 0x0042 -i can0
```

---

## ✅ Checklist Final

- [ ] Arduino Nano com ArduinoISP carregado
- [ ] Capacitor 10µF no reset do Nano (**OBRIGATÓRIO!**)
- [ ] 6 conexões ISP corretas (D10→RESET, D11→D51, D12→D50, D13→D52, 5V→5V, GND→GND)
- [ ] Teste de comunicação OK (`avrdude -v` funciona)
- [ ] Backup dos fuses originais feito
- [ ] Fuses programados (HFUSE = 0xDA obrigatório!)
- [ ] Bootloader compilado (`firmware.hex` existe)
- [ ] Bootloader uploadado com sucesso
- [ ] Verificação do flash OK

**🎯 Resultado**: ATmega2560 com bootloader CAN funcionando, pronto para receber firmware via barramento CAN usando Arduino Nano como programmer
