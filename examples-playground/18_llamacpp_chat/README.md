# Tiny Local LLM

A JavaScript llama.cpp example that downloads an 88.2 MB public
SmolLM2-135M-Instruct Q2_K GGUF on first launch, stores it under `files/models/`,
and streams local chat through `sys.llamacpp`.

The chat screen uses a local copy of `ui-library.js` for its prompt field,
buttons, layout, and scrollable transcript. The copied library adds disabled
buttons, programmatic prompt focus, and follow-to-bottom scrolling so streamed
replies remain visible without preventing manual scrollback.

The app downloads the model in 4 MiB HTTP Range chunks, appending each chunk
directly to the sandboxed file instead of holding the complete GGUF in guest
memory. SmolLM2 has an embedded ChatML template and is instruction tuned, though
at 135M parameters it should still be treated as a compact demo assistant.

Model metadata:

- Base model: `HuggingFaceTB/SmolLM2-135M-Instruct` (Apache-2.0)
- Quantization: `bartowski/SmolLM2-135M-Instruct-GGUF`, Q2_K
- Size: 88,202,080 bytes
- SHA-256/LFS object: `741ad12b64088fedc17c33aacb22e48be1972ef36a39f03666dd68bd15614fb9`
