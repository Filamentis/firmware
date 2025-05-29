#pragma once

#include "mesh/MeshModule.h"         // Base class for modules
#include "concurrency/OSThread.h"    // For threading
#include "modules/RssiFingerprintingModule.h" // For localization
#include "modules/CannedMessageModule.h"    // For sending messages
// #include "globals.h" // Will access modules via global extern pointers

// Define a placeholder GPIO pin for the SOS button
// This should ideally be configurable via preferences later
// Using a common ESP32 GPIO, like GPIO_NUM_33
#ifndef SOS_BUTTON_PIN
#define SOS_BUTTON_PIN 33 
#endif

// Forward declare global module pointers if not already done by their headers
// extern RssiFingerprintingModule *rssiFingerprintingModule; // Assuming RssiFingerprintingModule.h declares this
// extern CannedMessageModule *cannedMessageModule;       // Assuming CannedMessageModule.h declares this

class SOSModule : public MeshModule, private concurrency::OSThread {
  public:
    SOSModule(); // Constructor
    virtual ~SOSModule();

    // Called once after all hardware and mesh protocol layers have been initialized
    virtual void setup() override; 

  protected:
    // Main loop for the OSThread
    virtual int32_t runOnce() override; 

    // We don't expect SOSModule to handle regular mesh packets for a specific portnum
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

  private:
    RssiFingerprintingModule *rssiFingerprintingModuleInstance;
    CannedMessageModule *cannedMessageModuleInstance;

    volatile bool sosButtonActive; // Flag to indicate button requires processing
    
    void handleButtonPress();    // Logic to handle debounced button press and trigger SOS
    void triggerSOS();           // Perform SOS actions (localize, send message)
    
    static void IRAM_ATTR gpioIsrHandler(void *arg); // ISR handler for the SOS button

    // Debouncing and long press detection members
    unsigned long lastInterruptTimeMillis = 0;
    const unsigned long debounceTimeMillis = 50;     // 50ms debounce
    const unsigned long longPressTimeMillis = 2000;  // 2 seconds for long press
    volatile unsigned long buttonPressStartTimeMillis = 0;
    volatile bool potentialLongPress = false;
    volatile bool longPressTriggered = false;

  public: 
    // Public method that can be called if ISR is handled externally or for testing
    void processSosButtonActivation(); 
};

extern SOSModule *sosModule; // Global instance
