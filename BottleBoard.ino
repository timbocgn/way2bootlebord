
/*
    --------------------------------------------------------------------------------

    BottleBoard       
    
    Arduino based shelf whith (in my case) 10 slots for bottles or glasses, which 
    will be illuminated from the bottom to let them shine.

    The 10 lights are round 5050 / WS2812B PCBs with 8 LEDs each, daisy chained 
    to construct one "line" of WS2812Bs.

    You can configure the color and brightness of each slot individually using the
    K40 encoder and change the overall brightness of all slots at once.

    --------------------------------------------------------------------------------

    Copyright (c) 2021 Tim Hagemann / way2.net Services

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
    --------------------------------------------------------------------------------
*/


#include <Arduino.h>
#include <RotaryEncoder.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "common.h"

// -----------------------------------------------------------------------------------------------------------------------------

// ---- hardware definitons

#define PIXEL_PIN       5 

#define ENCODER_PIN1    2
#define ENCODER_PIN2    3
#define BUTTON_PIN      4

// ---- hardware definitons

#define RL_LED_PER_ROTARY 8
#define RL_NUM_ROTARIES 10

// ---- if you want some output on the serial, remove the comments

//#define DEBUG 1

// -----------------------------------------------------------------------------------------------------------------------------

// ---- global variables

RotaryEncoder     *g_encoder  = NULL;     // ---- encode lib object
Adafruit_NeoPixel *g_pixels   = NULL;     // ---- pixel lib object

unsigned long      g_encoder_button_starttime     = 0;        // --- this is the millis() count when you hit the button
int                g_selected_rl_idx              = 0;        // --- this is the selected slot in config mode
int                g_current_brighness            = -1;       // --- the current overall brightness level
bool               g_DeviceIsConfigured           = false;    // --- flag for bootstrapping the device on first start
bool               g_SetupDirty                   = false;    // --- true, if some setup values changed
unsigned long      g_last_config_save              = 0;       // --- this is the millis() count when we last saved the config

// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------

#ifdef DEBUG

#define DEBUG_LOG(...) debug_log(__VA_ARGS__)

char g_debug_log_buffer[256];

void debug_log(const char* fmt...)
{
  va_list args;
  va_start (args, fmt);
  vsnprintf (g_debug_log_buffer,256,fmt, args);
  
  Serial.print(g_debug_log_buffer);
  
  va_end (args);
}

#else

#define DEBUG_LOG(...) ;

#endif

// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------

// ---- these are the colors we use for the LEDs. It is a color circle generated by the small generator in ./ColorGenerator

const t_rgb_triple g_ColorTable[] =
{
    { 0,0,0},
    { 255,0,0},
    { 255,43,0},
    { 255,85,0},
    { 255,128,0},
    { 255,170,0},
    { 255,213,0},
    { 255,255,0},
    { 213,255,0},
    { 170,255,0},
    { 128,255,0},
    { 85,255,0},
    { 42,255,0},
    { 0,255,0},
    { 0,255,43},
    { 0,255,85},
    { 0,255,128},
    { 0,255,170},
    { 0,255,212},
    { 0,255,255},
    { 0,212,255},
    { 0,170,255},
    { 0,128,255},
    { 0,85,255},
    { 0,43,255},
    { 0,0,255},
    { 42,0,255},
    { 85,0,255},
    { 128,0,255},
    { 170,0,255},
    { 213,0,255},
    { 255,0,255},
    { 255,0,213},
    { 255,0,170},
    { 255,0,128},
    { 255,0,85},
    { 255,0,42},
    { 255,255,255},
};

// -----------------------------------------------------------------------------------------------------------------------------

#define NUM_COLORS (sizeof(g_ColorTable) / sizeof(t_rgb_triple))

// -----------------------------------------------------------------------------------------------------------------------------

// ---- get color value from our index colors

const t_rgb_triple &GetLEDColor(const int f_idx)
{
  if (f_idx < NUM_COLORS)
    return g_ColorTable[f_idx];
   else
    return g_ColorTable[0];
}

// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------

// ---- This interrupt routine will be called on any change of one of the input signals of the encoder

void checkPosition()
{
  g_encoder->tick(); // just call tick() to check the state.
}


// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------

// --- a super simple abstraction for a state machine

class CStateMachine
{
public:
        enum t_state
        {
          state_normal,                           // --- change brightness 
          state_config_select_rl,                 // --- select the slot to be configured
          state_config_select_rl_color,           // --- change the color 
          state_config_select_rl_brightness,      // --- change the slot brightness
        };

        CStateMachine()
        {
          m_state = state_normal;
        }

