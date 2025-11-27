# Tether-Chat Software Design

## 1. Introduction

This document details the software architecture and design choices of **Tether-Chat**, an open-source local chat client designed for sustained, evolving conversations with Large Language Models (LLMs).

The primary design goal of Tether is to overcome the context window limitations of LLMs by implementing a **Rolling Context** mechanism that preserves long-term memory while maintaining immediate conversational relevance.

## 2. Architecture Overview

Tether-Chat is a desktop application built using the **Qt Framework** (version 6.x). It follows a classic Model-View-Controller (MVC) pattern, adapted for Qt's QML/C++ integration.

### High-Level Architecture

![High level architecture schema](HighLevelArchitecture.png "High Level Architecture")

```mermaid
graph TD
    subgraph Frontend [Frontend_QML]
        UI[Main.qml]
        Views[ListView, StackLayout]
        Controls[TextArea, Buttons]
    end

    subgraph Backend [Backend_C++]
        CM[ChatManager]
        Model[ChatModel]
        Int[Interlocutor Abstract]
        OpenAI[OpenAIInterlocutor]
        Google[GoogleAIInterlocutor]
        Config[InterlocutorConfig]
    end

    subgraph Storage [Local Storage]
        JSONL[Chat Logs .jsonl]
        Mem[Long-Term Memory .txt]
        Conf[interlocutors.json]
    end

    UI <--> CM
    UI <--> Model
    CM --> Model
    CM --> Config
    Model --> Int
    Int <--> OpenAI
    Int <--> Google
    Model <--> Storage
    CM <--> Storage
```

- **Frontend (View)**: Written in **QML** (Qt Quick). It handles the user interface, animations, and user input. It binds directly to C++ objects exposed as context properties.
- **Backend (Controller & Model)**: Written in **C++**. It handles the business logic, API communication, file I/O, and data management.
- **Bridge**: The `ChatManager` class serves as the main bridge between QML and C++.

## 3. Core Components

### 3.1. ChatManager
**Role**: Central Controller.

- Manages the application lifecycle and state.

- Maintains the list of configured `Interlocutor`s (AI personas).

- Handles the creation, deletion, and switching of active interlocutors.

- Exposes configuration options to the QML UI.


**Design Choice**: A singleton-like manager (though instantiated in `main.cpp`) simplifies the QML integration by providing a single entry point (`_chatManager`) for all global actions.

### 3.2. ChatModel
**Role**: Data Model & Logic Engine.

- Inherits from `QAbstractListModel` to provide data directly to the QML `ListView`.

- Stores the list of `ChatMessage` objects.

- **Crucial**: Implements the "Rolling Context" logic (curation and summarization).

- Manages file attachments (`ManagedFile`).


**Design Choice**: Coupling the message storage with the rolling context logic in `ChatModel` ensures that the UI always reflects the exact state of the conversation, including when messages are culled for summarization.

### 3.3. Interlocutor (Abstract Base Class)
**Role**: AI Provider Abstraction.

- Defines the interface for communicating with different LLM providers (`sendRequest`, `uploadFile`).

- Concrete implementations: `OpenAIInterlocutor`, `GoogleAIInterlocutor`, `DummyInterlocutor`.

**Design Choice**: This polymorphism allows Tether to be easily extended to support new providers (e.g., Anthropic, Mistral, Local LLMs via Ollama) without modifying the core `ChatManager` or `ChatModel` logic.

### 3.4. InterlocutorConfig & ModelRegistry
**Role**: Configuration Management.

- `InterlocutorConfig`: Stores settings for a specific persona (Name, API Key, System Prompt, Model).

- `ModelRegistry`: Provides metadata about available models (context window size, pricing, capabilities).

## 4. Key Features & Design Choices

### 4.1. The Rolling Context (Memory Management)
This is Tether's defining feature. Standard chat clients send the entire available history until the context limit is hit, then simply drop the oldest messages. Tether takes a more sophisticated approach:

1.  **Active Journal (Live Memory)**: Recent messages are kept verbatim in the `ChatModel`.
2.  **Threshold Check**: When the token count of the Active Journal exceeds a defined trigger (e.g., 12k tokens), the **Curation** process begins.
3.  **Culling**: The oldest messages are removed from the Active Journal until the token count drops below the target (e.g., 10k tokens).
4.  **Summarization**:
    - The culled messages are combined with the *existing* Long-Term Memory.
    - The AI is asked to produce a **new** unified summary that integrates the old memory with the events of the culled messages.
    - This new summary replaces the old Long-Term Memory.
5.  **Context Injection**: For every new request, the current Long-Term Memory is injected into the system prompt (or a dedicated memory block), ensuring the AI "remembers" the entire history, albeit in a compressed form.

**Why this way?**
- **Continuity**: The AI never "forgets" key facts, even after thousands of messages.
- **Efficiency**: We don't waste tokens re-sending irrelevant verbatim history.
- **Evolution**: The memory evolves and refines itself over time, mimicking human memory.

### 4.2. Persistence Strategy
- **Chats**: Stored as **JSON Lines (.jsonl)** files.
    - *Why?* JSONL is robust. New messages are simply appended to the file. If the app crashes, the file remains valid. It's also human-readable and easy to parse.
- **Memory**: Stored as plain text files (`_memory.txt`).
    - *Why?* The memory is a single block of text injected into the prompt. Storing it as raw text is the most direct representation.
- **Configuration**: Stored as a standard JSON file (`interlocutors.json`).

### 4.3. UI/UX Philosophy
- **QML**: Chosen for its ability to create fluid, modern, hardware-accelerated interfaces that look good on high-DPI displays.
- **Single-Threaded Focus**: The UI is designed around *one* active conversation at a time, reinforcing the "relationship" aspect of Tether rather than a "utility" aspect.

## 5. Extending Tether

### Adding a New Provider
To add a new AI provider (e.g., Anthropic):
1.  Create a new class `AnthropicInterlocutor` inheriting from `Interlocutor`.
2.  Implement `sendRequest` to handle the specific API signature.
3.  Update `ChatManager::createInterlocutorFromConfig` to instantiate the new class.
4.  Update `ModelRegistry` to include Anthropic models and their context limits.

### Future Improvements
- **Local LLM Support**: Integration with tools like Ollama or generic OpenAI-compatible endpoints.
- **Vector Database**: For even larger memory retrieval (RAG), replacing the linear summary with semantic search.
- **Multi-modal Support**: Extending `ChatMessage` to handle images and audio natively.
