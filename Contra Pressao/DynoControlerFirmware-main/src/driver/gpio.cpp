#include "driver/gpio.h"

// Contador de rotação (volatile para interrupção)
volatile    int enc_pos; 
static      int lastClkState;
static      int lastSwState;

void d_gpio_read_encoder_irq(void);
boolean lock = 0;
uint16_t lock_counter = 0;
uint8_t prev_dir = 0;



void d_gpio_init(void)
{
    //1-Encoder: setting the pins
    pinMode(CLTPAD_ENC_CLK_PIN, INPUT_PULLUP);//Enable internal pullup
    pinMode(CLTPAD_ENC_DT_PIN , INPUT_PULLUP);//Enable internal pullup
    pinMode(CLTPAD_ENC_SW_PIN , INPUT_PULLUP);//Enable internal pullup
    pinMode(CLTPAD_AN_INP_PIN , INPUT);       //Enable input
    pinMode(CLTPAD_APP_INP_PIN, INPUT);       //Enable input
    pinMode(CLTPAD_PRESS_INP_PIN, INPUT); 
    pinMode(CLTPAD_R_IS_INP_PIN, INPUT); 
    pinMode(CLTPAD_L_IS_INP_PIN, INPUT); 
    pinMode(CLTPAD_F_EN_PIN, OUTPUT);
    pinMode(CLTPAD_R_EN_PIN, OUTPUT);
    digitalWrite(CLTPAD_F_EN_PIN, HIGH);
    digitalWrite(CLTPAD_R_EN_PIN, HIGH);

    //2-DAC: Starting SPI communication with DAC
    Wire.begin();
    Wire.setWireTimeout(3000, true);

    //3-Encoder: read initial pins state
    lastClkState    = digitalRead(CLTPAD_ENC_CLK_PIN);
    lastSwState     = digitalRead(CLTPAD_ENC_SW_PIN);

    //4-Encoder: Attach interruption to encoder 
    attachInterrupt(digitalPinToInterrupt(CLTPAD_ENC_CLK_PIN), d_gpio_read_encoder_irq, CHANGE);
}

void d_gpio_read_raw(uint8_t channel, uint16_t *raw)
{
    *raw=analogRead(channel);    
}

void d_gpio_read_analog_input(uint16_t *raw)
{
    *raw=analogRead(CLTPAD_AN_INP_PIN);        
}

void d_gpio_read_pedal_app1(uint16_t *raw)
{
    *raw=analogRead(CLTPAD_APP1_INP_PIN); 
    //Serial.print("D_GPIO_APP1: ");    
    //Serial.println(*raw);        
    //*raw=(uint16_t)(enc_pos*10.23);      
}

void d_gpio_read_pedal_app2(uint16_t *raw)
{
    *raw=analogRead(CLTPAD_APP2_INP_PIN);        
    //*raw=(uint16_t)(1023-enc_pos*10.23);      
}

void d_gpio_read_enc(encoder_t *enc)
{
    //Check if button was pressed
    if (digitalRead(CLTPAD_ENC_SW_PIN) == LOW && lastSwState == HIGH) 
    {
        enc->e_swt=true;            
    }
    else
    {
        enc->e_swt=false;            
    }
    //Read enconder position
    enc->e_pos = enc_pos;  
}

void d_gpio_read_press(uint16_t *pressure_raw){
    *pressure_raw =analogRead(CLTPAD_PRESS_INP_PIN);   
}

void d_gpio_write_enc(const encoder_t *enc)
{
    enc_pos=enc->e_pos;    
    //Serial.print("D_GPIO_PEDAL: ");    
    //Serial.println(enc_pos);  
}

void d_gpio_read_encoder_irq(void)
{
    int clkState = digitalRead(CLTPAD_ENC_CLK_PIN);
    int dtState  = digitalRead(CLTPAD_ENC_DT_PIN);
    //Change clock state
    if (clkState!=lastClkState)
    {
        if (dtState != clkState) 
        {
            //Sentido Horário (incremento)
            enc_pos++;            
        }
        else
        {
            //Sentido Anti-Horário (decremento)
            enc_pos--;            
        }
        //Serial.print("D_GPIO_PEDAL: ");    
        //Serial.println(enc_pos);    
    }
}

void d_gpio_send_DAC_voltage(int BusDirection, uint16_t valueMCP4725)
{
  uint8_t buffer[3]; 
  buffer[0]= 0b01000000;                         // Control Byte.
  buffer[1] = valueMCP4725 >> 4;                 // MSB 11-4 Shift right 4 places.
  buffer[2] = valueMCP4725 << 4;                 // LSB 3-0 shift left 4 places.
  Wire.beginTransmission (BusDirection);         // Device address
  Wire.write (buffer[0]);                        // Position 0 of array.   
  Wire.write (buffer[1]);                        // Position 1 (8 MSB).   
  Wire.write (buffer[2]);                        // Position 2 (4 LSB).
  int err = Wire.endTransmission();       // End of Communication
  if (err != 0) {
    //Serial.println("I2Cerror");
  }
}

void d_gpio_set_valve(int dir, int pwmVal) {
    static uint16_t raw = 0;
        if (dir == 1) {//Open
            raw = analogRead(CLTPAD_R_IS_INP_PIN);
            //Serial.print("dir1: ");
            //Serial.print(raw);
            //Serial.print("lock: ");
            //Serial.println(lock_r);
            if (raw >= 60){
                lock_counter ++;
                if (lock_counter >= 10){
                    lock = 1;
                    analogWrite(CLTPAD_R_PWM_PIN, 0);
                    analogWrite(CLTPAD_F_PWM_PIN, 0);
                }
            } else if (lock == 0){
                analogWrite(CLTPAD_R_PWM_PIN, pwmVal+85);
                analogWrite(CLTPAD_F_PWM_PIN, 0);
            }  
        } else if (dir == 2) {//Close
            raw = analogRead(CLTPAD_L_IS_INP_PIN);
            //Serial.print("dir2: ");
            //Serial.print(raw);
            //Serial.print("lock: ");
            //Serial.println(lock_l);
            if (raw >= 60){
                lock_counter ++;
                if (lock_counter >= 10){
                    lock = 1;
                    analogWrite(CLTPAD_R_PWM_PIN, 255);
                    analogWrite(CLTPAD_F_PWM_PIN, 0);
                    delay(150);
                    analogWrite(CLTPAD_R_PWM_PIN, 0);
                    analogWrite(CLTPAD_F_PWM_PIN, 0);
                    delay(10);
                }     
            } else if (lock == 0) {
                analogWrite(CLTPAD_R_PWM_PIN, 0);
                analogWrite(CLTPAD_F_PWM_PIN, pwmVal+85);
            }  
        } else if (dir == 0) {
            lock = 0;
            lock_counter = 0;
            analogWrite(CLTPAD_R_PWM_PIN, 0);
            analogWrite(CLTPAD_F_PWM_PIN, 0);
        }
        if (prev_dir != dir){
            lock_counter = 0;
            lock = 0;
        }
        //Serial.print(dir);
        //Serial.print("   ");
        //Serial.println(lock);
        prev_dir = dir;
        //Serial.println(lock);
}
