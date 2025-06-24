// Required Libraries
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <esp_camera.h>
#include <HTTPClient.h> // For making API calls to AvalAI
#include "Base64.h"     // For Base64 encoding images

// Include camera pins definitions. This file is crucial for mapping camera signals to ESP32 GPIOs.
// Ensure camera_pins.h is in the same directory as this sketch or properly included in your project.
#include "camera_pins.h"

// ===========================
// WiFi Credentials
// ===========================
const char *ssid = "YOUR_WIFI_SSID";     // <<<< REPLACE WITH YOUR WIFI SSID
const char *password = "YOUR_WIFI_PASSWORD"; // <<<< REPLACE WITH YOUR WIFI PASSWORD

// ===========================
// AvalAI API Key
// ===========================
const char *avalaiApiKey = "YOUR_AVALAI_API_KEY"; // <<<< REPLACE WITH YOUR AVALAI API KEY

// Web Server object on port 80
WebServer server(80);

// Camera configuration (from cameras_pin_config.txt and esp_camea_webserver.c)
camera_config_t camera_config = {
    .ledc_channel = LEDC_CHANNEL_0,
    .ledc_timer = LEDC_TIMER_0,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .pixel_format = PIXFORMAT_JPEG, // Use JPEG for streaming and analysis
    .frame_size = FRAMESIZE_QVGA,   // QVGA (320x240) is a good balance for initial setup
    .jpeg_quality = 10,             // JPEG quality (0-63, lower is higher quality)
    .fb_count = 1,                  // Number of frame buffers to allocate
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY, // Grab a new frame when the buffer is empty
    .fb_location = CAMERA_FB_IN_PSRAM   // Try PSRAM first, fallback to DRAM if not available
};

/**
 * @brief Initializes the ESP32-CAM camera module.
 * @return True if camera initialization is successful, false otherwise.
 */
bool setupCamera() {
    // Attempt to use PSRAM for higher resolution and quality if available.
    if (psramFound()) {
        camera_config.jpeg_quality = 10;
        camera_config.fb_count = 2; // Use two frame buffers if PSRAM is available
        camera_config.grab_mode = CAMERA_GRAB_LATEST; // Grab the latest frame in buffer
        camera_config.frame_size = FRAMESIZE_UXGA; // Try UXGA (1600x1200) resolution
    } else {
        // If no PSRAM, limit frame size to prevent memory issues.
        Serial.println("PSRAM not found, limiting frame size to SVGA.");
        camera_config.frame_size = FRAMESIZE_SVGA; // SVGA (800x600)
        camera_config.fb_location = CAMERA_FB_IN_DRAM; // Use DRAM
    }

    // Initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }
    Serial.println("Camera initialized successfully.");

    // Get sensor object and apply initial settings
    sensor_t *s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) { // Specific settings for OV3660 sensor (e.g., ESP-EYE)
        s->set_vflip(s, 1);        // Flip vertically
        s->set_brightness(s, 1);   // Increase brightness
        s->set_saturation(s, -2);  // Decrease saturation
    }
    // Drop down frame size for higher initial frame rate if not using UXGA (e.g., if PSRAM not found)
    if (camera_config.pixel_format == PIXFORMAT_JPEG && !psramFound()) {
        s->set_framesize(s, FRAMESIZE_QVGA); // Fallback to QVGA for smoother operation
    }

    // Set LED flash if LED_GPIO_NUM is defined in camera_pins.h
    #if defined(LED_GPIO_NUM)
    pinMode(LED_GPIO_NUM, OUTPUT);
    digitalWrite(LED_GPIO_NUM, LOW); // Turn off LED initially
    #endif

    return true;
}

/**
 * @brief Handles the root URL ("/") and serves the main HTML page.
 * This page contains the form for delay and prompt input.
 */
