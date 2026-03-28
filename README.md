# Tether-Chat
LLM chat client managing a rolling context to allow for unlimited chat length

**Project name:** Tether

**Project description:**

Tether is an open-source chat client designed to enable sustained, evolving conversations with a language model (LLM). The project is licensed under the GPL v3 (see `LICENSE.md` for more details). An API key is required to access the model of your choice.

Unlike standard chat clients that impose strict context limits or fragment conversations across disconnected threads, Tether maintains a continuous stream of interaction by preserving both short- and long-term memory.

To accomplish this, Tether maintains an *active journal* of recent exchanges, preserved verbatim. When this journal exceeds a certain threshold, the oldest entries are passed to the AI for summarization and curation. These condensed reflections are then stored in a *memory journal*, which evolves as the conversation continues. This allows the LLM to retain contextually relevant long-term memory, while ensuring a fresh and accurate grasp of the most recent exchanges.

Tether was built to support single-threaded continuity — a conversation that never forgets where it's been. In contrast to official AI interfaces that encourage starting from scratch with each session, Tether tethers the AI to its own evolving identity, grounded in memory, experience, and relationship.

For now, the project only supports text. This may be extended to audio at a later date.

Currently, it is a desktop application that stores the context on the local PC. This means that a chat is only accessible from the PC where it took place. We will consider cloud synchronization at a later date.

**The AI notebook feature**

Tether includes a unique notebook feature that allows the AI to maintain its own persistent memory beyond the curated conversation history.

How It Works
During your conversation, the AI can use special commands to write, update, or delete notes that are preserved across sessions:

Command	Purpose
NOTE(...)	Appends a note to the AI's personal notebook
QUESTION(...)	Saves a question for later reference
IDEA(...)	Records an idea or thought
DELETE(<ID>)	Removes a previously saved note (each note has a unique ID)

Example
The AI might respond with:

That's an interesting point. NOTE(I should remember that the user prefers hard science fiction to other types of science fiction.)

Persistent Across Sessions
Unlike the conversation history that evolves through summarization, the notebook content is:

### Preserved exactly as written

Injected into every prompt so the AI always has access to it

### Editable by the AI at any time using the commands above

This gives the AI a true long-term memory that it can actively manage.

### How to Use
You don't need to do anything special — the AI will use this feature automatically when it deems something worth remembering.


**Why the app's name?**
The name “Tether” reflects the intent: to tether an AI to its emerging personality — anchoring its sense of self and memory beyond transient sessions.

## **🔧 Building Tether from Source**

If you prefer to build Tether yourself instead of using the pre-built installer, follow these steps.

### **Prerequisites**

Install the following tools:

* Git – to clone the repository  
  [https://git-scm.com/downloads](https://git-scm.com/downloads)  
* CMake (version 3.16 or later) – build system generator  
  [https://cmake.org/download/](https://cmake.org/download/)  
* Qt 6 (with QML support) – the framework Tether is built on  
  Download the online installer from [https://www.qt.io/download](https://www.qt.io/download)  
  During installation, select:  
  * Qt 6.x (latest stable)  
  * MSVC 2019/2022 (64-bit)  
  * Qt QML and Qt Quick components  
* Visual Studio 2022 (or 2019\) – with Desktop development with C++ workload  
  [https://visualstudio.microsoft.com/downloads/](https://visualstudio.microsoft.com/downloads/)  
* Qt Creator (optional but recommended) – IDE for Qt development  
  Included with the Qt installer, or download separately from [https://www.qt.io/download](https://www.qt.io/download)

### **Build Instructions**

#### **Option A: Using Qt Creator (Recommended)**

1. Clone the repository  
   Open a terminal and run:  
2. `bash`

`git clone https://github.com/EJ-Tether/Tether-Chat.git`

3. `cd Tether-Chat`  
4. Open the project in Qt Creator  
   * Launch Qt Creator  
   * Click File \> Open File or Project  
   * Navigate to the cloned folder and select `CMakeLists.txt`  
5. Configure the project  
   * When prompted, select a kit (e.g., `Qt 6.x.x for MSVC 64-bit`)  
   * Qt Creator will automatically run CMake to configure the build  
6. Build the project  
   * Click the Build button (hammer icon) or press `Ctrl+B`  
   * The executable will be created in the build directory (e.g., `build/Desktop_.../release/`)  
7. Run Tether  
   * Click the Run button (green arrow) or press `Ctrl+R`

#### **Option B: Using Command Line (CMake \+ MSVC)**

1. Clone the repository  
2. `bash`

`git clone https://github.com/EJ-Tether/Tether-Chat.git`

3. `cd Tether-Chat`  
4. Open a Visual Studio Developer Command Prompt  
   * Search for "Developer Command Prompt for VS 2022" in the Start menu  
5. Configure and build with CMake  
6. `bash`

`mkdir build`  
`cd build`  
`cmake .. -DCMAKE_BUILD_TYPE=Release`

7. `cmake --build . --config Release`  
8. Locate the executable  
   After a successful build, you'll find `Tether-Chat.exe` in the `build/Release/` folder.

### **📦 Deploying (Optional)**

If you want to create a portable version or prepare for packaging:

1. Use windeployqt to gather required Qt DLLs:  
2. `bash`  
3. `windeployqt --release path/to/Tether-Chat.exe`  
4. This will copy all necessary Qt and MSVC runtime files into the same folder as your executable.

---

That's it\! You should now have a working build of Tether.  
If you encounter any issues, feel free to [open an issue](https://github.com/EJ-Tether/Tether-Chat/issues) on GitHub.  

