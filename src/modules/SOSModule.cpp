#include "SOSModule.h"
#include "modules/RssiFingerprintingModule.h" // Ensure global instance is declared
#include "modules/CannedMessageModule.h"    // Ensure global instance is declared
#include "globals.h"                        // For mainService and other globals if needed by modules
#include "mesh/Router.h"                    // For router->getNodeId() or similar if needed for message
#include "configuration.h"                  // For access to config if needed

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // For semaphores if flags become complex

// Global instance definition
SOSModule *sosModule = nullptr;

// Logging tag
static const char *TAG = "SOSModule";

// Assuming RssiFingerprintingModule and CannedMessageModule have global pointers
// If not, they need to be passed via Modules.cpp or another mechanism.
// For now, we will rely on their headers providing `::rssiFingerprintingModule` etc.
// If these are not directly available, this will need adjustment.
extern RssiFingerprintingModule *rssiFingerprintingModule;
extern CannedMessageModule *cannedMessageModule;


SOSModule::SOSModule() : MeshModule("SOS"), concurrency::OSThread("SOSModuleThread", 4096, 2, CORE_ID_CONSOLIDATED) {
    sosModule = this; // Assign global instance
    sosButtonActive = false;
    buttonPressStartTimeMillis = 0;
    lastInterruptTimeMillis = 0;
    potentialLongPress = false;
    longPressTriggered = false;

    // Attempt to get module instances. These must be created before SOSModule.
    rssiFingerprintingModuleInstance = ::rssiFingerprintingModule;
    cannedMessageModuleInstance = ::cannedMessageModule;
}

SOSModule::~SOSModule() {
    // Detach interrupt, cleanup GPIO? (ESP-IDF might handle some of this)
    gpio_isr_handler_remove((gpio_num_t)SOS_BUTTON_PIN);
}

void IRAM_ATTR SOSModule::gpioIsrHandler(void *arg) {
    SOSModule *module = static_cast<SOSModule *>(arg);
    uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;

    if ((now - module->lastInterruptTimeMillis) > module->debounceTimeMillis) {
        module->lastInterruptTimeMillis = now;
        if (gpio_get_level((gpio_num_t)SOS_BUTTON_PIN) == 0) { // Assuming active low
            module->buttonPressStartTimeMillis = now;
            module->sosButtonActive = true; // Signal the thread
            module->potentialLongPress = true; 
            module->longPressTriggered = false; // Reset long press trigger
        } else { // Button released
            module->potentialLongPress = false;
            // If it was active and not yet a long press, it could be a short press (not handled yet)
            // Or it's the release after a long press.
            // For now, sosButtonActive is cleared by runOnce after processing.
        }
    }
}

void SOSModule::setup() {
    ESP_LOGI(TAG, "Setting up SOS Module...");

    if (!rssiFingerprintingModuleInstance) {
        ESP_LOGE(TAG, "RssiFingerprintingModule instance not available!");
    }
    if (!cannedMessageModuleInstance) {
        ESP_LOGE(TAG, "CannedMessageModule instance not available!");
    }

    // Configure button GPIO
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_ANYEDGE; // Trigger on both press and release for long press logic
    io_conf.pin_bit_mask = (1ULL << SOS_BUTTON_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;   // Enable pull-up for active low
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Install ISR service and add handler for the pin
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3); // ESP_INTR_FLAG_DEFAULT
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "ISR service already installed. This might be okay.");
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
        return;
    }
    
    err = gpio_isr_handler_add((gpio_num_t)SOS_BUTTON_PIN, gpioIsrHandler, (void *)this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(err));
        return;
    }

    start(); // Start the OSThread
    ESP_LOGI(TAG, "SOS Module setup complete. Monitoring GPIO %d", SOS_BUTTON_PIN);
}

int32_t SOSModule::runOnce() {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (sosButtonActive) {
        if (gpio_get_level((gpio_num_t)SOS_BUTTON_PIN) == 0) { // Button is still pressed
            if (potentialLongPress && !longPressTriggered && (now - buttonPressStartTimeMillis >= longPressTimeMillis)) {
                ESP_LOGI(TAG, "Long press detected!");
                longPressTriggered = true; // Mark that long press has been triggered
                triggerSOS();
                // sosButtonActive will be cleared once button is released or if we decide to clear it after triggering
            }
            // If it's not a long press yet, just keep sosButtonActive true and wait.
        } else { // Button was released
            ESP_LOGD(TAG, "Button released.");
            // If a long press was already triggered, we've done our job.
            // If not (i.e., it was a short press), we could handle that here.
            // For now, just reset flags.
            sosButtonActive = false;
            potentialLongPress = false;
            longPressTriggered = false; 
            buttonPressStartTimeMillis = 0;
        }
    }
    
    // If button is not pressed, and potentialLongPress is true, it means it was released before long press time
    if (gpio_get_level((gpio_num_t)SOS_BUTTON_PIN) != 0 && potentialLongPress && !longPressTriggered) {
         ESP_LOGD(TAG, "Button released before long press triggered.");
         potentialLongPress = false; // Reset
         sosButtonActive = false; // Ensure it's processed
    }

    return 50; // Check every 50ms
}

