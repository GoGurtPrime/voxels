---
name: ai-agent-rules-and-prompts
description: 'Design, write, review, or improve AI coding-agent rules, prompts, skills, custom agents, and hooks. Use when steering an AI toward reliable implementation, debugging, planning, review, validation, or tool use; creating AGENTS.md, copilot-instructions.md, SKILL.md, .prompt.md, .agent.md, or instruction files; or diagnosing why an agent ignores guidance, over-scopes, hallucinates, or ships unverified work.'
argument-hint: 'Agent outcome, workflow, failure mode, or customization to create or improve'
user-invocable: true
disable-model-invocation: false
---

# AI Agent Rules and Prompts

Create precise, enforceable guidance that changes an AI coding agent's observable behavior. Optimize for reliable execution, focused scope, evidence-based decisions, and honest verification rather than broad aspirational advice.

## When to Use

- Author or revise repository rules such as `AGENTS.md`, `copilot-instructions.md`, or `*.instructions.md`.
- Create a reusable `SKILL.md`, a focused `*.prompt.md`, a specialized `*.agent.md`, or deterministic lifecycle hooks.
- Review agent guidance after failures such as speculative edits, skipped tests, stale assumptions, unhelpful verbosity, tool misuse, or incomplete implementation.
- Convert an expert's working method into instructions another agent can apply consistently.

## Choose the Right Control Surface

1. Identify the desired repeatable behavior, its trigger, intended scope, and how success can be observed.
2. Select the narrowest customization that can govern that behavior:
   - Use repository instructions for constraints that apply to most work in a project.
   - Use file instructions for language-, directory-, or subsystem-specific conventions.
   - Use a prompt for one focused, parameterized task.
   - Use a skill for an on-demand, multi-step workflow with reusable criteria or bundled resources.
   - Use a custom agent when a separate context, role, or tool boundary improves reliability.
   - Use hooks only when deterministic enforcement is necessary and a command can objectively check it.
3. Do not use always-loaded instructions for a rare workflow. Do not use a skill when a short prompt is sufficient.

## Gather Evidence Before Writing

1. Read the nearest existing agent rules, applicable instructions, and customization files. Preserve established placement, naming, and frontmatter conventions.
2. Start from a concrete failure, workflow, code path, test, or delivery requirement. State the desired agent behavior as an observable outcome.
3. Extract the smallest repeatable sequence:
   - Inputs and preconditions.
   - Actions in dependency order.
   - Decision points and stop conditions.
   - Required evidence and completion criteria.
4. Separate hard constraints from preferences. A hard constraint must state its scope, trigger, action, and consequence. Preferences should leave room for judgment.
5. If the target repository is known, use its actual commands, paths, build configuration, and quality gates. Do not invent project facts.

## Write High-Leverage Guidance

1. Lead with the outcome and the situations that should trigger the guidance.
2. Give the agent an ordered procedure, not a list of disconnected wishes.
3. Make branching explicit: "If condition X, do Y; otherwise do Z." Include a bounded escalation path for missing context, unavailable tooling, ambiguous requirements, and failing validation.
4. Define completion in externally checkable terms: a test result, build target, artifact, runtime observation, review finding, or user-facing behavior.
5. Tie requirements to their reason when that prevents harmful shortcuts. Prefer concise rules with clear ownership over long policy prose.
6. Specify tool behavior only when it changes correctness, safety, reproducibility, or cost. Name the required command or tool when one is authoritative.
7. Require agents to distinguish observed facts, assumptions, and unverified claims. Never let a green mocked check stand in for a required shipping-path observation.
8. Use direct language. Replace vague terms such as "properly," "robustly," or "best practice" with a concrete action or measurable criterion.

## Build a Reusable Skill

1. Create the skill at `.agents/skills/<lowercase-hyphenated-name>/SKILL.md` unless the repository uses another established skill location.
2. Match the folder and frontmatter `name`. Write a keyword-rich `description` that says both what the skill does and when to use it.
3. Keep the main file concise and procedural. Put detailed templates, examples, scripts, and references one level below the skill only when they materially reduce repetition.
4. Include these sections when applicable: purpose, triggers, decision flow, procedure, quality checks, failure handling, and final reporting expectations.
5. Design the skill so it can be followed using the repository's real tools and conventions.

## Review and Pressure-Test

Evaluate a draft against the following questions before finalizing it:

- Discovery: Does the description contain the actual terms a user or model will use?
- Scope: Does it say when the guidance applies and when it does not?
- Agency: Does it tell the agent what to do next, including on failure?
- Testability: Can each important requirement be checked from outputs, commands, or artifacts?
- Conflict handling: Does it defer to higher-priority user, repository, security, or platform instructions?
- Cost: Does it avoid broad scans, redundant checks, and irrelevant ceremony?
- Safety: Does it prohibit fabricating results, bypassing validation, exposing secrets, or destroying unrelated work?
- Maintainability: Are rules non-duplicative, current, and located at the narrowest useful scope?

Rewrite any rule that fails one of those questions. Remove instructions that merely restate an already higher-priority rule unless local clarification materially changes behavior.

## Validate the Customization

1. Verify the file lives in the intended scope and follows its naming convention.
2. Check YAML frontmatter: opening and closing markers, required `name`, matching skill folder, and a meaningful quoted `description`.
3. Check the procedure against one realistic prompt. Confirm it has a concrete first action, a decision rule, and a completion check.
4. Check for contradictions with repository rules and for instructions that require unavailable tools, credentials, or permissions.
5. Report the artifact path, intended triggers, the behavior it steers, and any remaining ambiguity that needs the user's policy decision.

## Failure Modes to Prevent

- Do not confuse a persona with a workflow; describe actions and verification.
- Do not require exhaustive exploration before an edit when a local hypothesis and focused check are available.
- Do not prescribe a tool or command without a reason it is authoritative for the task.
- Do not claim builds, tests, runtime behavior, or external actions that were not observed.
- Do not turn a narrow procedure into global instructions through broad applicability patterns.
- Do not add conflicting directives without an explicit precedence rule.

## Completion Report

Summarize:

- The customization created or updated and its scope.
- The workflows and trigger phrases it covers.
- The main decisions, gates, and failure handling it adds.
- The validation performed and any unresolved policy choices.