        // ---- return number of pixels for this rotary.

        void SetState(const t_state f_state)
        {
          DEBUG_LOG("SetState new %d old %d\n",f_state,m_state);
          
          m_state = f_state;
        }

        const t_state GetState(void) const
        {
          return m_state;
        }

private:
        t_state m_state;
};

CStateMachine g_StateMachine;

// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------

class CRotaryLight
{
public:
          enum rl_state
          {
              rls_Backout,            // --- slot will be off, independed of the color
              rls_DisplayColor,       // --- the color will be shown
              rls_Selected,           // --- a nice blinking animation will be shown
          };

          CRotaryLight(void)
          {
            m_color   = 0; 
             m_state  = rls_DisplayColor;  
          }

          void SetColorIndex(const int f_idx)
          {
            m_color = f_idx;
          }
          
          const int GetColorIndex(void) const
          {
            return m_color;
          }
          
          void SetBrightness(const int f_b)
          {
            m_Brightness = f_b;
          }
          
          const int GetBrightness(void) const
          {
            return m_Brightness;
          }
          
          // ---- return number of pixels for this rotary.
          
          const int GetNumPixels(void) const
          {
            return RL_LED_PER_ROTARY;
          }

          // ---- set the state of the rotary light
          
          void SetMode(rl_state f_state)
          {
            m_state = f_state;
          }
                    
          // ---- add the visual representation of this rotary to the neopixel pixel map
          
          void UpdatePixels(int f_startpos)
          {
              switch(m_state)
              {
                case rls_Backout:
                                  UpdatePixels_Blackout(f_startpos);
                                  break;
                                  
                case rls_DisplayColor:
                                  UpdatePixels_Color(f_startpos);
                                  break;
                                  
                case rls_Selected:
                                  UpdatePixels_Selected(f_startpos);
                                  break;                  
              };           
          }
private:

          // ---- simple: all black

          void UpdatePixels_Blackout(int f_startpos)
          {
            for (int i=0;i < GetNumPixels();++i)
            {
              const t_rgb_triple &l_col = GetLEDColor(m_color);
              g_pixels->setPixelColor(f_startpos + i, 0,0,0);
            }
          
          }
          
          // ---- simple: show color

          void UpdatePixels_Color(int f_startpos)
          {
            for (int i=0;i < GetNumPixels();++i)
            {
              const t_rgb_triple &l_col = GetLEDColor(m_color);
              g_pixels->setPixelColor(f_startpos + i, (l_col.r*m_Brightness)>>8, (l_col.g*m_Brightness)>>8, (l_col.b*m_Brightness)>>8);
            }
          }

          // ---- tricky: create a color / black flash with a frequency of 1 Hz

          void UpdatePixels_Selected(int f_startpos)
          {
            for (int i=0;i < GetNumPixels();++i)
            {
              int l_flg = (millis() % 1000) < 500 ? 1 : 0; // this gives us a nice flashing light
              
              const t_rgb_triple &l_col = GetLEDColor(m_color);
              g_pixels->setPixelColor(f_startpos + i, l_col.r * l_flg, l_col.g * l_flg, l_col.b * l_flg); 
            }
          
          }

          int       m_color; 
          int       m_Brightness;
          rl_state  m_state;
           
};

// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------

// --- this is the abstraction of all slots

class CRotaryLightSet
{
public:
          CRotaryLightSet(void)
          {           
          }

          // ---- how many rotaries do we have? 
 
          const int GetNumLights(void) const
          {
            return RL_NUM_ROTARIES;
          }

          // ---- tell all rotary lights to dump their pixel colors 
          
          void UpdatePixels(void)
          {
            int l_pixelpos = 0;
            
            for (int i=0;i<GetNumLights();++i)
            {
              m_lights[i].UpdatePixels(l_pixelpos);
              l_pixelpos += m_lights[i].GetNumPixels();
            }
          }

          // ---- access to the lights

          CRotaryLight &operator[](int f_idx)
          {
            return m_lights[f_idx];
          }

          // ---- calculate the number of pixels for the whole set
                    
          const int GetNumPixels(void)
          {
            int l_numpixel = 0;
            
            for (int i=0;i<GetNumLights();++i)
            {
              l_numpixel += m_lights[i].GetNumPixels();
            } 

            return l_numpixel;
          }

private:
          // ---- the state of all lights 
          
          CRotaryLight m_lights[RL_NUM_ROTARIES];  
};

CRotaryLightSet g_Lights;

// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------

// --- helper to bind the rotary encoders position in a specific range. Since the decoder ticks 2 positions per "decoder slot"
//     everything is devided by 2

