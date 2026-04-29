#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <curl/curl.h>

// Definícia platformy pre macOS
#define APL 1
#define IBM 0
#define LIN 0

#include "XPLMPlugin.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"

// DataRefs pre pozíciu
static XPLMDataRef gLatitudeRef = NULL;
static XPLMDataRef gLongitudeRef = NULL;
static XPLMDataRef gAltitudeRef = NULL;
static XPLMDataRef gGroundSpeedRef = NULL;
static XPLMDataRef gTrueHeadingRef = NULL;
static XPLMDataRef gPitchRef = NULL;
static XPLMDataRef gRollRef = NULL;
static XPLMDataRef gFuelTotalRef = NULL;

// Configuration variables (with defaults)
static char kTargetURL[256] = "http://localhost:5055/";
static char kDeviceID[64] = "xplane";
static float kMinUpdateInterval = 1.0f;  // Minimum update interval (seconds)
static float kMaxUpdateInterval = 60.0f; // Maximum update interval (seconds)
static float kMinSpeedThreshold = 6.0f;  // 6km/h

// Position history for comparison
typedef struct {
    double lat;
    double lon;
    double alt;
    double heading;
    double pitch;
    double roll;
    double speed;       // knots converted to km/h after dataref read
    double fuel;        // Total fuel in kg
    time_t timestamp;   // Unix timestamp
} PositionData;

static PositionData gLastSentPosition = {0};
static PositionData gPreviousPosition = {0};
static PositionData gCurrentPosition = {0};
static float gTimeSinceLastUpdate = 0.0f;
static int sendAgain = 1;


// Function to read configuration file
void readConfiguration() {
    char configPath[512];
    FILE *configFile;
    
    // Get X-Plane system path

    XPLMGetSystemPath(configPath);
    
    // Create config file path in the X-Plane root directory
    strcat(configPath, "Resources/plugins/TraccarPlugin.cfg");

    // Open config file
    configFile = fopen(configPath, "r");
    if (!configFile) {
        char line[500];
        snprintf(line, sizeof(line),"Traccar Plugin: Config file not found (%s) - using defaults\n", configPath);
        XPLMDebugString(line); 
        return;
    }
    
    // Read config values
    char line[256];
    while (fgets(line, sizeof(line), configFile)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Parse key-value pairs
        char key[64], value[192];
        if (sscanf(line, "%63[^=]=%191[^\n]", key, value) == 2) {
            if (strcmp(key, "url") == 0) {
                strncpy(kTargetURL, value, sizeof(kTargetURL) - 1);
            } else if (strcmp(key, "id") == 0) {
                strncpy(kDeviceID, value, sizeof(kDeviceID) - 1);
            } else if (strcmp(key, "min_interval") == 0) {
                kMinUpdateInterval = atof(value);
                if (kMinUpdateInterval < 0.5f) kMinUpdateInterval = 0.5f;
            } else if (strcmp(key, "max_interval") == 0) {
                kMaxUpdateInterval = atof(value);
                if (kMaxUpdateInterval < kMinUpdateInterval) kMaxUpdateInterval = kMinUpdateInterval * 2;
            }
        }
    }
    
    fclose(configFile);
    XPLMDebugString("Traccar Plugin: Configuration loaded successfully\n");
}

// Function to read all DataRef values
void readDataRefs(PositionData* position) {
    position->lat = XPLMGetDataf(gLatitudeRef);
    position->lon = XPLMGetDataf(gLongitudeRef);
    position->alt = XPLMGetDataf(gAltitudeRef);
    position->heading = XPLMGetDataf(gTrueHeadingRef);
    position->pitch = XPLMGetDataf(gPitchRef);
    position->roll = XPLMGetDataf(gRollRef);
    position->speed = XPLMGetDataf(gGroundSpeedRef) * 1.94384; //knots to km/h
    position->fuel = XPLMGetDataf(gFuelTotalRef);
    position->timestamp = time(NULL);
}

// Check if significant change has occurred
bool hasSignificantChange() {
    // Calculate changes in trajectory. Change more then 5m/s from direct flight is "significant"

    if ( gCurrentPosition.speed > kMinSpeedThreshold ) {
        if ( abs(gLastSentPosition.heading - gCurrentPosition.heading) > (1000.0 / gCurrentPosition.speed / kMinUpdateInterval) )
            return true;
        else 
            return false;
    }
    return false;

}

// Callback funkcia pre libcurl (ignorujeme odpoveď)
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    return size * nmemb;
}