void handleRoot() {
    String html = R"raw(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-CAM Environmental Monitor</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <style>
        body { font-family: 'Inter', sans-serif; }
        .spinner {
            border: 4px solid rgba(0, 0, 0, 0.1);
            width: 36px;
            height: 36px;
            border-radius: 50%;
            border-left-color: #0d6efd;
            animation: spin 1s ease infinite;
        }
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
    </style>
</head>
<body class="bg-gradient-to-br from-blue-100 to-indigo-200 min-h-screen flex items-center justify-center p-4">
    <div class="bg-white p-8 rounded-xl shadow-2xl w-full max-w-2xl border-t-4 border-indigo-500">
        <h1 class="text-4xl font-extrabold text-gray-900 mb-6 text-center">
            📸 ESP32-CAM AI Monitor
        </h1>
        <p class="text-gray-600 mb-8 text-center">
            Capture environmental changes and get AI-powered insights.
        </p>

        <form id="monitorForm" class="space-y-6">
            <div>
                <label for="delay" class="block text-gray-700 text-sm font-semibold mb-2">
                    Delay between images (seconds):
                </label>
                <input type="number" id="delay" name="delay" value="15" min="1" max="300"
                       class="w-full p-3 border border-gray-300 rounded-lg focus:ring-indigo-500 focus:border-indigo-500 transition duration-200 ease-in-out shadow-sm"
                       required>
            </div>
            <div>
                <label for="prompt" class="block text-gray-700 text-sm font-semibold mb-2">
                    GPT Prompt for Analysis:
                </label>
                <textarea id="prompt" name="prompt" rows="5"
                          class="w-full p-3 border border-gray-300 rounded-lg focus:ring-indigo-500 focus:border-indigo-500 transition duration-200 ease-in-out shadow-sm resize-y"
                          placeholder="E.g., Compare the two images and tell me what changed in the room."
                          required>The following two images were captured in the same room with a {delay}-second delay. Please analyze and describe: Any noticeable changes in objects or lighting. If any devices appear to have turned ON or OFF. Any signs of human or animal presence or movement. Any suspicious or unexpected changes. Please respond with a clear and concise summary.</textarea>
            </div>
            <button type="submit" id="submitBtn"
                    class="w-full bg-indigo-600 text-white font-bold py-3 px-4 rounded-lg hover:bg-indigo-700 focus:outline-none focus:ring-4 focus:ring-indigo-300 transition duration-300 ease-in-out transform hover:scale-105 shadow-lg">
                Start Capture & Analyze
            </button>
        </form>

        <div id="status" class="mt-8 p-4 bg-blue-50 text-blue-800 rounded-lg hidden">
            <div class="flex items-center space-x-3">
                <div class="spinner"></div>
                <span id="statusMessage" class="font-medium"></span>
            </div>
        </div>

        <div id="results" class="mt-8 p-6 bg-gray-50 rounded-xl shadow-inner hidden">
            <h2 class="text-xl font-bold text-gray-800 mb-4">GPT Analysis Results:</h2>
            <pre id="gptResponse" class="whitespace-pre-wrap text-gray-700 leading-relaxed"></pre>
        </div>
    </div>

    <script>
        document.getElementById('monitorForm').addEventListener('submit', async function(event) {
            event.preventDefault(); // Prevent default form submission

            const delay = document.getElementById('delay').value;
            let promptText = document.getElementById('prompt').value;
            // Replace placeholder in prompt text
            promptText = promptText.replace('{delay}', delay);

            const statusDiv = document.getElementById('status');
            const statusMessage = document.getElementById('statusMessage');
            const resultsDiv = document.getElementById('results');
            const gptResponsePre = document.getElementById('gptResponse');
            const submitBtn = document.getElementById('submitBtn');

            // Show status, hide results
            statusDiv.classList.remove('hidden');
            resultsDiv.classList.add('hidden');
            submitBtn.disabled = true; // Disable button during processing

            statusMessage.textContent = 'Initiating capture process...';

            try {
                // Construct URL with parameters
                const url = `/capture?delay=${delay}&prompt=${encodeURIComponent(promptText)}`;
                
                const response = await fetch(url);
                const data = await response.json(); // Expect JSON response from ESP32

                statusDiv.classList.add('hidden'); // Hide status spinner
                submitBtn.disabled = false; // Re-enable button

                if (data.status === 'success') {
                    gptResponsePre.textContent = data.gpt_response;
                    resultsDiv.classList.remove('hidden');
                } else {
                    gptResponsePre.textContent = `Error: ${data.message || 'Unknown error occurred.'}`;
                    resultsDiv.classList.remove('hidden');
                    resultsDiv.classList.add('bg-red-50', 'text-red-800'); // Style error
                }

            } catch (error) {
                statusDiv.classList.add('hidden');
                submitBtn.disabled = false;
                resultsDiv.classList.remove('hidden');
                resultsDiv.classList.add('bg-red-50', 'text-red-800');
                gptResponsePre.textContent = `Failed to connect to ESP32: ${error.message}`;
                console.error('Fetch error:', error);
            }
        });

        // Function to update status message dynamically
        function updateStatus(message) {
            document.getElementById('statusMessage').textContent = message;
        }

        // Expose updateStatus to the global scope if needed for future direct calls from Arduino side (e.g. websockets)
        window.updateStatus = updateStatus;
    </script>
</body>
</html>
)raw";
    server.send(200, "text/html", html);
}