int GetNormEncPos(int f_max)
{
   // ---- pos is now limited from 0...GetNumLights
    
    int rawnewPos = g_encoder->getPosition();
    int newPos = min(max(rawnewPos,0),f_max*2);
  
    if (rawnewPos != newPos) g_encoder->setPosition(newPos);

    return newPos/2;
}

// -----------------------------------------------------------------------------------------------------------------------------

// --- set the encoder position taking care of our div 2 logic

void SetNormEncPos(int f_pos)
{
   g_encoder->setPosition(f_pos*2);
}


// -----------------------------------------------------------------------------------------------------------------------------

// --- set the overall brightness level, if it has changed. if force is true, it will be changed for sure.

void SetBrightness(int f_b,bool f_force = false)
{
  if ( (g_current_brighness != f_b) || f_force)
  {
      DEBUG_LOG("New brightness %d  Forced? %d\n",f_b,f_force);
      
      g_pixels->setBrightness(f_b);     

      g_current_brighness = f_b;
      
      SetNormEncPos( (g_current_brighness-5) / 10);

      // --- save the setup on the next event
      
      g_SetupDirty = true;
  }

}

// -----------------------------------------------------------------------------------------------------------------------------

// -- load the config from the EEPROM or set default values

void LoadConfig(void)
{
  int l_address = 0;

  // --- get our configuration flag. If true, we have saved somthing to the EEPROM
  
  EEPROM.get(l_address,g_DeviceIsConfigured);
  l_address += sizeof(g_DeviceIsConfigured);

  DEBUG_LOG("Loading Configuration\n");

  // ---- lets check
  
  if (g_DeviceIsConfigured == true)
  {
     DEBUG_LOG("Device is configured\n");
     
     // ---- yes found a config. first load the brightness level
     
    EEPROM.get(l_address,g_current_brighness);
    l_address += sizeof(g_current_brighness);

     // ---- now for all lights
     
    for (int i=0;i < g_Lights.GetNumLights();++i)
    {
      int l_colidx;
      EEPROM.get(l_address,l_colidx);
      l_address += sizeof(l_colidx);

      int l_bright;
      EEPROM.get(l_address,l_bright);
      l_address += sizeof(l_bright);

      g_Lights[i].SetColorIndex(l_colidx);
      g_Lights[i].SetBrightness(l_bright);

      g_Lights[i].SetMode(CRotaryLight::rls_DisplayColor);
 
    }
  }
  else
  {
     DEBUG_LOG("Device is NOT configured.Use defaults.\n");

    // ---- load std values

    g_DeviceIsConfigured = true;
    g_current_brighness  = 255;

    for (int i=0;i < g_Lights.GetNumLights();++i)
    {

      g_Lights[i].SetColorIndex(NUM_COLORS-1);
      g_Lights[i].SetBrightness(255);
      g_Lights[i].SetMode(CRotaryLight::rls_DisplayColor);
 
    }
    
  }

  SetBrightness(g_current_brighness,true);

}

// -----------------------------------------------------------------------------------------------------------------------------

// --- save the current config to the EEPROM

void SaveConfig(void)
{
  int l_address = 0;

  DEBUG_LOG("Save configuration\n");

  EEPROM.put(l_address,g_DeviceIsConfigured);
  l_address += sizeof(g_DeviceIsConfigured);

  EEPROM.put(l_address,g_current_brighness);
  l_address += sizeof(g_current_brighness);

  for (int i=0;i < g_Lights.GetNumLights();++i)
  {
    int l_colidx = g_Lights[i].GetColorIndex();
    EEPROM.put(l_address,l_colidx);
    l_address += sizeof(l_colidx);

    int l_bright = g_Lights[i].GetBrightness();
    EEPROM.put(l_address,l_bright);
    l_address += sizeof(l_bright);
  }

  
}
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------

// --- Arduino standard fot Sketch startup

