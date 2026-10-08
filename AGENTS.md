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