/**
 * @brief Handles the '/capture' URL. Triggers image capture, GPT analysis, and sends back results.
 */
void handleCapture() {
    Serial.println("Capture request received on web server.");

    // Extract delay parameter
    if (!server.hasArg("delay")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"delay parameter missing.\"}");
        Serial.println("Error: delay parameter missing in request.");
        return;
    }
    int delaySeconds = server.arg("delay").toInt();
    if (delaySeconds <= 0) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid delay value. Must be positive.\"}");
        Serial.println("Error: Invalid delay value. Must be positive.");
        return;
    }

    // Extract prompt parameter
    if (!server.hasArg("prompt")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"prompt parameter missing.\"}");
        Serial.println("Error: prompt parameter missing in request.");
        return;
    }
    String gptPrompt = server.arg("prompt");

    Serial.printf("Delay: %d seconds, Prompt: \"%s\"\n", delaySeconds, gptPrompt.c_str());

    // --- Capture First Image ---
    Serial.println("[INFO] Capturing first image...");
    camera_fb_t *fb1 = esp_camera_fb_get();
    if (!fb1) {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to capture first image.\"}");
        Serial.println("[ERROR] Failed to capture first image.");
        return;
    }
    String image1Base64 = base64::encode(fb1->buf, fb1->len);
    esp_camera_fb_return(fb1);
    Serial.println("[INFO] Image #1 captured successfully. Waiting for delay...");

    // --- Wait for Delay ---
    delay(delaySeconds * 1000); // delay in milliseconds

    // --- Capture Second Image ---
    Serial.println("[INFO] Capturing second image...");
    camera_fb_t *fb2 = esp_camera_fb_get();
    if (!fb2) {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to capture second image.\"}");
        Serial.println("[ERROR] Failed to capture second image.");
        return;
    }
    String image2Base64 = base64::encode(fb2->buf, fb2->len);
    esp_camera_fb_return(fb2);
    Serial.println("[INFO] Image #2 captured successfully.");

    // --- Send to GPT via AvalAI API ---
    Serial.println("[INFO] Sending data to GPT via AvalAI API...");
    HTTPClient http;
    // AvalAI GPT API endpoint for chat completions
    http.begin("https://api.avalai.ir/v1/chat/completions"); 
    http.addHeader("Content-Type", "application/json");
    // Ensure the API Key is set as a Bearer token
    http.addHeader("Authorization", "Bearer " + String(avalaiApiKey));

    String httpRequestData = "{\"model\": \"gpt-3.5-turbo-vision\", \"messages\": [";
    httpRequestData += "{\"role\": \"user\", \"content\": [";
    httpRequestData += "{\"type\": \"text\", \"text\": \"" + gptPrompt + "\"},";
    // Base64 images need to be prefixed with "data:image/jpeg;base64,"
    httpRequestData += "{\"type\": \"image_url\", \"image_url\": {\"url\": \"data:image/jpeg;base64," + image1Base64 + "\"}},";
    httpRequestData += "{\"type\": \"image_url\", \"image_url\": {\"url\": \"data:image/jpeg;base64," + image2Base64 + "\"}}";
    httpRequestData += "]}]}";

    int httpResponseCode = http.POST(httpRequestData);
    String gptResponseText = "Failed to get response from GPT.";

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("[INFO] HTTP Response code: %d\n", httpResponseCode);
        Serial.printf("[INFO] GPT Raw Response: %s\n", response.c_str());

        // Simple parsing of JSON response for "content" from "choices" array
        // This parsing is basic and assumes a simple structure. For robust parsing, use a JSON library like ArduinoJson.
        int contentStartIndex = response.indexOf("\"content\":\"");
        if (contentStartIndex != -1) {
            contentStartIndex += String("\"content\":\"").length();
            int contentEndIndex = response.indexOf("\"", contentStartIndex);
            if (contentEndIndex != -1) {
                gptResponseText = response.substring(contentStartIndex, contentEndIndex);
                // Replace escaped newlines for display
                gptResponseText.replace("\\n", "\n");
                gptResponseText.replace("\\\"", "\"");
                gptResponseText.replace("\\t", "\t");
                Serial.println("[INFO] Parsed GPT Response:");
                Serial.println(gptResponseText);
            }
        }
    } else {
        Serial.printf("[ERROR] HTTP Request failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
        gptResponseText = "Error making API call: " + http.errorToString(httpResponseCode);
    }
    http.end(); // Close the connection

    // Send JSON response back to the web client
    String jsonResponse = "{\"status\":\"success\",\"gpt_response\":\"";
    jsonResponse += gptResponseText;
    jsonResponse += "\"}";
    server.send(200, "application/json", jsonResponse);

    Serial.println("Operation complete. Ready for next command.");
}

