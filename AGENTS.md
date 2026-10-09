# Workspace policies

## Memory budget

The container has a 4 GB memory budget. The enforced cgroup limit is 4 GiB (4,294,967,296 bytes). Keep workloads within this limit and leave room for the running application and tools.

- Use at most two build jobs by default, such as `cmake --build build --parallel 2`. Use one job for memory-intensive compilation or linking.
- Run browser tests with one worker.
- Run builds, browser tests, and other memory-intensive workloads sequentially.
- Check cgroup memory usage before sustained heavy work. Reduce concurrency or workload size if memory pressure rises; do not retry an out-of-memory workload unchanged.
- Do not choose concurrency from the CPU count alone or use unrestricted parallel builds.

## Dockerfile

Do not modify, replace, regenerate, or reformat `Dockerfile` unless the user explicitly asks for a Dockerfile change. Read-only inspection is allowed.

## Git commits

The user authorizes commits as `Codex`. Use the repository-local identity `Codex <codex@local.invalid>` for these commits; do not change global Git configuration. The email is a placeholder for the local agent identity.

## Remote publishing

The user manages Git pushes from macOS and decides when to update GitHub Pages. Keep the Pages workflow manually triggered with `workflow_dispatch`. Do not push, create a GitHub repository, or trigger a deployment unless the user explicitly requests that action.

## Portable kernel boundary

The C++ abstract core, compiler, session, and geometric interpreter form a reusable portable kernel. Custom renderers and controllers must be able to consume its public native or WASM APIs independently of the reference application.

- Keep Three.js, React, Vite, DOM access, camera controls, input gestures, and animation/event-loop scheduling outside the kernel.
- Keep abstract rules independent of geometric data within the kernel. Renderers consume interpreter assets and frames; controllers submit revision-checked abstract requests.
- Maintain the independent native/WASM packaging and consumer examples when changing public APIs. Document ownership, exact serialization, errors, capabilities, and buffer layouts in `docs/kernel-api.md`.
- Treat API/buffer versions separately from puzzle schemas and semantic digests. Update public declarations and appropriate independent-consumer checks with contract changes.
