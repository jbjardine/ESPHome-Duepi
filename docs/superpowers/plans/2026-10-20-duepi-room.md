# Duepi external room regulation implementation plan

> For agentic workers: use superpowers:subagent-driven-development. All product code is delegated. Read the task brief and its referenced spec/protocol; do not expand the scope.

**Goal:** optional external room control and optional PI in ESPHome, with HA sensor selection.

**Architecture:** opt-in external component; HA config-flow companion forwards measurements via standard text.set_value. Only the firmware computes RF2 targets.

**Tech stack:** C++17/ESPHome 2026.9.1, Python/Home Assistant, built-in ESPHome JSON, development-only native/HA tests.

**Spec:** ../specs/2026-10-20-duepi-room-design.md

**Contract:** ../specs/2026-10-20-duepi-room-protocol.md

## Global constraints

- Shared checkout /Users/jbjardine/cursor/ESPHome-Duepi; branch codjardine/remote-room-regulation; push only origin, never upstream. No worktree or per-agent branch.
- At most two coding agents in parallel, disjoint ownership; no subdelegation; no agent Git mutation. Coordinator serializes Git. Preserve others' work.
- No live HA writes, flash/upload, stove commands, factory settings or automatic P1-P5/fan writes.
- Existing installations without the optional package preserve IDs and behavior. No VTherm code copy. Native target limits come from configuration.
- All exact behavior/defaults are in the binding design and protocol; no new product dependency except ESPHome/HA's existing libraries.

## Review focus

Stale reports kept alive by heartbeats; own RF2 mistaken for manual change; bridge/boot synthetic observations accepted as fresh; malformed/oversized/out-of-order relay packets; RF2 ACK without RC6000 confirmation. Cover these in their owning tasks.

### Task 1: Firmware controller and opt-in component (A)

**Owner:** firmware worker. **Files:** components/duepi_room/**, tests/native/**. Do not edit existing YAML, HA files, requirements or README in this task.

Read ../specs/2026-10-20-duepi-room-design.md and ../specs/2026-10-20-duepi-room-protocol.md first (resolve from this plan directory).

Produce a pure testable controller plus idiomatic ESPHome climate/select/number/diagnostic adapters and the JSON relay parser. Generate USE_DUEPI_ROOM. Provide guarded, null-safe observation hooks for raw stove temperature, raw phase, RC6000, bridge state, manual native target, UART availability, and actual RF2 emission. Provide YAML automation callbacks for requested native target and explicit user mode commands. Document adapter interfaces in your report for task 3.

- [ ] Write failing native tests for compensation examples, PI/integration/antiwindup, rounding/bounds, repeated samples, stale/invalid data, transport expiry, manual/bridge/boot guards, write interval, and RC6000 confirmation/failure.
- [ ] Implement controller and parser with injected monotonic time and no transport/stove side effects in the pure class; ensure malformed/versioned messages fail closed.
- [ ] Add external component codegen/schema and ESPHome entity adapters, including configurable fallback/source age and advanced PI settings. No synthetic reading may unlock control.
- [ ] Run native tests; exercise parser malformed/wrong-type/oversized inputs; report exact commands, count/results and integration API. Self-review scoped code.

### Task 2: Home Assistant companion (B)

**Owner:** HA worker. **Files:** custom_components/duepi_room/**, tests/ha/**, requirements-test-ha.txt, pytest.ini if needed. No firmware/native/YAML/README edits.

Read the same design and relay protocol as task 1. HA version at Capinghem is the validation reference; use isolated fixture dependencies. No actual HA API write or installation.

- [ ] Write failing HA fixture tests for config/options selectors, duplicate module, stable relay discovery/rename, incompatible firmware, source conversion/invalid/restored/stale, unchanged-value reports, serialized heartbeats, reconnect, cleanup/unload.
- [ ] Implement config flow/options, entry lifecycle and relay sender using only public HA APIs and standard text.set_value. No climate, new device, private ESPHome runtime coupling, direct API client or control action.
- [ ] Test listener/source replacement, not replaying expired data, generation/sequence/sample identity, finite/unit validation, and packet length from the frozen protocol.
- [ ] Run focused pytest/format checks with evidence. Supply a minimal development test dependency file and report exact HA/Python versions and commands. Self-review scoped code.

### Task 3: Stove hooks, package, builds, documentation (C)

**Owner:** fresh firmware integration worker after task 1 review. **Files:** existing components/{climate,script,uart,time,globals}.yaml only for required hooks; new optional room package; tests/fixtures/**; examples/**; README and docs/room-regulation.md. Can fix components/duepi_room/** only through coordination if a build reveals an adapter issue; no HA edits.

Consume the reviewed controller APIs and frozen protocol. Preserve the existing IDs and serial transaction lane. Extract the existing native RF2 writer into a shared script, preserving legacy behavior; automatic output calls it directly, never a legacy climate target call. Existing native target user changes notify manual takeover unless restoring/synchronizing. Feed genuine RD1000/raw phase/RC6000 and bridge observations through compile-guarded hooks so legacy-only builds need no absent optional IDs.

- [ ] Add focused integration/contract tests that fail before hooks, including no extra on/off/power/fan output, RC6000 echo vs manual target, bridge resync and busy UART.
- [ ] Add the opt-in YAML package and examples; pin package/component to the same revision, allow a local source override for fixtures. Check restored state and explicit climate heat/off forwarding guards.
- [ ] Prepare dummy-only secret fixtures and isolated ESPHome 2026.9.1 runtime. Validate and compile legacy baseline, opt-in esp01_1m with OTA fit, opt-in lolin_c3_mini. Never upload.
- [ ] Document installation, entity/control meaning, defaults, source report age limitation, custom component setup, and a separate field protocol: back up/disable only stove VTherm without sending off, no radiator changes, start Compensation, verify RC6000 then RD4000 on three readings a minute apart, evaluate PI separately.
- [ ] Report exact builds, firmware sizes/OTA limit, tests, supported versions and any genuine blocker. No claim of thermal precision without field evidence.

### Task 4: Independent final review and delivery (D)

**Owner:** read-only fresh reviewer, coordinator verifies evidence/Git. All product fixes return to the owning coding agent.

- [ ] Review task-specific diffs after each lot against brief/spec and quality, using report evidence instead of repeating passing tests.
- [ ] Resolve important findings with focused fixes and scoped re-review; examine final whole-branch diff once for cross-boundary and regression risk.
- [ ] Verify final native/HA/build evidence corresponds to final code and tracked fixtures contain no real credentials, generated build output or AI attribution.
- [ ] Commit coherent changes locally and push this feature branch only to origin. Update project SUIVI with result, limitations, build evidence and field-test resumption point.
- [ ] Deliver branch link and concise results. Stop before live configuration or flash; no upstream PR/publish.
