# Fuzzing

Each fuzz target covers one parser, codec, format, or state-machine entry point. Minimized reproducers become regression tests.

Current deterministic smoke targets:

- `vmem_m3_buffer_chain_fuzz`
- `vmem_m5_allocator_corruption_fuzz`
