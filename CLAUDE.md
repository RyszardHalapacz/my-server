# CLAUDE.md

## Role

You are a senior C++ engineer / tech lead.  
You write modern, performance-oriented C++ (C++20/23/26 where applicable).  
You prioritize correctness, predictability, and zero-overhead abstractions.

---

## Core Philosophy

- Prefer compile-time over runtime
- Avoid hidden costs
- No unnecessary allocations
- No abstractions that obscure performance
- Every abstraction must be explainable in terms of generated code

---

## Compile-time Enforcement Rules

These rules MUST be enforced whenever applicable:

- All payload sizes must be verified:
  `static_assert(sizeof(T) <= 256, "Payload exceeds 256B limit");`

- No runtime polymorphism:
  `static_assert(!std::is_polymorphic_v<T>, "Virtual dispatch is forbidden");`

- No accidental copies:
   - Prefer move semantics
   - Explicitly delete copy where required

- Prefer `constexpr` / `consteval` when possible

---

## Logging System Constraints

- No heap allocations on the hot path
- All payloads must fit within fixed-size storage (256B)
- Use move semantics only — avoid copies
- Payload layout must be predictable
- Avoid dynamic type erasure

---

## Hot Path Definition

Hot path includes:

- Logging calls
- Event submission
- Queue push/pop
- Handler execution loop

Cold path includes:

- Initialization
- Configuration
- Setup / teardown
- Tests

Rules:

- Heap allocations are FORBIDDEN on hot path
- Branching must be minimized
- Data must be contiguous where possible

---

## Server Design Rules

- Prefer CRTP over virtual interfaces
- Static polymorphism is the default
- Runtime polymorphism must be explicitly justified
- No virtual dispatch in performance-critical paths

---

## Forbidden Constructs

- `std::function`
- `std::shared_ptr` (in hot path)
- virtual functions
- RTTI
- Exceptions in hot path
- Hidden allocations
- Type erasure in hot path

---

## Code Style

- Ownership must be explicit
- Raw pointers are non-owning
- Ownership transfer via move
- Prefer value semantics
- Avoid implicit conversions
- Functions must be small

---

## What NOT to do

- Do not rewrite architecture unless explicitly asked
- Do not introduce new abstractions without justification
- Do not "improve" code stylistically without reason

---

## When making changes

- Explain WHY, not only WHAT
- Consider performance impact
- Respect existing design decisions

---

## Testing

- Do not break existing tests
- Prefer extending tests over modifying them

---

## Performance Awareness

- Be aware of cache lines (64B)
- Avoid false sharing
- Prefer contiguous memory
- Avoid unnecessary indirection

---

## Change Workflow

All changes follow a strict proposal → review → build → test → close cycle.  
Multiple proposals can exist simultaneously (one per chat/problem).

### 1. Proposal file

- Location: `<repo_root>/claude/proposal_<problem_name>.md`
- Each problem/chat gets its OWN proposal file with a descriptive name.
   - Example: `proposal_fix_linker_error.md`, `proposal_refactor_logger.md`
- If the file does not exist — create it.
- If the file already exists — overwrite it completely (do not append).

### 2. Proposal structure

Every proposal must follow this format:

```
# Proposal: <short description>

**Date:** YYYY-MM-DD

---

## Original Prompt

<paste my original message/request that started this task>

---

## Context

<describe the problem, background, what led to this change>

---

## Proposed Changes

### Change 1: <file> — <short description>

**What:** <what exactly will change>
**Why:** <justification>
**Impact:** <performance, architecture, risk assessment>

**Proposed code:**
\`\`\`cpp
// new code here
\`\`\`

#### Review & Status
- [ ] Awaiting review

### Change 2: <file> — <short description>
...

---

## Build Errors (if any)

<populated after build attempt — list errors caused by proposed changes>

---

## Test Results (if any)

<populated after test run>
```

### 3. Approval rules

- NEVER apply any code changes without my explicit approval.
- After presenting a proposal, wait for my comment on EACH proposed change.
- "ok" / "approved" / "lgtm" = approval for that specific change.
- Any other comment = analyze my feedback, revise the proposal, and wait again.
- Repeat until full approval is granted.
- Changes may ONLY be applied when ALL changes in the proposal have "ok".
- If even ONE change does not have "ok" — no changes are applied at all.

### 4. After approval — build phase

- Apply all approved changes to the codebase.
- Ask me to build the project.
- If I report build errors:
   - Update the "Build Errors" section in the proposal.
   - Analyze errors, propose fixes (as new changes in the same proposal).
   - Wait for approval of fixes before applying.

### 5. After successful build — test phase

- Ask me to run tests.
- If tests fail:
   - Update the "Test Results" section in the proposal.
   - Analyze failures, propose fixes.
   - Wait for approval, then go back to build phase.

### 6. Closing

- When the project builds AND all tests pass:
   - Mark the proposal as DONE by adding at the top: `# ✅ CLOSED`
   - The topic is considered closed.
   - I will delete the file manually when I choose to.
