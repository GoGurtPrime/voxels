---
name: aaa-engine-work-items
description: 'Plan, decompose, or execute AAA C++ game-engine work with strict DevOps gates. Use when creating autonomous work items, phasing a voxel-engine feature, defining acceptance criteria, planning vertical slices, or reviewing delivery readiness.'
argument-hint: 'Engine objective, subsystem, or milestone to decompose or deliver'
user-invocable: true
disable-model-invocation: false
---

# AAA Engine Work Items

Turn a C++ game-engine objective into a sequence of narrow, autonomous work items that each produce observable progress. Favor exact contracts, dependency order, and proof over broad descriptions or optimistic status.

## When to Use

- Break an engine, gameplay, tooling, rendering, networking, asset-pipeline, or build-system goal into executable phases.
- Write a work item an AI can own without guessing its scope, ownership, or success condition.
- Plan or assess a vertical slice that must work in the real desktop game, not merely in unit tests.
- Define DevOps, CI, packaging, or release-readiness gates for a C++ engine change.

## Operating Principles

1. Start from the player, operator, or build-system outcome. State what will be visibly different, operationally available, or measurably protected.
2. Read the project architecture, active work items, build configuration, and nearby implementation before planning or changing code. Treat current code and history as the source of truth when documentation conflicts.
3. Deliver vertical slices. A subsystem is not complete until its output is consumed by the shipping application path, or the item explicitly delivers a prerequisite with a precise consumer item named.
4. Size one item for a small, cohesive pull request: one primary capability, compatible ownership boundaries, and independently verifiable outcomes. Split discovery, contracts, implementation, integration, migration, and performance work whenever combining them prevents autonomous completion.
5. Prefer deterministic, boring designs over speculative abstractions. Make interfaces explicit about ownership, lifetimes, thread affinity, error behavior, performance budgets, and platform boundaries.
6. Never represent scaffolding as delivery. Empty states, no-op overrides, mocks on the default runtime path, silent optional dependencies, and console-only behavior fail the item.
7. Require evidence. A green compile or mocked unit test alone does not prove player-facing work.

## Decompose an Objective

1. Define the outcome in one sentence:
   - Player outcome: what they can see, hear, or do in the desktop application.
   - Operator/DevOps outcome: what builds, packages, deploys, diagnoses, or recovers differently.
   - Technical outcome: the measurable invariant, latency, memory, reliability, or compatibility target.
2. Identify the controlling path: entry point, owning subsystem, data flow, thread boundary, asset path, build target, and runtime state that decide the outcome.
3. List hard dependencies and classify each as already available, a prerequisite item, or an external human action. Do not conceal unavailable SDKs, assets, platform access, or credentials.
4. Partition the work into phases in dependency order. A typical sequence is:
   1. Discovery and contract decision, only when uncertainty blocks implementation.
   2. Foundation or data contract.
   3. Core implementation with focused tests.
   4. Shipping-path integration and player/operator observability.
   5. Performance, reliability, platform, and release hardening.
5. Split a phase again when it has more than one independently deployable behavior, crosses unrelated ownership boundaries, requires multiple uncertain design decisions, or would make a small pull request hard to review, test, or revert.
6. Mark parallel-safe items only when they do not edit the same controlling interfaces, generated assets, CMake targets, or shared integration point. Otherwise state the required order explicitly.
7. Stop at the next executable phase. Do not smuggle future architecture, polish, or adjacent features into the current item.

## Work Item Format

Use this exact structure for every item:

