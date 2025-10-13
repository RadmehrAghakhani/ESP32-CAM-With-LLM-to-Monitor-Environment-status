# ESP32-CAM AI Monitor  
### Intelligent Environmental Monitoring with Gemini 1.5 Flash  

---

## 🧠 Overview  
This project implements an **IoT-based AI monitoring system** using an **ESP32-CAM** module integrated with the **Gemini 1.5 Flash multimodal model** through the **Avalapis API**.  
The system captures two consecutive images from its environment and analyzes them using a **text-and-image prompt**. The Gemini model then provides context-aware interpretations in **three distinct modes**:
- **Alert** — concise emergency warnings  
- **Formal** — structured analytical reports  
- **Friendly** — brief, user-friendly explanations  

The core objective is to create an intelligent, low-cost, and efficient visual monitoring assistant capable of detecting environmental or behavioral changes.

---

## 🧩 Key Features  
- **ESP32-CAM Integration** for real-time image capture  
- **Multimodal AI Processing** via Gemini 1.5 Flash (Vision Model)  
- **Web Interface** for remote interaction  
- **Three Analysis Modes:**  
  - `alert` → immediate threat detection  
  - `formal` → detailed analytical reporting  
  - `friendly` → simplified daily summaries  
- **Efficient Token Usage** using prompt caching and low-resolution frames  
- **Fully Self-Contained Frontend** (HTML + CSS + JavaScript served directly from ESP32)  

---

## ⚙️ System Architecture  
The ESP32-CAM hosts a lightweight HTTP web server with the following endpoints:

| Endpoint | Function |
|-----------|-----------|
| `/` | Displays main control interface |
| `/capture_images?delay=<sec>` | Captures two images with user-defined delay |
| `/get_analysis?type=<mode>` | Sends both images and the selected analysis mode to the Gemini API |
| `/reset_data` | Clears image buffers and resets memory |

### Core Functions  
- `handleRoot()` → serves the web UI and embedded scripts  
- `handleCaptureImages()` → captures two images and encodes them as Base64  
- `handleGetAnalysis()` → constructs multimodal prompts and sends them to the API  
- `sendToAPI()` → handles POST requests with model payloads  
- `flushCameraBuffer()` → ensures fresh image capture by clearing residual frames  
- `handleResetData()` → resets all stored image data  

---

## 📸 Sample Scenarios  

| Scenario | Description | Mode | Example Output |
|-----------|--------------|------|----------------|
| **1. Human Detection** | Detects the presence of a person in an empty room | `formal` | “The second image shows a person present in the room, while the first image does not.” |
| **2. Lighting Change** | Identifies environmental lighting and camera angle variations | `formal` | “Lighting and camera orientation have changed significantly.” |
| **3. Suspicious Object** | Detects object manipulation or suspicious activity | `alert` / `friendly` | “Unauthorized presence detected; possible object handling.” |
| **4. Lighting Drop** | Detects major lighting loss or power outage | `alert` | “Significant lighting change detected; possible outage.” |

---

## 🧰 Technical Details  

### Model Selection  
The **Gemini 1.5 Flash** model was chosen for:
- High efficiency with limited API tokens  
- Reliable image-text interpretation  
- Fast response and low-latency operation  

**API Endpoint:**  
https://api.avalapis.ir/v1/chat/completions

**Data Format:**  
- JSON payload containing:
  - `model`: `gemini-1.5-flash`
  - `messages`: Array including a system role and user content (text + Base64 images)

---

## 🧪 Example Prompt
**Formal Mode:**
Generate a comprehensive analytical report comparing two images.
Identify lighting, object movement, or device status changes.
Present findings in a structured and objective manner.
**Friendly Mode:**
Hey! Can you tell me in a simple way what looks different between these two photos?
Maybe someone moved something or turned on a light?

---

## 👥 Team Members  
| Name | Student ID |
|------|-------------|
| Hosna Rajai | 4003613030 |
| Mobina Momenzadeh | 4003623036 |
| Rudmehr Alghakani | 4003663002 |

**Supervisor:** Dr. Behlouli  
**Semester:** 1403-04  

---

## 🔗 References  
- [ChatGPT](https://chatgpt.com/)  
- [Google Gemini](https://gemini.google.com/)  
- [Avalapis API](https://avalapis.ir/)  

---

## 🛠️ Future Work  
- Integration with motion sensors for real-time triggers  
- Expansion to multi-camera networks  
- On-device inference optimization using quantized AI models  
- Cloud dashboard for long-term environmental monitoring  

---

## 🧾 License  
This project is for **academic and educational purposes**.  
© 2025 Rudmehr Alghakani,  Hosna Rajai, Mobina Momenzadeh 
