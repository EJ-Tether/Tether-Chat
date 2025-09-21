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

**Why the name?**
The name “Tether” reflects the intent: to tether an AI to its emerging personality — anchoring its sense of self and memory beyond transient sessions.