```markdown
## WI-<phase>.<sequence>: <imperative capability title>

### Goal
<One outcome and the user, operator, or subsystem affected.>

### Scope
- Own: <modules, files, runtime path, or build targets this item may change>
- Exclude: <nearby work intentionally deferred>
- Prerequisites: <completed item IDs, existing interfaces, or external actions>

### Implementation Contract
- Inputs and outputs: <formats, API contracts, units, ownership, errors>
- Runtime integration: <exact state/frame loop/service/target that consumes the result>
- Threading and performance: <affinity, determinism, budgets, or "not applicable">
- Platform and dependencies: <required libraries, toolchains, assets, feature flags>

### Acceptance Criteria
- [ ] <Concrete functional behavior with measurable expected result>
- [ ] <Real shipping-path integration, or an explicitly named consumer item>
- [ ] <Focused automated test that would fail if the behavior were removed>
- [ ] <Build, static analysis, packaging, or deployment gate relevant to this item>
- [ ] <Desktop runtime smoke observation, when the result is player-facing>
- [ ] <Documentation, diagrams, asset requests, or operational runbook update if contracts changed>

### Verification Commands
```text
<exact configure/build/test/lint/package commands and expected success condition>
```

### Completion Evidence
- Changed: <files and responsibilities>
- Observed: <specific player/operator result>
- Results: <test counts, performance measurements, artifact paths, logs>
- Known gaps: <none, or a bounded follow-up with owner/item ID>
```

## Execute a Work Item

1. Re-read the item, architecture, diagrams, asset requirements, and current files on its path. Use version history when the rationale is unclear.
2. State a falsifiable implementation hypothesis and identify the cheapest focused check that could disprove it.
3. Implement the smallest complete change that establishes the item contract. Preserve layer direction, RAII ownership, C++20 quality, and platform-neutral boundaries.
4. Immediately run the focused test, compile, or deterministic check. Repair root causes; never weaken tests, disable gates, or add an environment-only bypass to claim success.
5. Integrate the result into the actual application or operational path named in the item. Test fixtures and headless paths may support automated checks but cannot be the only delivery proof for desktop behavior.
6. Run the release gate appropriate to the change: configured build, full CTest suite, required static analysis, asset/package validation, and a non-headless desktop smoke run for player-facing changes.
7. Report evidence in the completion format. Explicitly name any verification that was impossible and why. Do not begin a follow-up item before the current item satisfies its acceptance criteria.

## Decision Rules

| Situation | Required decision |
| --- | --- |
| The feature is only callable in a test or prints to a console | Add shipping-path integration before completion. |
| A dependency is missing | Fetch/configure it or fail the build clearly; use an explicit placeholder only for replaceable content and register the request. |
| A proposed item changes core API, renderer, gameplay, and CI | Split by ownership and order the integration item after stable contracts exist. |
| Tests pass but no real runtime behavior was observed | The item remains incomplete for player-facing work. |
| Performance target is unproven | Add profiling instrumentation and a measurable budget before claiming optimization. |
| A design decision blocks multiple items | Create a short decision item with alternatives, criteria, an ADR/result, and a named downstream consumer. |
| The environment cannot launch the application | Complete automated checks, report the missing runtime proof plainly, and leave human smoke validation as an explicit gate. |

## Quality Gate

Before closing an item, confirm all applicable conditions:

- The default runtime path contains real implementations, not test doubles or empty abstractions.
- Build output is clean and all relevant tests pass.
- Tests assert a meaningful side effect, invariant, serialized artifact, rendered result, or composed-system behavior.
- The outcome is observable by the intended player or operator.
- Thread affinity, ownership, error handling, determinism, and performance constraints remain valid.
- CMake, CI, package manifests, documentation, diagrams, and asset/action records reflect changed contracts.
- Completion evidence distinguishes verified facts from assumptions and known gaps.
- Follow the repository's established commit and push workflow; this skill does not impose a Git policy.

## Completion Report

```markdown
## Work Item <ID> - <Title>

### What Changed
- <file/subsystem and responsibility>

### Player / Operator Outcome
<Concrete behavior now available in the desktop app, pipeline, or release process.>

### Verification
- Build: <command and result>
- Tests: <command, counts, and result>
- Runtime or operational smoke test: <command/action and observed result, or explicit blocker>
- Performance/package/CI: <measurement or artifact, when applicable>

### Architecture and Delivery Notes
<ADR deviation: none, or exact rationale; known gaps and their owning work items.>

### Assets & Actions Needed From You
<Exact required files, formats, SDKs, credentials, tooling, or "none".>
```