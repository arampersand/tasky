Tasky Walkthrough: Modernization & UX Overhaul
This update transforms the TaskyCircular experience with fluid animations, intuitive navigation, and refined input logic.

Key Changes
1. Navigation & Structure
Smart Boot: The device now boots directly into the QR Code screen, encouraging immediate connection to the web app.
New Cycle: QR -> Timer -> Tasks -> Streak -> Clock -> Settings -> (Loop).
Independent QR View: The QR code is now a primary screen, featuring a clear "Scan to Start" prompt.
2. Animations & Visuals
Typewriter Splash: A premium startup animation types out "Tasky" character-by-character.
Staggered Entries: Lists (Tasks, Settings) and Grids (Streak) now animate in with a fluid staggered slide-up effect.
Reverse Timer Ring: The progress ring now fills Counter-Clockwise (Right-to-Left) for a unique visual signature.
WiFi Legibility: SSID and Password fonts have been optimized (9pt) to fit the circular display without overlapping.
3. Input Logic Refinements
Timer Control:
Short Click: Start/Pause (Instant response).
Long Press: Reset Timer (Prevents accidental resets).
Streak Interaction:
Navigation is purely via Encoder rotation.
Click to toggle "Edit Mode" for the selected day.
Tasks Interaction:
Long Press on a task opens a new Detail View overlay for reading full descriptions.
Empty State: Displays a helpful "Scan QR code to create tasks" message if the list is empty.
4. Settings Simplification
Removed "Sound" Option: Simplified menu as sound is logically tied to LED feedback.
Theme Toggle: Moved to the end of the list for easy access.
Verification Checklist
Feature	Action	Expected Result
Boot	Restart Device	"Tasky" types out -> QR Code appears with "Scan to Start".
Nav	Rotate Encoder	Cycles through QR -> Timer -> Tasks -> Streak -> Clock -> Settings.
Timer	Short Click	Starts/Pauses countdown.
Timer	Long Press	Resets timer to default.
Tasks	Long Press	Opens detail text overlay for the selected task.
Streak	Rotate / Click	Rotate moves cursor. Click enters edit mode (green box).
Animation	Enter Settings	Items slide up one by one.