/**
 * @brief Main setup function, runs once on boot.
 */
void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true); // Enable debug output for detailed logs
    Serial.println("\n");

    // Initialize camera
    if (!setupCamera()) {
        Serial.println("Camera setup failed! Please check connections and power. Halting.");
        while (true); // Stay in loop if camera fails to initialize
    }

    // Connect to WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    WiFi.setSleep(false); // Keep WiFi active for web server responsiveness

    unsigned long connectTimeout = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - connectTimeout > 30000) { // 30-second timeout
            Serial.println("\nWiFi connection timed out. Please check credentials.");
            while (true); // Halt if WiFi cannot connect
        }
    }
    Serial.println("");
    Serial.println("WiFi connected successfully!");
    Serial.print("Access the web server at: http://");
    Serial.println(WiFi.localIP());

    // Setup web server routes
    server.on("/", handleRoot);          // Serve the main HTML page
    server.on("/capture", HTTP_GET, handleCapture); // Handle image capture and analysis requests

    // Start web server
    server.begin();
    Serial.println("HTTP server started.");
}

/**
 * @brief Main loop function, runs repeatedly.
 * Handles incoming client requests for the web server.
 */
void loop() {
    server.handleClient(); // Process any pending HTTP requests
    delay(10); // Small delay to prevent watchdog timer from resetting the board
}
```


```c
#ifndef CAMERA_PINS_H
#define CAMERA_PINS_H

// This header defines the GPIO pins for the ESP32-CAM (AI-Thinker model)
// It's crucial for the camera module to function correctly.

// Power Down and Reset pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1 // -1 means not connected / not used

// SCCB (Serial Camera Control Bus) pins for I2C communication with the camera sensor
#define SIOD_GPIO_NUM     26 // SDA
#define SIOC_GPIO_NUM     27 // SCL

// XCLK (External Clock) pin
#define XCLK_GPIO_NUM      0

// Data pins (D0-D7) - These receive the image pixel data
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

// Synchronization and Pixel Clock pins
#define VSYNC_GPIO_NUM    25 // Vertical Sync
#define HREF_GPIO_NUM     23 // Horizontal Reference
#define PCLK_GPIO_NUM     22 // Pixel Clock

// Optional: LED Flash pin (if your board has one connected)
// #define LED_GPIO_NUM      4 // Example for some ESP32-CAM boards

#endif // CAMERA_PINS_H
