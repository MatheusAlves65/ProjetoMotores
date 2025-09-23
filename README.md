# Projeto SegurAuto

Sistema de sensoriamento e controle de motores com comunicação CAN para aplicações automotivas de segurança.

## Descrição

Este projeto implementa um sistema de monitoramento de temperatura e controle de motores usando microcontrolador ATmega2560 com comunicação via barramento CAN através do módulo MCP2515.

### Principais Funcionalidades

- **Monitoramento de Temperatura**: Leitura de sensores de temperatura com filtragem digital e limites de segurança configuráveis
- **Controle de Motores**: Controle PWM de motores com ponte H usando timers dedicados
- **Comunicação CAN**: Interface completa via MCP2515 com protocolo definido (.dbc)
- **Sistema de Segurança**: Timers de proteção que desativam motores em caso de superaquecimento
- **Armazenamento EEPROM**: Configurações de segurança persistentes
- **Bootloader CAN**: Upload remoto de firmware via barramento CAN

### Hardware Suportado

- **MCU**: ATmega2560 (Arduino Mega)
- **CAN Controller**: MCP2515 com cristal 8MHz
- **Velocidade CAN**: 500 kbps
- **Timers Utilizados**: 
  - Timer 1/2/4: Controle PWM das pontes H
  - Timer 3/5: Interrupções de segurança e controle geral

### Estrutura do Projeto

```
├── src/
│   ├── main.cpp          # Código principal
│   └── config.cpp        # Funções de configuração CAN
├── include/
│   └── config.h          # Estruturas e protótipos
├── canmod-gen1.dbc       # Definição do protocolo CAN
├── bootloader.md         # Instruções do bootloader
├── interfacetester.py    # Interface gráfica de teste
└── pioconfig2560.txt     # Configuração PlatformIO
```

### IDs CAN Principais

- `0x401-0x405`: Comandos de controle
- `0x422-0x426`: Respostas e dados de sensores  
- `0x510-0x530`: Dados de temperatura
- `0x243`: Reset remoto para bootloader

### Configuração do Hardware

Para configurar o bootloader CAN no ATmega2560, consulte o **[bootloader.md](bootloader.md)** que contém o passo a passo completo para:

- Configuração do Arduino Nano como programador ISP
- Programação dos fuses corretos
- Upload do bootloader MCP-CAN-Boot
- Configuração para atualizações remotas via CAN

⚠️ **Importante**: O bootloader remove a capacidade de upload USB. Atualizações devem ser feitas via CAN usando:
```bash
mcp-can-boot-flash-app -f firmware.hex -p m2560 -m 0x0042
```

### Desenvolvimento

Desenvolvido na **Universidade de Brasília (UnB) - Faculdade do Gama** para o projeto **SegurAuto**.

**Plataforma**: PlatformIO com Arduino Framework  
**Dependências**: mcp_can, TimerInterrupt_Generic, SimpleTimer
