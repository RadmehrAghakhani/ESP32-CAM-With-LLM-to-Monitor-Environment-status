#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "base64.h" // You might need to install this via Library Manager

// WiFi credentials
const char* ssid = "Hosna"; // <-- UPDATE WITH YOUR WIFI SSID
const char* password = "hosna2003"; // <-- UPDATE WITH YOUR WIFI PASSWORD

// AI API config
const char* api_url = "https://api.avalapis.ir/v1/chat/completions";
const char* api_key = "aa-LE6yzzso7O7fqY0DG2h8DkfmnagYQ7XR6QPF8ZvkZlRXX2pP"; // <-- UPDATE WITH YOUR API KEY
const char* model = "gemini-1.5-flash";

// Image base64 buffers
String img1_base64 = "";
String img2_base64 = "";

// Prompt suffix that will be appended to all selected prompts
const String base_analysis_prompt = " Please analyze and describe:\n- Any noticeable changes in objects or lighting.\n- If any devices appear to have turned ON or OFF.\n- Any signs of human or animal presence or movement.\n- Any suspicious or unexpected changes.\nPlease respond with a clear and concise summary.";

WiFiServer server(80);

// ESP32-CAM config (AI-Thinker model)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Function Prototypes
void handleRoot(WiFiClient &client);
void handleCaptureImages(WiFiClient &client, int delaySeconds);
void handleGetAnalysis(WiFiClient &client, String promptType);
void handleResetData(WiFiClient &client);
String captureImage();
String sendToAPI(String prompt_text, String img1, String img2);
String urlDecode(String input); // Helper for URL decoding

// ============ Setup ============
void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // Configure camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QQVGA; // Good balance of quality and memory
    config.jpeg_quality = 10;
    config.fb_count = 1; // 1 framebuffer is often sufficient for capture-then-process
  } else {
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  server.begin();
  Serial.println("Server ready. Access via browser at http://" + WiFi.localIP().toString());
}

// ============ Loop ============
void loop() {
  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    String header = "";
    String method = "";
    String url = "";

    // Read HTTP request header
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        header += c;
        if (c == '\n') {
          if (currentLine.length() == 0) { // End of header
            // Parse HTTP method and URL
            int firstSpace = header.indexOf(' ');
            if (firstSpace != -1) {
              method = header.substring(0, firstSpace);
              int secondSpace = header.indexOf(' ', firstSpace + 1);
              if (secondSpace != -1) {
                url = header.substring(firstSpace + 1, secondSpace);
              }
            }

            // Handle different URLs based on parsed method and URL
            if (url == "/") {
              handleRoot(client);
            } else if (url.startsWith("/capture_images")) {
              int delaySeconds = 5;
              int pos = url.indexOf("delay=");
              if (pos != -1) {
                // Extract delay value from URL query parameter
                String delayStr = url.substring(pos + 6);
                int endOfDelay = delayStr.indexOf("&"); // In case of other parameters
                if (endOfDelay != -1) {
                  delayStr = delayStr.substring(0, endOfDelay);
                }
                delaySeconds = delayStr.toInt();
                if (delaySeconds < 1) delaySeconds = 1;
                if (delaySeconds > 60) delaySeconds = 60;
              }
              handleCaptureImages(client, delaySeconds);
            } else if (url.startsWith("/get_analysis")) {
              String promptType = "";
              int typePos = url.indexOf("type=");
              if (typePos != -1) {
                // Extract prompt type from URL query parameter and URL decode it
                promptType = urlDecode(url.substring(typePos + 5));
              }
              handleGetAnalysis(client, promptType);
            } else if (url == "/reset_data") {
              handleResetData(client);
            }
            break; // Exit header reading loop after handling the request
          } else {
            currentLine = ""; // Clear currentLine for the next header line
          }
        } else if (c != '\r') {
          currentLine += c; // Add character to currentLine
        }
      }
    }
    client.stop(); // Close the connection after handling the request
  }
}

// ============ HTTP Handlers ============

