// Get references to HTML elements
const delayForm = document.getElementById('delayForm');
const delayInput = document.getElementById('delay');
const startCaptureBtn = document.getElementById('startCaptureBtn');

const statusDiv = document.getElementById('status');
const statusMessage = document.getElementById('statusMessage');

const delaySection = document.getElementById('delaySection');
const imageDisplaySection = document.getElementById('imageDisplaySection');
const promptSection = document.getElementById('promptSection');
const image1Elem = document.getElementById('image1');
const image2Elem = document.getElementById('image2');
const promptTextarea = document.getElementById('prompt');
const analyzeBtn = document.getElementById('analyzeBtn');

const resultsSection = document.getElementById('resultsSection');
const gptResponsePre = document.getElementById('gptResponse');

const promptTypeSelect = document.getElementById('promptType');
const outputFormatCheckboxes = document.querySelectorAll('input[name="outputFormat"]');

let capturedImage1Base64 = '';
let capturedImage2Base64 = '';
let currentDelay = 0;

// Define different prompt templates for the *user query* part
const promptTemplates = {
    'default': `Analyze the two images captured with a {delay}-second delay. Describe any noticeable changes in objects, lighting, device states (ON/OFF), or presence of people/animals.`,
    'short_alert': `Quickly analyze the two images ({delay}-second delay). Identify any critical or immediate changes that require attention. Focus on security, safety, or unexpected operational shifts.`,
    'formal_report': `Conduct a detailed comparative analysis of the two images captured with a {delay}-second delay. Document all observed discrepancies concerning object positions, environmental lighting, operational status of devices, and instances of human or animal presence.`,
    'friendly_message': `Hey there! I took two pictures {delay} seconds apart. Can you spot anything new or different? Maybe a light turned on, something moved, or a new visitor appeared? Keep it chill!`,
    'complete_report_comparison': `Perform a comprehensive side-by-side comparison of the two provided images, taken {delay} seconds apart. Detail all detected changes including but not limited to: shifts in object positions, changes in luminosity or light sources (on/off), presence or absence of individuals/animals, and any other notable environmental alterations. Provide a structured report outlining 'Before' and 'After' states for key elements, followed by a summary of 'Differences Detected'.`
};

// Define instructions for the *LLM's response format*
// These will be appended to the main prompt based on user selection.
const outputFormatInstructions = {
    'short_alert': `\n\n---SHORT ALERT---\nPlease provide a concise, urgent alert (max 2 sentences) summarizing the most critical change, if any. Example: "Warning: Printer failed to restart after outage. Manual check required."`,
    'formal_report': `\n\n---FORMAL REPORT---\nGenerate a formal, detailed report. Include sections for: "Initial State (Image 1)", "Current State (Image 2)", "Observed Changes", and "Recommendations".`,
    'friendly_message': `\n\n---FRIENDLY MESSAGE---\nGive me a casual, friendly message describing the changes, as if talking to a friend.`,
};


/**
 * @brief Function to update status message dynamically.
 * @param message The message string to display.
 */
function updateStatus(message) {
    statusMessage.textContent = message;
    statusDiv.classList.remove('hidden');
}

/**
 * @brief Updates the prompt textarea with the selected template and appends output format instructions.
 */
function updatePromptTextarea() {
    const selectedPromptType = promptTypeSelect.value;
    let basePrompt = promptTemplates[selectedPromptType] || promptTemplates['default'];
    
    // Replace {delay} placeholder with the current delay value
    basePrompt = basePrompt.replace('{delay}', currentDelay);

    let finalPrompt = basePrompt;

    // Append instructions for desired output formats
    const selectedOutputFormats = Array.from(outputFormatCheckboxes)
                                       .filter(checkbox => checkbox.checked)
                                       .map(checkbox => checkbox.value);

    // If multiple output formats are selected, append their instructions to the main prompt.
    // If none are selected, the LLM will just respond based on the basePrompt.
    if (selectedOutputFormats.length > 0) {
        finalPrompt += "\n\n---DESIRED OUTPUT FORMATS---";
        selectedOutputFormats.forEach(format => {
            if (outputFormatInstructions[format]) {
                finalPrompt += outputFormatInstructions[format];
            }
        });
        finalPrompt += "\n--------------------------\nPlease provide all requested formats in your single response, clearly labeled by their headers.";
    }

    promptTextarea.value = finalPrompt;
}

// --- Event Listener for Prompt Type Selection ---
if (promptTypeSelect) {
    promptTypeSelect.addEventListener('change', updatePromptTextarea);
}

// --- Event Listeners for Output Format Checkboxes ---
outputFormatCheckboxes.forEach(checkbox => {
    checkbox.addEventListener('change', updatePromptTextarea);
});