void setup()
{

#ifdef DEBUG
   
  // ---- setup serial library

  Serial.begin(115200);
  while (!Serial)
    ;

  DEBUG_LOG("Bottle Board v1.0 - may the light be with you\n");

#endif

  // ---- init rotary library

  g_encoder = new RotaryEncoder(ENCODER_PIN1, ENCODER_PIN2, RotaryEncoder::LatchMode::TWO03);

  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN1), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN2), checkPosition, CHANGE);

  // ---- setup pin for encoder button
  
  pinMode(BUTTON_PIN, INPUT);

  // ---- init pixel library
  
  g_pixels = new Adafruit_NeoPixel(g_Lights.GetNumPixels(), PIXEL_PIN, NEO_GRB + NEO_KHZ800);
  g_pixels->begin(); 

  // ---- some basic first calls
  
  g_encoder->tick(); // just call tick() to check the state.
  g_encoder->setPosition(255);
  
  g_pixels->clear(); // Set all pixel colors to 'off'
  g_pixels->setBrightness(255);

  // ---- fancy startup animation
  
  for (int i=0;i < g_Lights.GetNumLights();++i)
  {
    g_Lights[i].SetMode(CRotaryLight::rls_Backout);
    g_Lights[i].SetColorIndex(NUM_COLORS-1); // set to white
    g_Lights[i].SetBrightness(255);
  }

  for (int c=0;c<2;++c)
    for (int i=0;i < g_Lights.GetNumLights();++i)
    {
      g_Lights[i].SetMode(CRotaryLight::rls_DisplayColor);
      g_Lights.UpdatePixels();
      g_pixels->show(); 
      
      delay(50);
      
      g_Lights[i].SetMode(CRotaryLight::rls_Backout);
      g_Lights.UpdatePixels();
      g_pixels->show(); 
    }

    // --- now load the config

    LoadConfig();
}


// -----------------------------------------------------------------------------------------------------------------------------

// --- this function polls the encoder push button and determines long and short press events

t_pb_state checkPushButton(void)
{
  // ---- low means pushed button
  
  if (digitalRead(BUTTON_PIN) == LOW)
  { 
    // --- check if we're alreay waiting for "up"
    
    if (g_encoder_button_starttime == 0)
    {
      // ---- no: start counting the time the button is pressed
      
      g_encoder_button_starttime = millis();      
    }
  }

  // ---- high means not pushed button
  
  if (digitalRead(BUTTON_PIN) == HIGH)
  {
    // --- check if we're alreay waiting for "up"
    
    if (g_encoder_button_starttime != 0)
    {
      // ---- yes. we now have to calculate the "press" time
   
      unsigned long l_duration = millis() - g_encoder_button_starttime;
        
      g_encoder_button_starttime = 0;

      if (l_duration > 1000)
      {
        DEBUG_LOG("push button long press with delay %d\n",l_duration);

        // ---- long press
        return pb_long_click;
      }
      else
      {
        if (l_duration > 10 && l_duration<500)
        {
          DEBUG_LOG("push button short press with delay %d\n",l_duration);
          
          // ---- short press
          return pb_short_click;
        }
      }

      // --- we will ignore all shorter pulses.
      
    }
  }
  
  return pb_no_click;
}

// -----------------------------------------------------------------------------------------------------------------------------

// --- event handler called when exiting the config mode

void OnExitConfigMode(void)
{
    DEBUG_LOG("Exit Config Mode\n");

    // --- set the next state
    
    g_StateMachine.SetState(CStateMachine::state_normal);

    // --- switch on all lights 
    
    for (int i=0;i < g_Lights.GetNumLights();++i)
    {
      g_Lights[i].SetMode(CRotaryLight::rls_DisplayColor);
    }

    // --- restore encoder pos for brightness

    SetBrightness(g_current_brighness,true);

    // --- and save to stuff to the EEPROM on the next event

    g_SetupDirty = true;
}

// -----------------------------------------------------------------------------------------------------------------------------

// --- event handler called when enterig the config mode

void OnEnterConfigMode(void)
{    
    DEBUG_LOG("Enter Config Mode\n");

    // --- set the next state
    
    g_StateMachine.SetState(CStateMachine::state_config_select_rl);

    // --- switch off all lights 
    
    for (int i=0;i < g_Lights.GetNumLights();++i)
    {
      g_Lights[i].SetMode(CRotaryLight::rls_Backout);
    }

    // --- full power
    
    g_pixels->setBrightness(255);

    // --- implicitly select the first rl
    
    g_encoder->setPosition(0);
    
    // --- this will trigger an initial update of the rl selection

    g_selected_rl_idx = -1;
}

// -----------------------------------------------------------------------------------------------------------------------------

// --- event handler to handle brightness changes in normal mode

void OnBrightnessChanged(void)
{
    // ---- get the encoder position
    
    int newPos = GetNormEncPos(25);

    // ---- and set the brightness

    SetBrightness(newPos*10+5);
}

// -----------------------------------------------------------------------------------------------------------------------------

// --- Arduino standard: called over and over again

