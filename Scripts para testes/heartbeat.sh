#!/bin/bash
# test_heartbeat.sh (CORRIGIDO)

echo "=============================="
echo "  TESTE DE HEARTBEAT"
echo "=============================="
echo

# Verifica se CAN0 existe
if ! ip link show can0 &> /dev/null; then
    echo "✗ CAN0 não encontrado!"
    exit 1
fi

# Pega estado completo
FULL_STATE=$(ip -details link show can0 | grep -oP 'state \K[A-Z-]+')
CAN_STATE=$(ip -details link show can0 | grep -oP 'can state \K[A-Z-]+')

echo "Estado Interface: $FULL_STATE"
echo "Estado CAN: $CAN_STATE"
echo

# Verifica se está UP
if [[ "$FULL_STATE" == *"UP"* ]]; then
    echo "✓ Interface está UP"
else
    echo "✗ Interface não está UP"
    exit 1
fi

# Verifica estado CAN
if [[ "$CAN_STATE" == "ERROR-ACTIVE" ]]; then
    echo "✓ CAN em estado ERROR-ACTIVE (normal)"
elif [[ "$CAN_STATE" == "ERROR-WARNING" ]]; then
    echo "⚠ CAN em estado ERROR-WARNING"
elif [[ "$CAN_STATE" == "ERROR-PASSIVE" ]]; then
    echo "⚠ CAN em estado ERROR-PASSIVE"
elif [[ "$CAN_STATE" == "BUS-OFF" ]]; then
    echo "✗ CAN em estado BUS-OFF!"
    exit 1
fi

echo
echo "Enviando heartbeat..."
cansend can0 401#R &

# Aguarda resposta
RESPONSE=$(timeout 2 candump can0 2>/dev/null | grep -m 1 "401.*\[0\]" | grep -v "remote")

if [ -n "$RESPONSE" ]; then
    echo "✓ RESPOSTA RECEBIDA!"
    echo "$RESPONSE"
else
    echo "✗ Sem resposta"
fi