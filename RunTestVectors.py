import serial
import time
import sys
import glob
import can
from dataclasses import dataclass
from typing import List


TempFrameId = 0x123  # from config.h

# ========== Struct Definitions ==========

@dataclass
class SafetyConfigStructure:
    Monit_Enable: int = 0
    maxtemp: float = 40
    timer: int = 1000  # milliseconds


@dataclass
class AquisitionConfigStructure:
    Aquics_Enable_Continuous: int = 0
    Aquics_Enable: int = 0
    analog: bool = False
    timer: int = 1000


@dataclass
class TempReadStructure:
    CJtemp: int = 25
    TLstatus: int = 0
    TRstatus: int = 0
    BLstatus: int = 0
    BRstatus: int = 0
    TLtemp: int = 50
    TRtemp: int = 50
    BLtemp: int = 50
    BRtemp: int = 50


# ========== Function Implementations ==========

def read_digital(buf: bytes) -> List[int]:
    """Decode 8 digital 2-bit values from 2 bytes."""
    digital_command = [0] * 8
    digital_command[3] = buf[0] & 0x03
    digital_command[2] = (buf[0] >> 2) & 0x03
    digital_command[1] = (buf[0] >> 4) & 0x03
    digital_command[0] = buf[0] >> 6
    digital_command[7] = buf[1] & 0x03
    digital_command[6] = (buf[1] >> 2) & 0x03
    digital_command[5] = (buf[1] >> 4) & 0x03
    digital_command[4] = buf[1] >> 6
    return digital_command


def read_pwm_enc(buf: bytes, num: int) -> int:
    value = buf[num]
    if value <= 250:
        return (value * 255) // 250
    elif value == 255:
        return -2
    elif value >= 251:
        return -1
    else:
        return 255


def send_digital(digital_command: List[int], PWM1: int, PWM2: int, Enc: int) -> bytearray:
    tx_buf = bytearray(5)
    tx_buf[0] = (digital_command[0] << 6) | (digital_command[1] << 4) | (digital_command[2] << 2) | digital_command[3]
    tx_buf[1] = (digital_command[4] << 6) | (digital_command[5] << 4) | (digital_command[6] << 2) | digital_command[7]
    tx_buf[2] = PWM1
    tx_buf[3] = PWM2
    tx_buf[4] = Enc
    return tx_buf


def safety_config(buf: bytes) -> SafetyConfigStructure:
    tempc = SafetyConfigStructure()
    check_id = (buf[0] << 4) | (buf[1] >> 4)
    if check_id != TempFrameId:
        tempc.Monit_Enable = 2
        return tempc
    tempc.Monit_Enable = buf[1] & 0x03
    tempc.maxtemp = (buf[2] * 120) / 255
    tempc.timer = (buf[3] * 10000) // 255
    return tempc


def send_safety_config(tempc: SafetyConfigStructure) -> bytearray:
    tx_buf = bytearray(4)
    tx_buf[0] = 0x12
    tx_buf[1] = 0x30 | tempc.Monit_Enable
    tx_buf[2] = int((tempc.maxtemp * 255) / 120)
    tx_buf[3] = int((tempc.timer * 255) / 10000)
    return tx_buf


def aquisition_config(buf: bytes) -> AquisitionConfigStructure:
    aquisc = AquisitionConfigStructure()
    aquisc.timer = (buf[0] << 8) | buf[1]
    aquisc.analog = bool(buf[2] & 0x01)
    aquisc.Aquics_Enable = buf[3] & 0x03
    return aquisc


def send_aquisition_config(aquisc: AquisitionConfigStructure) -> bytearray:
    tx_buf = bytearray(8)
    tx_buf[0] = (aquisc.timer >> 8) & 0xFF
    tx_buf[1] = aquisc.timer & 0xFF
    tx_buf[2] = int(aquisc.analog) & 0x01
    tx_buf[3] = aquisc.Aquics_Enable_Continuous & 0x03
    # Padding unused bytes
    for i in range(4, 8):
        tx_buf[i] = 0x00
    return tx_buf


def temp_read(buf: bytes) -> TempReadStructure:
    temp = TempReadStructure()
    temp.CJtemp = buf[0] - 128
    temp.TLstatus = buf[1] & 0x03
    temp.TLtemp = (((buf[2] & 0x3F) << 6) | (buf[1] >> 2)) - 2048
    temp.TRstatus = buf[2] >> 6
    temp.TRtemp = (((buf[4] & 0x0F) << 8) | buf[3]) - 2048
    temp.BLstatus = (buf[4] >> 4) & 0x03
    temp.BLtemp = (((buf[6] & 0x03) << 10) | (buf[5] << 2) | (buf[4] >> 6)) - 2048
    temp.BRstatus = (buf[6] >> 2) & 0x03
    temp.BRtemp = ((buf[7] << 4) | (buf[6] >> 4)) - 2048
    return temp


def send_can_message():
    try:
        # Connect to CAN interface via MCP2515 (socketcan)
        bus = can.interface.Bus(channel='can0', bustype='socketcan')

        # Construct the data payload
        data = [0x00, 0x55, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00]

        # Create CAN message (Standard ID 0x101)
        msg = can.Message(arbitration_id=0x402,
                          data=data,
                          is_extended_id=False)

        # Send the message
        #bus.send(msg)
        #print(f"Message sent: ID=0x{msg.arbitration_id:X} Data={msg.data}")

        message = bus.recv(timeout=1.0)
        if message:
            print(f"ID: 0x{message.arbitration_id:X}, Data: {message.data.hex()}")

    except can.CanError as e:
        print(f"CAN message failed to send: {e}")
        
def main():
    bus = can.interface.Bus(channel='can0', bustype='socketcan')
    config_time = AquisitionConfigStructure(
        Aquics_Enable_Continuous=0,
        Aquics_Enable=0,
        analog=True,
        timer=2000  # 2 seconds
    )
    data = send_aquisition_config(config_time)
    msg = can.Message(
        arbitration_id=0x404,
        data=data,
        is_extended_id=False
    )
    #try:
    #    bus.send(msg)
        #print("Message sent to 0x404:", msg)
    #except can.CanError as e:
    #    print("Failed to send message:", e)


    while True:
        send_can_message()
        
        
        
if __name__ == "__main__":
    main()

