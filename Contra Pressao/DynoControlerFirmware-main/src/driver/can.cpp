#include "driver/can.h"

//CAN Hardware → Driver (ISR, registers)
//             → HAL (filtering, decoding, units)
//             → Application (business logic)

//static d_can_rx_cb_t  can_rx_cb;
static can_rx_queue_t rxq;
static void d_can_recv_irq(void);
MCP_CAN CAN0(CAN0_CS_PIN);

void d_can_init(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("D_CAN_INIT");
    #endif    
    //Initializing CAN devices 
    while(CAN0.begin(MCP_ANY,CAN_1000KBPS,MCP_8MHZ)!=CAN_OK)
    {
        delay(100);
    }  
    //Setting interrupt pin
    pinMode(CAN0_INT_PIN, INPUT);
    //Set CANBus to normal mode  
    CAN0.setMode(MCP_NORMAL);
    attachInterrupt(digitalPinToInterrupt(CAN0_INT_PIN), d_can_recv_irq, FALLING);
}
//Set loopback
void d_can_set_loopback(void)
{
   CAN0.setMode(MCP_LOOPBACK);    
}
//Set loopback
void d_can_set_normal(void)
{
    CAN0.setMode(MCP_NORMAL);    
}
//Register RX callback
//void d_can_register_rx_callback(d_can_rx_cb_t cb)
//{
//   can_rx_cb=cb;
//}
static void d_can_recv_irq(void)
{
    can_frame_t frame;
    if (CAN0.readMsgBuf(&frame.id,&frame.dlc,frame.data)==CAN_OK)
    {
        #ifdef ENABLE_SERIAL_DEBUG
        dbSerial.println("d_can_recv_success!");
        #endif   
       //Serial.println("d_can_recv_success!");

        uint8_t next = (rxq.head + 1) % (CAN_RX_QUEUE_SIZE + 1);
        if (next != rxq.tail)   // drop if full
        {
            rxq.buf[rxq.head] = frame;
            rxq.head = next;
        }         
    }
}

//To send can data
bool d_can_send(can_frame_t *frame)
{
    #ifdef ENABLE_SERIAL_DEBUG
    dbSerial.println("d_can_send!");
    #endif
   if (CAN0.sendMsgBuf(frame->id,frame->dlc,frame->data)==CAN_OK)
    {
        #ifdef ENABLE_SERIAL_DEBUG
        dbSerial.println("d_can_send_success!");
        #endif
        return true;
    }    
    return false;     
}

//To receive can data
boolean d_can_receive(can_frame_t *frame)
{    
    if (rxq.head == rxq.tail)
    {
        return false;
    }
    *frame = rxq.buf[rxq.tail];
    rxq.tail = (rxq.tail + 1) % (CAN_RX_QUEUE_SIZE + 1);
    return true;    
}