void loop()
{

  // ---- at first check the push button
  
  t_pb_state l_button_state = checkPushButton();

  // ---- handle long clicks
  
  if (l_button_state == pb_long_click)
  {
    if (g_StateMachine.GetState() == CStateMachine::state_normal)
    {
      // ---- long press in normal state brings you into the configure state
      
      OnEnterConfigMode();
    }
    else
    {
      // ---- long press in any other state brings you back to normal state
      
      OnExitConfigMode();
      
    }
  }

  // ---- handle short clicks

  if (l_button_state == pb_short_click)
  {
    switch(g_StateMachine.GetState())
    {
      case CStateMachine::state_normal: // ---do othing
                                        break; 
                                        
      case CStateMachine::state_config_select_rl: 

                                        // --- next state
                                        
                                        g_StateMachine.SetState(CStateMachine::state_config_select_rl_color);

                                        // --- show the color and not the selection image
                                        
                                        g_Lights[g_selected_rl_idx].SetMode(CRotaryLight::rls_DisplayColor);

                                        // --- set the encoder to the color value 

                                        SetNormEncPos(g_Lights[g_selected_rl_idx].GetColorIndex());
                                        
                                        break; 
                                        
      case CStateMachine::state_config_select_rl_color: 

                                        // --- next state
                                        
                                        g_StateMachine.SetState(CStateMachine::state_config_select_rl_brightness);

                                        // --- set the encoder to the brightness value 

                                        SetNormEncPos(g_Lights[g_selected_rl_idx].GetBrightness()/4);
                                        break; 
                                        
      case CStateMachine::state_config_select_rl_brightness: 

                                        // --- next state
                                        
                                        g_StateMachine.SetState(CStateMachine::state_config_select_rl);

                                        // --- restore the selected element

                                        SetNormEncPos(g_selected_rl_idx);

                                        // --- force update
                                        
                                        g_selected_rl_idx = -1;
                                        break; 
                                        
      default:                          // ---do othing
                                        break;  
    }
  }

  // ---- some brightness magic when in normal state

  if (g_StateMachine.GetState() == CStateMachine::state_normal)
  {
    OnBrightnessChanged();
  }

  // ---- some color selection magic when in state_config_select_rl_color state

  if (g_StateMachine.GetState() == CStateMachine::state_config_select_rl_color)
  {
   // ---- pos is now limited from 0...NUM_COLORS-1
    
    int newPos = GetNormEncPos(NUM_COLORS-1);
    
    // ---- if changed

    if (g_Lights[g_selected_rl_idx].GetColorIndex() != newPos)
    {    
      // ---- set the new color in the respective light
      
      g_Lights[g_selected_rl_idx].SetColorIndex(newPos);
    }        
  }

  // ---- some individual brightness selection magic when in state_config_select_rl_color state

  if (g_StateMachine.GetState() == CStateMachine::state_config_select_rl_brightness)
  {
   // ---- pos is now limited from 0...63
    
    int newPos = GetNormEncPos(63);
    
    // ---- if changed

    if (g_Lights[g_selected_rl_idx].GetBrightness() != newPos)
    {    
      // ---- set the new color in the respective light
      
      g_Lights[g_selected_rl_idx].SetBrightness(newPos*4);
    }        
  }  
 
  // ---- some selection magic when in state_config_select_rl state

  if (g_StateMachine.GetState() == CStateMachine::state_config_select_rl)
  {
    // ---- pos is now limited from 0...GetNumLights
    
    int newPos = GetNormEncPos(g_Lights.GetNumLights()-1);
  
    // ---- if changed

    if (g_selected_rl_idx != newPos)
    {    

      DEBUG_LOG("Select new light %d, old light %d\n",newPos,g_selected_rl_idx);
      
      // --- switch the current selection off and the new selection on
      
      g_Lights[g_selected_rl_idx].SetMode(CRotaryLight::rls_Backout);
      g_selected_rl_idx = newPos;
      g_Lights[g_selected_rl_idx].SetMode(CRotaryLight::rls_Selected);   

    }        
  }

  // ---- refresh the pixels 10 times per second

  if ( (millis() % 100) == 0 )
  {
    // ---- now update the rotary lights
  
    g_Lights.UpdatePixels();
    g_pixels->show(); 
  }

  // ---- save the configuration at max every 10 seconds (of course only if changed) to safe the EEPROMs life
  
  if (g_SetupDirty)
  {
    long l_now   = millis();
    long l_delay = l_now - g_last_config_save;

    // --- correct millis overrun after 50 days
    
    if (l_delay < 0) l_delay = l_now + (0xFFFFFFFF - g_last_config_save);

    // --- more than 10 seconds?
    
    if (l_delay > 10000)
    {
        g_last_config_save = l_now;
        
        SaveConfig();

        g_SetupDirty = false;
    }
  }
  
} 
