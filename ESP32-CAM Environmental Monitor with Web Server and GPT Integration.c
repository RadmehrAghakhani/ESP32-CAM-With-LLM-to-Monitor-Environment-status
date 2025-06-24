// Required Libraries
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <esp_camera.h>
#include <HTTPClient.h> // For making API calls to AvalAI
#include "Base64.h"     // For Base64 encoding images
#include <FS.h>         // Required for file system operations (SPIFFS)
#include <SPIFFS.h>     // For SPIFFS file system

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
 * @brief Handles the root URL ("/") and serves the main HTML page from SPIFFS.
 */
void handleRoot() {
    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
        server.send(500, "text/plain", "Failed to open index.html");
        Serial.println("Failed to open index.html from SPIFFS.");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

/**
 * @brief Handles the '/capture_images' URL. Triggers image capture and sends back Base64 encoded images.
 */
void handleCaptureImages() {
    Serial.println("Capture images request received.");

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

    Serial.printf("Delay for capture: %d seconds\n", delaySeconds);

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

    // Send Base64 images back to the client
    String jsonResponse = "{\"status\":\"success\",\"image1\":\"";
    jsonResponse += image1Base64;
    jsonResponse += "\",\"image2\":\"";
    jsonResponse += image2Base64;
    jsonResponse += "\"}";
    server.send(200, "application/json", jsonResponse);
    Serial.println("Images sent to client.");
}

/**
 * @brief Handles the '/analyze_gpt' URL. Receives images and prompt, then calls GPT API.
 */
void handleAnalyzeGPT() {
    Serial.println("Analyze GPT request received.");

    if (!server.hasArg("plain")) { // Check if POST body is available
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Request body missing.\"}");
        Serial.println("Error: Request body missing.");
        return;
    }

    String requestBody = server.arg("plain");
    Serial.printf("Request Body: %s\n", requestBody.c_str());

    // Basic parsing to extract image data and prompt
    // For more robust parsing, consider using ArduinoJson library.
    String image1Base64;
    String image2Base64;
    String gptPrompt;

    // Example simple parsing (assumes fixed key names and structure)
    // Find "image1" value
    int img1_key_idx = requestBody.indexOf("\"image1\":\"");
    if (img1_key_idx != -1) {
        int img1_val_start = img1_key_idx + String("\"image1\":\"").length();
        int img1_val_end = requestBody.indexOf("\"", img1_val_start);
        if (img1_val_end != -1) {
            image1Base64 = requestBody.substring(img1_val_start, img1_val_end);
        }
    }

    // Find "image2" value
    int img2_key_idx = requestBody.indexOf("\"image2\":\"");
    if (img2_key_idx != -1) {
        int img2_val_start = img2_key_idx + String("\"image2\":\"").length();
        int img2_val_end = requestBody.indexOf("\"", img2_val_start);
        if (img2_val_end != -1) {
            image2Base64 = requestBody.substring(img2_val_start, img2_val_end);
        }
    }
    
    // Find "prompt" value
    int prompt_key_idx = requestBody.indexOf("\"prompt\":\"");
    if (prompt_key_idx != -1) {
        int prompt_val_start = prompt_key_idx + String("\"prompt\":\"").length();
        int prompt_val_end = requestBody.indexOf("\"", prompt_val_start);
        if (prompt_val_end != -1) {
            gptPrompt = requestBody.substring(prompt_val_start, prompt_val_end);
            // Replace escaped characters in prompt if any
            gptPrompt.replace("\\n", "\n");
            gptPrompt.replace("\\\"", "\"");
            gptPrompt.replace("\\t", "\t");
        }
    }

    if (image1Base64.isEmpty() || image2Base64.isEmpty() || gptPrompt.isEmpty()) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing image data or prompt in request.\"}");
        Serial.println("Error: Missing image data or prompt in request.");
        return;
    }

    Serial.println("[INFO] Sending data to GPT via AvalAI API...");
    HTTPClient http;
    // AvalAI GPT API endpoint for chat completions
    http.begin("https://api.avalai.ir/v1/chat/completions"); 
    http.addHeader("Content-Type", "application/json");
    // Ensure the API Key is set as a Bearer token
    http.addHeader("Authorization", "Bearer " + String(avalaiApiKey));

    // Construct the JSON payload for GPT
    String httpRequestData = "{\"model\": \"gpt-3.5-turbo-vision\", \"messages\": [";
    
    // Add the SYSTEM role message FIRST
    httpRequestData += "{\"role\": \"system\", \"content\": \"You are an environmental monitoring AI. Analyze provided images and respond precisely as requested by the user, adhering to specified output formats and instructions.\" },";

    // Then add the USER role message with the dynamically generated prompt and images
    httpRequestData += "{\"role\": \"user\", \"content\": [";
    httpRequestData += "{\"type\": \"text\", \"text\": \"" + gptPrompt + "\"},";
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
        int contentStartIndex = response.indexOf("\"content\":\"");
        if (contentStartIndex != -1) {
            contentStartIndex += String("\"content\":\"").length();
            int contentEndIndex = response.indexOf("\"", contentStartIndex);
            if (contentEndIndex != -1) {
                gptResponseText = response.substring(contentStartIndex, contentEndIndex);
                // Replace escaped characters for display
                gptResponseText.replace("\\n", "\n");
                gptResponseText.replace("\\\"", "\"");
                gptResponseText.replace("\\t", "\t");
                // The prompt might also include characters that need escaping when putting back in JSON
                // However, for display on client, these unescaped versions are better.
            }
        }
    } else {
        Serial.printf("[ERROR] HTTP Request failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
        gptResponseText = "Error making API call: " + http.errorToString(httpResponseCode);
    }
    http.end(); // Close the connection

    // Send JSON response back to the web client
    String jsonResponse = "{\"status\":\"success\",\"gpt_response\":\"";
    // Ensure gptResponseText is properly escaped for JSON transmission
    gptResponseText.replace("\"", "\\\""); // Escape double quotes
    gptResponseText.replace("\n", "\\n");  // Escape newlines
    gptResponseText.replace("\r", "");     // Remove carriage returns
    jsonResponse += gptResponseText;
    jsonResponse += "\"}";
    server.send(200, "application/json", jsonResponse);

    Serial.println("GPT analysis complete, response sent to client.");
}