// Serves the initial HTML page with embedded JavaScript
void handleRoot(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close"); // Close connection after sending HTML
  client.println(); // Blank line marks end of header

  // Embedded HTML directly in the C++ code using R"rawliteral(...)rawliteral"
  client.print(R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-CAM AI Monitor</title>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; }
    .container { max-width: 600px; margin: auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0, 0, 0, 0.1); }
    h1 { text-align: center; color: #333; }
    form { margin-top: 20px; }
    label { display: block; margin-bottom: 5px; font-weight: bold; }
    input[type='number'] { width: calc(100% - 22px); padding: 10px; margin-bottom: 10px; border: 1px solid #ddd; border-radius: 4px; }
    button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; width: 100%; }
    button:hover { background-color: #45a049; }
    .status { margin-top: 20px; padding: 10px; border: 1px solid #ccc; background-color: #e9e9e9; border-radius: 4px; }
    .prompt-section { margin-top: 30px; border-top: 1px solid #eee; padding-top: 20px; }
    .prompt-option { margin-bottom: 10px; }
    .response-box { border: 1px solid #007bff; padding: 15px; margin-top: 15px; border-radius: 5px; background-color: #e0f7fa; }
    .response-box h3 { color: #0056b3; margin-top: 0; }
    .reset-button { background-color: #dc3545; margin-top: 20px; }
    .reset-button:hover { background-color: #c82333; }
    #prompt-selection, #responses { display: none; } /* Initially hidden sections */
    .image-display { display: flex; justify-content: space-around; margin-top: 20px; flex-wrap: wrap; }
    .image-display img { max-width: 48%; height: auto; border: 1px solid #ddd; margin: 5px; box-sizing: border-box; }
    #image1, #image2 { display: none; } /* Hide images initially */
  </style>
</head>
<body>
  <div class='container'>
    <h1>ESP32-CAM AI Monitor</h1>
    <div id='status' class='status'></div>

    <form id='capture-form'>
      <label for='delay'>Delay between images (seconds):</label>
      <input type='number' id='delay' name='delay' value='5' min='1' max='60'>
      <button type='submit'>Capture Images</button>
    </form>

    <div id='prompt-selection' class='prompt-section'>
      <h2>Images Captured! Choose analysis type:</h2>
      <div class="image-display">
        <img id="image1" src="" alt="First Captured Image">
        <img id="image2" src="" alt="Second Captured Image">
      </div>
      <form id='analysis-form'>
        <div class='prompt-option'><input type='checkbox' name='prompt_type' value='alert' id='alert'><label for='alert'> Immediate Alert</label></div>
        <div class='prompt-option'><input type='checkbox' name='prompt_type' value='formal' id='formal'><label for='formal'> Formal Analytical Report</label></div>
        <div class='prompt-option'><input type='checkbox' name='prompt_type' value='friendly' id='friendly'><label for='friendly'> Friendly Home User Report</label></div>
        <button type='submit'>Get Analysis</button>
      </form>
      <form id='reset-form'>
        <button class='reset-button' type='submit'>Reset</button>
      </form>
    </div>

    <div id='responses'></div>
  </div>

  <script>
    const statusDiv = document.getElementById('status');
    const captureForm = document.getElementById('capture-form');
    const promptSelectionDiv = document.getElementById('prompt-selection');
    const analysisForm = document.getElementById('analysis-form');
    const resetForm = document.getElementById('reset-form');
    const responsesDiv = document.getElementById('responses');
    const image1Element = document.getElementById('image1');
    const image2Element = document.getElementById('image2');

    // Function to update the status message displayed on the page
    function updateStatus(message, isError = false) {
      statusDiv.innerHTML = '<div class="status" style="color:' + (isError ? 'red' : 'green') + ';">' + message + '</div>';
    }

    // Function to toggle visibility of sections after image capture
    function showPromptSelection(img1Base64, img2Base64) {
      captureForm.style.display = 'none'; // Hide capture form
      promptSelectionDiv.style.display = 'block'; // Show prompt selection
      responsesDiv.innerHTML = ''; // Clear any previous responses
      responsesDiv.style.display = 'block'; // Show responses container
      
      // Update image sources and make them visible
      image1Element.src = 'data:image/jpeg;base64,' + img1Base64;
      image2Element.src = 'data:image/jpeg;base64,' + img2Base64;
      image1Element.style.display = 'block';
      image2Element.style.display = 'block';

      // Uncheck all prompt types on showing the section for new analysis
      analysisForm.querySelectorAll('input[type="checkbox"]').forEach(checkbox => {
        checkbox.checked = false;
      });
    }

    // Function to reset the UI to its initial state
    function resetPage() {
      captureForm.style.display = 'block'; // Show capture form
      promptSelectionDiv.style.display = 'none'; // Hide prompt selection
      responsesDiv.innerHTML = ''; // Clear responses
      responsesDiv.style.display = 'none'; // Hide responses container
      image1Element.src = ''; // Clear image sources
      image2Element.src = '';
      image1Element.style.display = 'none'; // Hide images
      image2Element.style.display = 'none';
      updateStatus('Ready to capture new images.'); // Set initial status
    }

    // Handle Capture Images form submission (AJAX request)
    captureForm.addEventListener('submit', async (event) => {
      event.preventDefault(); // Prevent default form submission (page reload)
      const delay = parseInt(document.getElementById('delay').value, 10); // Get delay value as integer
      
      updateStatus('Capturing first image...'); // Initial status update
      
      try {
        // Use setTimeout to update status during the delay
        setTimeout(() => {
          updateStatus(`Waiting ${delay} seconds for second image...`);
        }, 2000); // Give a moment for the first image capture message to show

        setTimeout(() => {
          updateStatus('Capturing second image...');
        }, (delay * 1000) + 1000); // Update before the actual second capture finishes on ESP32

        // Send fetch request to ESP32 for image capture
        const response = await fetch('/capture_images?delay=' + delay);
        const data = await response.json(); // Expecting JSON response from ESP32

        if (response.ok && data.success) { // Check if HTTP status is OK and success flag is true
          updateStatus('Images captured successfully! Now choose analysis type(s).');
          // Pass base64 image data to showPromptSelection
          showPromptSelection(data.img1_base64, data.img2_base64); 
        } else {
          // Display error message if capture failed
          updateStatus('Error capturing images: ' + (data.message || 'Unknown error'), true);
          console.error('Capture error:', data.message);
        }
      } catch (error) {
        // Catch network or parsing errors
        updateStatus('Network error during capture: ' + error.message, true);
        console.error('Fetch error:', error);
      }
    });

    // Handle Analysis form submission (AJAX request for each selected prompt)
    analysisForm.addEventListener('submit', async (event) => {
      event.preventDefault(); // Prevent default form submission
      // Get all selected prompt types
      const selectedPrompts = Array.from(analysisForm.querySelectorAll('input[name="prompt_type"]:checked')).map(cb => cb.value);

      if (selectedPrompts.length === 0) {
        updateStatus('Please select at least one analysis type.', true);
        return;
      }

      updateStatus('Sending images for analysis...');
      responsesDiv.innerHTML = ''; // Clear previous responses before new ones

      // Loop through each selected prompt type and send a request
      for (const promptType of selectedPrompts) {
        try {
          // Send fetch request to ESP32 for AI analysis
          const response = await fetch('/get_analysis?type=' + encodeURIComponent(promptType), { method: 'POST' });
          const rawResponseText = await response.text(); // Get raw text, as API response is JSON string

          if (response.ok) {
            updateResponse(promptType, rawResponseText); // Process and display the response
          } else {
            // Handle HTTP errors from the ESP32 server
            updateResponse(promptType, 'HTTP Error ' + response.status + ': ' + rawResponseText, true);
          }
        } catch (error) {
          // Catch network or other errors during API call
          updateResponse(promptType, 'Network error during API call: ' + error.message, true);
          console.error('API call error for ' + promptType + ':', error);
        }
      }
      updateStatus('Analysis complete. See responses below.');
    });

    // Handle Reset form submission (AJAX request)
    resetForm.addEventListener('submit', async (event) => {
      event.preventDefault(); // Prevent default form submission
      updateStatus('Resetting data...');
      try {
        // Send fetch request to ESP32 to clear image buffers
        const response = await fetch('/reset_data', { method: 'POST' });
        const data = await response.json(); // Expecting JSON response

        if(response.ok && data.success) {
            updateStatus('System reset. Ready for new capture.');
            resetPage(); // Reset UI to initial state
        } else {
            updateStatus('Error during reset: ' + (data.message || 'Unknown error'), true);
        }
      } catch (error) {
        // Catch network or parsing errors
        updateStatus('Network error during reset: ' + error.message, true);
        console.error('Reset error:', error);
      }
    });

    // Function to parse AI API response and display only the content
    function updateResponse(promptType, rawResponseText) { 
      let title = '';
      if (promptType === 'alert') title = 'Immediate Alert';
      else if (promptType === 'formal') title = 'Formal Analytical Report';
      else if (promptType === 'friendly') title = 'Friendly Home User Report';
      
      let displayContent = "Error: Could not parse API response or extract content."; // Default error message
      let isError = true; // Assume error until content is successfully extracted

      try {
        const jsonResponse = JSON.parse(rawResponseText); // Attempt to parse the raw text as JSON
        
        // --- CORRECTED JSON PARSING PATH ---
        if (jsonResponse.choices && jsonResponse.choices[0] && 
            jsonResponse.choices[0].message && jsonResponse.choices[0].message.content) {
          
          displayContent = jsonResponse.choices[0].message.content; // Extract the desired content
          isError = false; // Content successfully extracted
        } 
        // --- END OF CORRECTED JSON PARSING PATH ---
        
        else if (jsonResponse.error && jsonResponse.error.message) {
            // If the API returned a top-level error message (e.g., rate limit)
            displayContent = "API Error: " + jsonResponse.error.message;
            isError = true;
        } else {
            // If JSON is valid but the expected content path is missing or different
            displayContent = "API response format unexpected or content path missing. Raw response: " + rawResponseText;
            isError = true;
        }
      } catch (e) {
        // If rawResponseText is not valid JSON (parsing error)
        displayContent = "Invalid JSON response from API. Raw response: " + rawResponseText;
        isError = true;
        console.error("JSON parsing error for type " + promptType + ":", e, rawResponseText);
      }

      // Replace newlines with <br> for HTML display and escape quotes
      displayContent = displayContent.replace(/\n/g, '<br>');
      displayContent = displayContent.replace(/"/g, '&quot;'); // Escape double quotes for HTML attribute/content safety

      // Append the new response box to the responsesDiv
      responsesDiv.innerHTML += '<div class="response-box" style="border-color:' + (isError ? 'red' : '#007bff') + '; background-color:' + (isError ? '#ffebee' : '#e0f7fa') + ';"><h3>' + title + '</h3><p>' + displayContent + '</p></div>';
    }

    // Initial setup when the page loads
    document.addEventListener('DOMContentLoaded', () => {
      resetPage(); // Set the initial UI state
    });

  </script>
</body>
</html>
)rawliteral");
}

// Handles image capture and stores base64 strings in global variables
void handleCaptureImages(WiFiClient &client, int delaySeconds) {
  Serial.println("Received /capture_images request.");
  Serial.println("Capturing first image...");
  img1_base64 = captureImage();
  if (img1_base64.length() == 0) {
    Serial.println("Error capturing first image!");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println();
    client.println("{\"success\":false, \"message\":\"Failed to capture first image\"}");
    return;
  }
  Serial.println("First image captured. Waiting " + String(delaySeconds) + " seconds...");
  
  // No client update here, let the client-side setTimeout handle perceived delay.
  // The ESP32 actually delays here.
  delay(delaySeconds * 1000); // Delay between captures
  
  Serial.println("Capturing second image...");
  img2_base64 = captureImage();
  if (img2_base64.length() == 0) {
    Serial.println("Error capturing second image!");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println();
    client.println("{\"success\":false, \"message\":\"Failed to capture second image\"}");
    return;
  }
  Serial.println("Both images captured.");

  // Send success JSON response back to client, including the image data
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println();
  // Include image data directly in the JSON response
  client.print("{\"success\":true, \"message\":\"Images captured successfully\", \"img1_base64\":\"");
  client.print(img1_base64);
  client.print("\", \"img2_base64\":\"");
  client.print(img2_base64);
  client.println("\"}");
}

// Handles AI analysis requests for a specific prompt type
void handleGetAnalysis(WiFiClient &client, String promptType) {
  Serial.println("Received /get_analysis request for type: " + promptType);
  if (img1_base64.length() == 0 || img2_base64.length() == 0) {
    Serial.println("Error: Images not captured yet for analysis.");
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/plain");
    client.println();
    client.println("Error: Images not captured. Please capture images first.");
    return;
  }

  String prompt_to_send = "";
  if (promptType == "alert") {
    prompt_to_send = "URGENT ALERT: Immediate action required. Analyze and identify critical changes between the two images that demand immediate attention. Focus on potential threats, security breaches, or critical system failures. Respond with a very concise, actionable alert message.";
  } else if (promptType == "formal") {
    prompt_to_send = "Generate a comprehensive, formal analytical report comparing the two provided images. Detail all observed changes, including subtle differences in lighting, object positions, and the operational status of any visible devices. Analyze potential causes and implications of these changes. Present findings in a structured, objective manner.";
  } else if (promptType == "friendly") {
    prompt_to_send = "Hey there! I've taken two pictures of your room a little while apart. Could you tell me in a friendly, easy-to-understand way if anything looks different? Like, did someone move something, or is a light on that wasn't before? Just a quick summary for a home user, please!";
  } else {
    Serial.println("Unknown prompt type: " + promptType);
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/plain");
    client.println();
    client.println("Error: Invalid prompt type.");
    return;
  }

  prompt_to_send += base_analysis_prompt; // Append the standard analysis request to the chosen prompt

  Serial.println("Sending to API for: " + promptType);
  String apiResponse = sendToAPI(prompt_to_send, img1_base64, img2_base64);
  Serial.println("API Raw Response for " + promptType + ": " + apiResponse); // Log full raw response for debugging

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain"); // Send raw API response as plain text, client-side JS will parse
  client.println();
  client.println(apiResponse); // Send the raw API response back to the client
}
// ============ Helper Functions ============
void flushCameraBuffer() {
  for (int i = 0; i < 3; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(50);
  }
}
// Handles resetting the stored image data
void handleResetData(WiFiClient &client) {
  Serial.println("Received /reset_data request. Clearing image buffers.");
  img1_base64 = "";
  img2_base64 = "";
  flushCameraBuffer();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println();
  client.println("{\"success\":true, \"message\":\"Data reset successfully\"}");
}


// Captures a single image from the ESP32-CAM and returns it as a Base64 encoded string
String captureImage() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    if (!fb) {
  Serial.println("Camera capture failed. Possibly frame buffer not released or RAM issue.");
} else {
  Serial.println("Camera capture OK. Image size: " + String(fb->len));
}
    return "";
  }
  String img_base64_str = base64::encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return img_base64_str;
}

// Sends the images and prompt to the AI API (Avalapis/Gemini)
String sendToAPI(String prompt_text, String img1, String img2) {
  HTTPClient http;
  http.begin(api_url);
  http.setTimeout(30000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + api_key);

  // Construct the JSON payload for the multimodal request to Gemini API
  String payload =
    "{"
    "\"model\":\"" + String(model) + "\","
    "\"messages\":["
    "{"
    "\"role\":\"system\","
    "\"content\":\"You are an AI monitoring assistant. Compare two images and respond with a clear sentence describing any differences. Adhere strictly to the requested tone and detail level.\""
    "},"
    "{"
    "\"role\":\"user\","
    "\"content\": ["
    "{"
    "\"type\": \"text\","
    "\"text\": \"" + prompt_text + "\""
    "},"
    "{"
    "\"type\": \"image_url\","
    "\"image_url\": {"
    "\"url\": \"data:image/jpeg;base64," + img1 + "\""
    "}"
    "},"
    "{"
    "\"type\": \"image_url\","
    "\"image_url\": {"
    "\"url\": \"data:image/jpeg;base64," + img2 + "\""
    "}"
    "}"
    "]"
    "}"
    "]"
    "}";

  Serial.println("Sending API request...");
  int httpCode = http.POST(payload); // Perform the HTTP POST request
  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);
  String response = "";
  if (httpCode > 0) {
    response = http.getString(); // Get the response payload
  } else {
    response = "Error: " + http.errorToString(httpCode) + " (Code: " + String(httpCode) + ")";
  }
  http.end(); // Close the connection
  return response;
}

// Basic URL decoding for query parameters (e.g., handling spaces encoded as '+')
String urlDecode(String input) {
  String decodedString = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < input.length(); i++) {
    c = input.charAt(i);
    if (c == '+') {
      decodedString += ' ';
    } else if (c == '%') {
      i++;
      code0 = input.charAt(i);
      i++;
      code1 = input.charAt(i);
      code0 = (code0 >= '0' && code0 <= '9' ? code0 - '0' : code0 - 'A' + 10);
      code1 = (code1 >= '0' && code1 <= '9' ? code1 - '0' : code1 - 'A' + 10);
      decodedString += (char)(code0 * 16 + code1);
    } else {
      decodedString += c;
    }
  }
  return decodedString;
}