// --- Step 1: Handle Delay Form Submission ---
delayForm.addEventListener('submit', async function(event) {
    event.preventDefault(); // Prevent default form submission

    currentDelay = delayInput.value;
    // Update the prompt text area with the current delay based on selected type
    updatePromptTextarea(); // Call once initially to set the prompt based on default/selected values

    // Hide previous results and show status
    resultsSection.classList.add('hidden');
    imageDisplaySection.classList.add('hidden');
    promptSection.classList.add('hidden');
    
    updateStatus('Initiating image capture process...');
    startCaptureBtn.disabled = true; // Disable button during processing

    try {
        // Call ESP32 to capture images
        const url = `/capture_images?delay=${currentDelay}`;
        const response = await fetch(url);
        const data = await response.json(); // Expect JSON response with image data

        if (data.status === 'success') {
            capturedImage1Base64 = data.image1;
            capturedImage2Base64 = data.image2;

            // Display images
            image1Elem.src = `data:image/jpeg;base64,${capturedImage1Base64}`;
            image2Elem.src = `data:image:jpeg;base64,${capturedImage2Base64}`;

            // Transition UI to Step 2
            delaySection.classList.add('hidden');
            imageDisplaySection.classList.remove('hidden');
            promptSection.classList.remove('hidden'); // Show prompt input after images
            updateStatus('Images captured. Now enter your prompt and click Analyze.');
            statusDiv.classList.add('hidden'); // Hide spinner for this step
            analyzeBtn.disabled = false; // Enable analyze button
        } else {
            updateStatus(`Error capturing images: ${data.message || 'Unknown error occurred.'}`);
            statusDiv.classList.add('bg-red-50', 'text-red-800'); // Style error
            delaySection.classList.remove('hidden'); // Show delay section again
        }

    } catch (error) {
        updateStatus(`Failed to connect to ESP32 for capture: ${error.message}`);
        statusDiv.classList.add('bg-red-50', 'text-red-800');
        delaySection.classList.remove('hidden'); // Show delay section again
        console.error('Fetch error for capture:', error);
    } finally {
        startCaptureBtn.disabled = false; // Re-enable button
    }
});

// --- Step 2: Handle Analyze Button Click ---
analyzeBtn.addEventListener('click', async function() {
    const promptText = promptTextarea.value;
    if (!promptText) {
        // Using a custom message box instead of alert() as per instructions
        // This is a simple placeholder for a custom UI modal
        const messageBox = document.createElement('div');
        messageBox.innerHTML = `
            <div class="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50">
                <div class="bg-white p-6 rounded-lg shadow-xl text-center">
                    <p class="text-lg font-semibold mb-4">Please enter a prompt for analysis.</p>
                    <button class="bg-blue-500 text-white px-4 py-2 rounded-md hover:bg-blue-600" onclick="this.parentNode.parentNode.remove()">OK</button>
                </div>
            </div>
        `;
        document.body.appendChild(messageBox);
        return;
    }

    updateStatus('Sending images and prompt to GPT for analysis...');
    analyzeBtn.disabled = true; // Disable button during processing
    resultsSection.classList.add('hidden'); // Hide previous results

    try {
        // Prepare data for GPT analysis
        const postData = {
            image1: capturedImage1Base64,
            image2: capturedImage2Base64,
            prompt: promptText
        };

        // Call ESP32 to analyze with GPT
        const response = await fetch('/analyze_gpt', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(postData)
        });
        const data = await response.json(); // Expect JSON response with GPT result

        statusDiv.classList.add('hidden'); // Hide status spinner
        analyzeBtn.disabled = false; // Re-enable button

        if (data.status === 'success') {
            gptResponsePre.textContent = data.gpt_response;
            resultsSection.classList.remove('hidden');
            updateStatus('Analysis complete!');
            // Optionally, uncomment to reset to step 1 for a new session
            // delaySection.classList.remove('hidden');
            // imageDisplaySection.classList.add('hidden');
            // promptSection.classList.add('hidden');
        } else {
            gptResponsePre.textContent = `Error during analysis: ${data.message || 'Unknown error occurred.'}`;
            resultsSection.classList.remove('hidden');
            resultsSection.classList.add('bg-red-50', 'text-red-800'); // Style error
        }

    } catch (error) {
        updateStatus(`Failed to connect to ESP32 for analysis: ${error.message}`);
        statusDiv.classList.add('bg-red-50', 'text-red-800');
        analyzeBtn.disabled = false;
        resultsSection.classList.remove('hidden');
        resultsSection.classList.add('bg-red-50', 'text-red-800');
        gptResponsePre.textContent = `Failed to connect to ESP32 for analysis: ${error.message}`;
        console.error('Fetch error for analysis:', error);
    }
});

// Initialize UI state
statusDiv.classList.add('hidden');
imageDisplaySection.classList.add('hidden');
promptSection.classList.add('hidden');
resultsSection.classList.add('hidden');

// Set initial prompt text when page loads
updatePromptTextarea();