/**
 * @brief Main setup function, runs once on boot.
 */
void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true); // Enable debug output for detailed logs
    Serial.println("\n");

    // Initialize SPIFFS file system
    if (!SPIFFS.begin(true)) { // true will format SPIFFS if mount fails
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }
    Serial.println("SPIFFS mounted successfully.");

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
    server.on("/", HTTP_GET, handleRoot);                // Serve the main HTML page from SPIFFS
    server.on("/script.js", HTTP_GET, []() {             // Serve script.js from SPIFFS
        File file = SPIFFS.open("/script.js", "r");
        if (!file) {
            server.send(500, "text/plain", "Failed to open script.js");
            Serial.println("Failed to open script.js from SPIFFS.");
            return;
        }
        server.streamFile(file, "application/javascript");
        file.close();
    });
    server.on("/capture_images", HTTP_GET, handleCaptureImages); // Handle image capture request
    server.on("/analyze_gpt", HTTP_POST, handleAnalyzeGPT);   // Handle GPT analysis request (expects POST with JSON)

    // Handle any other file requests by trying to serve them from SPIFFS (e.g., CSS, images)
    server.onNotFound([]() {
        if (!handleFileRead(server.uri())) {
            server.send(404, "text/plain", "File Not Found");
        }
    });

    // Start web server
    server.begin();
    Serial.println("HTTP server started.");
}

/**
 * @brief Helper function to serve files from SPIFFS.
 * @param path The path of the file to serve.
 * @return True if the file was found and served, false otherwise.
 */
bool handleFileRead(String path) {
    if (path.endsWith("/")) path += "index.html"; // Serve index.html for root directory
    String contentType = "text/plain"; // Default content type
    if (path.endsWith(".html")) contentType = "text/html";
    else if (path.endsWith(".css")) contentType = "text/css";
    else if (path.endsWith(".js")) contentType = "application/javascript";
    else if (path.endsWith(".png")) contentType = "image/png";
    else if (path.endsWith(".jpg")) contentType = "image/jpeg";
    else if (path.endsWith(".gif")) contentType = "image/gif";
    else if (path.endsWith(".ico")) contentType = "image/x-icon";
    else if (path.endsWith(".svg")) contentType = "image/svg+xml";

    if (SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        if (file) {
            server.streamFile(file, contentType);
            file.close();
            return true;
        }
    }
    return false; // File not found or couldn't be opened
}

/**
 * @brief Main loop function, runs repeatedly.
 * Handles incoming client requests for the web server.
 */
void loop() {
    server.handleClient(); // Process any pending HTTP requests
    delay(10); // Small delay to prevent watchdog timer from resetting the board
}