// Funkcia na odoslanie pozície
void sendPositionData(const PositionData* data) {

    // Formátovanie dát pre OsmAnd
    char postData[256];

    if (data->timestamp == 0 ) return;

    snprintf(postData, sizeof(postData), 
             "lat=%.6f&lon=%.6f&alt=%.2f&speed=%.2f&course=%.2f&fuel=%.2f&timestamp=%ld&id=%s", 
             data->lat, data->lon, data->alt, 
             data->speed, data->heading, data->fuel, data->timestamp, kDeviceID);
    
    // Inicializácia CURL
    CURL *curl;
    CURLcode res;
    
    curl = curl_easy_init();
    if(curl) {
        // Nastavenie URL a dát
        curl_easy_setopt(curl, CURLOPT_URL, kTargetURL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData);
        
        // Nastavenie callback funkcie
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        
        // Vypnutie SSL verifikácie pre lokálne použitie
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        
        // Nastavenie timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        // Vykonanie požiadavky
        res = curl_easy_perform(curl);
        
        // Kontrola chýb
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        
//        XPLMDebugString(postData);

        // Cleanup
        curl_easy_cleanup(curl);
    }
}

// Flight loop callback - volané pravidelne
float flightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {

    // Update current position by reading all DataRefs
    readDataRefs(&gCurrentPosition);
    
    // Update time since last update
    gTimeSinceLastUpdate += inElapsedSinceLastCall;
    
    if (gTimeSinceLastUpdate >= kMinUpdateInterval) {
        if (hasSignificantChange()) {
            // Significant change detected, send previous position
            sendPositionData(&gPreviousPosition);
            gLastSentPosition = gPreviousPosition;
            sendAgain=true;
        } else if (gTimeSinceLastUpdate >= kMaxUpdateInterval || sendAgain ) {
            // Maximum interval reached, send current position
            sendPositionData(&gCurrentPosition);
            gLastSentPosition = gCurrentPosition;
            sendAgain=false;
        }
        gTimeSinceLastUpdate = 0.0f;
        gPreviousPosition = gCurrentPosition;
    }
    
    return kMinUpdateInterval; // Check every second
}

// Plugin start
PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc) {
    strcpy(outName, "Traccar Position Plugin");
    strcpy(outSig, "traccar.position.plugin");
    strcpy(outDesc, "Sends aircraft position to Traccar server");

    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS",1);
    
    // Read configuration
    readConfiguration();

    // Initialize DataRefs
    gLatitudeRef = XPLMFindDataRef("sim/flightmodel/position/latitude");
    gLongitudeRef = XPLMFindDataRef("sim/flightmodel/position/longitude");
    gAltitudeRef = XPLMFindDataRef("sim/flightmodel/position/elevation");
    gGroundSpeedRef = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    gTrueHeadingRef = XPLMFindDataRef("sim/flightmodel/position/true_psi");
    gPitchRef = XPLMFindDataRef("sim/flightmodel/position/theta");      // Pitch angle
    gRollRef = XPLMFindDataRef("sim/flightmodel/position/phi");         // Roll angle
    gFuelTotalRef = XPLMFindDataRef("sim/flightmodel/weight/m_fuel");   // Total fuel in kg
    
  // Check if all DataRefs are valid
    if (!gLatitudeRef || !gLongitudeRef || !gAltitudeRef || 
        !gGroundSpeedRef || !gTrueHeadingRef || !gPitchRef || !gRollRef || !gFuelTotalRef) {
        XPLMDebugString("Traccar Plugin: Error - Could not find all required DataRefs\n");
        return 0;
    }
 
    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Registrácia flight loop callbacku
    XPLMRegisterFlightLoopCallback(flightLoopCallback, kMinUpdateInterval, NULL);
    
    char startMsg[256];
    snprintf(startMsg, sizeof(startMsg), "Traccar Plugin: Started successfully. URL: %s, ID: %s, Interval: %.1fs\n", 
             kTargetURL, kDeviceID, kMaxUpdateInterval);
    XPLMDebugString(startMsg);

    return 1;
}

// Plugin stop
PLUGIN_API void XPluginStop(void) {
    // Zrušenie registrácie callbacku
    XPLMUnregisterFlightLoopCallback(flightLoopCallback, NULL);
    
    // Cleanup libcurl
    curl_global_cleanup();
    
    XPLMDebugString("Traccar Plugin: Stopped\n");
}

// Plugin enable
PLUGIN_API void XPluginEnable(void) {
    XPLMDebugString("Traccar Plugin: Enabled\n");
}

// Plugin disable
PLUGIN_API void XPluginDisable(void) {
    XPLMDebugString("Traccar Plugin: Disabled\n");
}
