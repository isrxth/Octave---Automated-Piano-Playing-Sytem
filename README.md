###### 🎹 Project Octave

###### 

###### By Team WeBeSyncing

###### 

###### Project Octave is a high-speed, polyphonic, CNC-style robotic piano playing system. Utilizing a multi-microcontroller architecture, ESP-NOW wireless synchronization, and a custom lookahead kinematic engine, Octave translates standard .mid files into precise physical movements, playing an acoustic piano with superhuman speed and accuracy.

###### ✨ Key Features

###### 

###### &#x20;   Polyphonic "Cluster" Engine: Groups simultaneous MIDI notes and calculates a "Center of Gravity" offset, allowing a rigid solenoid block to perfectly strike multiple keys at once.

###### 

###### &#x20;   Anti-Flood Staggering: Automatically spaces out simultaneous polyphonic WebSocket commands by 15ms to prevent Arduino Uno serial buffer overflow and dropped notes.

###### 

###### &#x20;   Lookahead Background Pre-Positioning: The Web UI calculates future moves and silently drives the stepper carriages into position during the natural musical rests, eliminating mechanical latency.

###### 

###### &#x20;   Strict Hardware Interlock: A closed-loop feedback system using ESP-NOW guarantees that solenoids will never fire while the carriage is in transit, completely protecting the 3D-printed mounts from lateral shear forces.

###### 

###### &#x20;   Dynamic Velocity Control: Maps MIDI velocity data to hardware PWM duty cycles, allowing for expressive, dynamic play (Soft vs. Hard strikes), or strict binary toggling.

###### 

###### &#x20;   Asynchronous Web UI: A beautiful, dark-mode TailwindCSS web interface hosted directly on the ESP32. Features live BPM scaling, dynamic clearance tuning, and an interactive virtual keyboard.

###### 

###### &#x20;   Over-The-Air (OTA) Updates: Fully integrated ElegantOTA allows for flashing firmware to the embedded microcontrollers via Wi-Fi without dismantling the piano hardware.

###### 

###### 🧠 System Architecture

###### 

###### Octave uses a distributed computing model to ensure non-blocking, real-time performance.

###### 1\. The Master Node (ESP32)

###### 

###### &#x20;   Hosts the Web UI (test.html) via ESPAsyncWebServer.

###### 

###### &#x20;   Receives WebSocket commands from the user's browser.

###### 

###### &#x20;   Directly drives the Black Key Solenoids (S1 - S5) via hardware PWM.

###### 

###### &#x20;   Broadcasts MOVE commands to the Stepper Node over ESP-NOW.

###### 

###### &#x20;   Sends Serial commands to the Arduino Uno.

###### 

###### 2\. The Stepper Node (ESP32)

###### 

###### &#x20;   Listens for ESP-NOW packets and drives two NEMA 17 stepper motors using AccelStepper.

###### 

###### &#x20;   Motor 1 (White Carriage): Uses sliding-window nearest-neighbor logic to traverse the white keys.

###### 

###### &#x20;   Motor 2 (Black Carriage): Uses inverted, octave-locked kinematics to perfectly align with the irregular 2-and-3 black key clusters.

###### 

###### &#x20;   Reads physical limit switches and transmits AT\_TARGET packets back to the Master to unlock the software thread.

###### 

###### 3\. The Strike Node (Arduino Uno)

###### 

###### &#x20;   Receives ultra-fast Serial packets from the Master ESP32 via a logic level shifter.

###### 

###### &#x20;   Manages a non-blocking 40ms ballistic state machine for the 7 White Key Solenoids.

###### 

###### &#x20;   Utilizes a custom 1kHz Software PWM engine for digital pins to maintain a 23% low-current hold state, preventing coil burnout.

###### 

###### 🛠️ Hardware Requirements

###### 

###### &#x20;   2x ESP32 Development Boards

###### 

###### &#x20;   1x Arduino Uno R3

###### 

###### &#x20;   2x NEMA 17 Stepper Motors (w/ TMC2209 or A4988 Drivers)

###### 

###### &#x20;   12x 15N Push-Pull Solenoids

###### 

###### &#x20;   3D-Printed Carriages \& Solenoid Mounts

###### 

###### &#x20;   12V / 24V Power Supplies \& Logic Level Converters (HW-209)

###### 

###### 🛜 Changing the Wi-Fi Network Name (SSID)

###### 

###### By default, the Master ESP32 creates a hotspot named POCOF3. Because the Master and Stepper nodes must communicate, their network credentials must match perfectly.

###### 

###### &#x20;   Open solenoid\_esp.cpp (Master) and modify:

###### &#x20;   C++

###### 

###### &#x20;   const char\* hotspotSSID     = "Project\_Octave"; // Your new name

###### &#x20;   const char\* hotspotPassword = "00000000";       // Your new password

###### 

###### &#x20;   Open stepper\_esp.cpp (Stepper) and ensure lines 8 \& 9 match exactly.

###### 

###### &#x20;   Re-flash both ESP32s.

###### 

###### 🚀 Installation \& Setup

###### 1\. Flash the Firmware

###### 

###### &#x20;   Open the project in PlatformIO or the Arduino IDE.

###### 

###### &#x20;   Install the required libraries: ESPAsyncWebServer, AsyncTCP, AccelStepper, ElegantOTA.

###### 

###### &#x20;   Flash solenoid\_esp.cpp and stepper\_esp.cpp to your respective ESP32s.

###### 

###### &#x20;   Flash solenoid\_uno.ino to your Arduino Uno.

###### 

###### 2\. Connect to the UI

###### 

###### &#x20;   Power on the Master ESP32.

###### 

###### &#x20;   Connect your computer/tablet to the Octave Wi-Fi hotspot.

###### 

###### &#x20;   Open your web browser and navigate to the ESP32's IP address (default: http://192.168.4.1).

###### 

###### &#x20;   Enter the IP into the Master Control UI and click CONNECT WS.

###### 

###### 🎹 Polyphony Limits \& Hardware Constraints

###### 

###### While the software fully supports polyphonic MIDI playback, it is bound by the physical laws of the rigid 3D-printed solenoid blocks.

###### 

###### &#x20;   The Span Limit: The system can play multiple notes at the exact same time, but they must physically fit under the 7-solenoid white carriage or the 5-solenoid black carriage. \* The Outlier Drop: If a MIDI file demands a C3 and a C5 simultaneously, the carriage cannot stretch. The software's Cluster Engine will calculate a "Center of Gravity" to cover the densest cluster of notes and safely ignore the unreachable outliers to prevent the motors from destroying the mounts.

###### 

###### 🎯 Usage \& Tuning

###### 

###### Once connected to the UI, you can calibrate Octave to match your specific physical build:

###### 

###### &#x20;   White Home / Black Home: Adjust these steps to perfectly align the 3D-printed carriages over the physical C3 octave. (Note: Black Home uses inverted kinematics, so its home step is a high value like 2400).

###### 

###### &#x20;   Clearance (ms): The mechanical time allowed for the 15N solenoid springs to pull the metal slugs out of the keybed before the stepper motors are allowed to move. Lower this value for faster songs, but ensure the slugs clear the keys to avoid lateral dragging.

###### 

###### &#x20;   Target BPM: The live tempo scaler. You can speed up or slow down a loaded MIDI file in real-time.

###### 

###### 📝 License \& Credits

###### 

###### Developed by Team WeBeSyncing.

###### Built for the intersection of Embedded Systems, Mechanical Engineering, and Music.

