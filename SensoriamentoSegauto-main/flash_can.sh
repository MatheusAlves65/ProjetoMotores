#!/bin/bash

# Check if .hex file is provided
if [ -z "$1" ]; then
    echo "Usage: $0 firmware.hex"
    exit 1
fi

HEX_FILE="$1"
MCU_ID="0x0042"
RESET_ID="243#00"

echo "Flashing $HEX_FILE to MCU $MCU_ID via CAN with reset $RESET_ID..."
mcp-can-boot-flash-app -f "$HEX_FILE" -p m2560 -m "$MCU_ID" --reset "$RESET_ID"

STATUS=$?
if [ $STATUS -eq 0 ]; then
    echo "Flashing completed successfully."
else
    echo "Flashing failed with status $STATUS."
fi