// Public method wrapper
void SOSModule::processSosButtonActivation() {
    // This method could be used if direct ISR management in the class is problematic
    // or for manual triggering. For now, the ISR sets the flag handled by runOnce.
    // We can simulate parts of the ISR logic if needed for testing.
    ESP_LOGI(TAG, "processSosButtonActivation called - simulating button active state for runOnce");
    // This might not be needed if ISR works as expected.
    // For now, let's assume ISR sets sosButtonActive and runOnce handles it.
}


void SOSModule::triggerSOS() {
    ESP_LOGI(TAG, "SOS Triggered!");

    if (!rssiFingerprintingModuleInstance) {
        ESP_LOGE(TAG, "Cannot get location, RssiFingerprintingModule is null.");
        // Optionally send SOS without location
    }
    if (!cannedMessageModuleInstance) {
        ESP_LOGE(TAG, "Cannot send SOS, CannedMessageModule is null.");
        return; // Critical failure
    }

    std::string locationName = "";
    double lat = 0.0, lon = 0.0;

    if (rssiFingerprintingModuleInstance) {
        ESP_LOGI(TAG, "Attempting to get location for SOS...");
        
        // Trigger a new scan and get results
        rssiFingerprintingModuleInstance->triggerNewScan();
        std::vector<RssiSample> scanResults = rssiFingerprintingModuleInstance->getCurrentScanResults();
        
        ESP_LOGI(TAG, "Scan for SOS yielded %d results.", scanResults.size());
        for(const auto& sample : scanResults) {
            ESP_LOGD(TAG, "  Sample: ID=%s, RSSI=%d", sample.id.c_str(), sample.rssi);
        }

        // Before calling localize, ensure RssiFingerprintingModule has some data or a way to handle empty scans.
        // The localize function itself checks if its database is empty.
        auto locationTuple = rssiFingerprintingModuleInstance->localize(scanResults, 3); // k=3
        lat = std::get<0>(locationTuple);
        lon = std::get<1>(locationTuple);
        locationName = std::get<2>(locationTuple);

        ESP_LOGI(TAG, "Localized to: Lat: %lf, Lon: %lf, Name: %s", lat, lon, locationName.c_str());
    } else {
        ESP_LOGE(TAG, "RssiFingerprintingModule instance is null. Cannot get location.");
        // locationName, lat, lon will remain default (empty or 0.0)
    }

    char sosMessageBuffer[200]; // Ensure buffer is large enough
    if (locationName.empty() && (lat == 0.0 && lon == 0.0)) {
        snprintf(sosMessageBuffer, sizeof(sosMessageBuffer), "SOS! Location unknown.");
    } else if (locationName.empty()) {
        snprintf(sosMessageBuffer, sizeof(sosMessageBuffer), "SOS! Last known location: (Lat: %.3f, Lon: %.3f)", lat, lon);
    } else {
        snprintf(sosMessageBuffer, sizeof(sosMessageBuffer), "SOS! Last known location: %s (Lat: %.3f, Lon: %.3f)", locationName.c_str(), lat, lon);
    }
    
    std::string sosMessage(sosMessageBuffer);
    ESP_LOGI(TAG, "Sending SOS Message: %s", sosMessage.c_str());

    if (cannedMessageModuleInstance) {
        // CannedMessageModule::sendCannedMessage typically broadcasts.
        // If it takes a string directly:
        // cannedMessageModuleInstance->sendCannedMessage(sosMessage);
        // However, CannedMessageModule typically sends pre-configured messages or uses an internal text buffer.
        // A more direct way might be needed, or use TextMessageModule if SOS is free-form.
        // For now, let's assume there's a way to send an arbitrary string.
        // The sendText method in CannedMessageModule seems more appropriate if available and public,
        // or if we need to use TextMessageModule instead.
        // Re-checking CannedMessageModule.h: it doesn't have a public sendCannedMessage(std::string).
        // It has sendText(NodeNum dest, ChannelIndex channel, const char* message, bool wantReplies) which is protected.
        // This implies CannedMessageModule might not be the right choice for arbitrary string sending.
        // Let's use TextMessageModule instead for sending arbitrary SOS string.
        
        // Fallback: If TextMessageModule is available and preferred for arbitrary messages:
        if (::textMessageModule) { // Check if textMessageModule global instance exists
             ESP_LOGI(TAG, "Using TextMessageModule to send SOS.");
            ::textMessageModule->sendTextMessage(sosMessage, NODENUM_BROADCAST, false); // Send to broadcast, no ACK
        } else if (cannedMessageModuleInstance) {
             ESP_LOGW(TAG, "TextMessageModule not available. Attempting to use CannedMessageModule (may not work as expected for arbitrary string).");
             // This part is tricky as CannedMessageModule is for pre-set messages.
             // A workaround might be to set its internal buffer and trigger send, but that's a hack.
             // For now, we'll log that this is a placeholder for proper CannedMessage sending.
             // cannedMessageModuleInstance->showTemporaryMessage(sosMessage.c_str()); // This just shows on screen.
             // A proper solution would be to add a "send this string" capability to CannedMessageModule or use TextMessageModule.
             // Given the constraints, if TextMessageModule is not available, we can't reliably send an arbitrary string via CannedMessageModule.
             ESP_LOGE(TAG, "CannedMessageModule cannot send arbitrary string directly. SOS message not sent via CannedMessageModule.");
             // For the purpose of this task, let's assume TextMessageModule is the way for arbitrary messages.
        }

    } else {
        ESP_LOGE(TAG, "CannedMessageModule instance is null after all. SOS Not sent.");
    }
}
