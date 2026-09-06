# VoxelWorld — Optional GLM-5.3-Flash Worker Strategy

**Status:** optional / promotional-compute strategy  
**Prepared:** 2026-09-05  
**Relationship to main handoff:** supplemental only  
**Important:** this document must **not** become a permanent project dependency. The project architecture, roadmap, and AI governance must continue to function if GLM-5.3-Flash access disappears tomorrow.

---

# 0. Executive position

GLM-5.3-Flash can be useful for VoxelWorld as a temporary or replaceable **implementation worker**, especially if current promotional access makes it free or effectively free for substantial daily periods.

Recommended hierarchy:

```text
Human Director
    |
    v
GPT-6 Astra
architecture / decomposition / review
    |
    v
GLM-5.3-Flash
bounded implementation work
    |
    v
compiler / tests / runtime validation
    |
    +------> Astra review
    |
    +------> selective Opus 5 adversarial review
```

The key rule is:

> **Do not try to make GLM incapable of making coding mistakes. Make it incapable of making consequential architectural decisions.**

Astra should not merely create a high-level todo list.

Astra should convert difficult work into **narrow implementation contracts** with:

- exact allowed files
- exact responsibilities
- exact interfaces
- explicit invariants
- explicit forbidden changes
- exact tests
- build command
- definition of done
- escalation conditions

GLM then implements those contracts.

This can substantially reduce premium-model usage.

However:

- GLM should not autonomously design foundational systems.
- GLM should not be trusted to resolve architecture ambiguity.
- GLM should not be allowed to expand task scope.
- GLM output must be compiled/tested.
- Important diffs should be reviewed by Astra.
- Architecturally sensitive milestones should also be reviewed independently by Claude/Opus when available.

The correct economic metric is not:

> "How many free GLM tokens did we use?"

It is:

> **"How many premium-model tokens and human hours were required per accepted, tested change?"**

If GLM repeatedly creates rework that consumes more Astra/Opus review than direct implementation would have, stop using it for that task category.

---

# 1. Why this is separate from the main architecture handoff

GLM-5.3-Flash promotional access may be temporary.

Therefore:

- do not encode "GLM" into core architecture decisions
- do not make backlog tasks depend on GLM availability
- do not make `AGENTS.md` require GLM
- do not design tooling that only works with Z.ai
- do not change software interfaces merely to accommodate GLM
- do not assume promotional hours/rates continue
- do not defer important development waiting for GLM availability

The permanent project model should remain:

```text
Director
  +
repository-owned project constitution
  +
replaceable architecture model
  +
replaceable implementation model
  +
independent review
  +
real compilation/tests
```

GLM is simply one candidate for the "implementation model" slot while it is economically attractive.

---

# 2. Core idea: senior architect + lower-cost worker

This maps to a normal engineering organization.

## Astra

Treat as:

- principal engineer
- systems architect
- task decomposer
- specification author
- high-risk debugger
- code reviewer
- architecture guardian

## GLM-5.3-Flash

Treat as:

- junior/mid implementation engineer
- test author
- boilerplate author
- mechanical refactor worker
- bounded bug-fix worker

## Opus 5 / Claude

Treat as:

- independent senior reviewer
- adversarial architecture critic
- second-opinion debugger

## Director

Remains:

- product owner
- design authority
- acceptance authority
- playtester
- final architecture decision-maker

---

# 3. What makes the hierarchy work

The value is not merely "Astra is smart and GLM is cheap."

The value is **capability separation**.

Open-ended tasks require architecture reasoning.

Example:

```text
"Implement multiplayer terrain persistence."
```

This forces the worker to decide:

- chunk model
- revision model
- serialization
- save format
- JIP transfer
- authority
- threading
- plugin boundary
- failure recovery

That is inappropriate for a weaker worker.

Instead Astra should produce:

```text
Task TERRAIN-017

Goal:
Add serialization for the already-approved FTerrainEditOp type.

Allowed files:
- TerrainEditOp.h
- TerrainEditOp.cpp
- TerrainEditOpTests.cpp

Must not modify:
- terrain backend adapter
- replication subsystem
- persistence subsystem
- inventory
- Blueprint assets

Required behavior:
- serialize OperationId
- serialize Revision
- serialize OpType
- serialize Transform
- serialize Radius
- serialize MaterialId
- preserve exact values on round trip
- reject malformed versions

Required tests:
- valid round trip
- invalid version
- boundary enum value
- maximum radius
- deterministic binary output if required

Definition of done:
- target compiles
- tests pass
- no new warnings
- diff contains only allowed files
```

Now GLM is implementing rather than designing.

That distinction is the foundation of the strategy.

---

# 4. Why decomposition does not make failure impossible

Even with excellent specification, a weaker worker can still create local defects.

Examples:

- squared distance compared against unsquared threshold
- wrong world-space origin
- wrong Unreal RPC macro usage
- invalid ownership assumption
- null pointer on dedicated server
- incorrect serialization version handling
- missing `UPROPERTY` where GC requires it
- unnecessary plugin include
- circular module dependency
- incorrect constness
- excessive copying
- wrong thread assumption
- changing public API unnecessarily

Therefore:

> decomposition reduces **decision risk**, not all implementation risk.

The system must rely on **verification**, not faith.

---

# 5. Correct execution loop

Use:

```text
Astra designs
    |
    v
Astra produces worker packet
    |
    v
GLM implements
    |
    v
Build / compiler / UHT / tests
    |
    v
Astra reviews exact diff + outputs
    |
    v
GLM fixes only specified defects
    |
    v
Tests again
    |
    v
Accept / reject / escalate
```

Do not use:

```text
Astra creates 50-item backlog
    |
    v
GLM works autonomously for hours
    |
    v
Huge unreviewed diff
```

The second model invites architecture drift.

---

# 6. Suitable GLM task categories

GLM can be used aggressively for work where architecture has already been decided.

Examples:

## Data types

- enums
- structs
- DTOs
- config structs
- serialization helpers
- version constants
- metadata containers

## Mechanical implementation

- `.cpp` implementation from approved header
- getters/setters
- simple validation
- adapters with exact interface
- known CRUD operations
- parsing approved data formats
- explicit mapping functions

## Testing

- unit tests
- serialization round-trip tests
- validation tests
- test fixture generation
- malformed-input tests
- regression tests for known bug

## Logging/debug instrumentation

- trace statements
- counters
- assertions
- timing scopes
- diagnostic commands

## Refactors with fixed target

- rename symbols
- move function
- split file
- replace repeated pattern
- switch known API
- remove deprecated call
- migrate exact type

## Documentation

- update STATE from completed task
- update test instructions
- document command output
- update API comments
- append accepted implementation notes

## Build/config work

Only when exact expected change is known:

- module dependency addition
- build target update
- config key addition
- test registration

## Simple UI glue

When presentation contract is clear:

- bind widget to existing property
- expose exact Blueprint callable function
- add read-only status text
- connect already-approved data

---

# 7. Tasks GLM should not own

GLM should not independently decide:

## Terrain architecture

- chunk dimensions
- density representation
- material semantics
- backend abstraction
- global vs per-chunk revision
- op ordering
- compaction model
- JIP model
- backend replacement strategy

## Networking

- replication ownership
- authority boundaries
- prediction model
- relevancy strategy
- reliable/unreliable policy
- RPC topology
- cross-chunk transaction semantics

## Persistence

- schema design
- save versioning architecture
- migration system
- journal/snapshot semantics
- corruption recovery
- backup model

## Unreal framework structure

Unless pre-decided:

- Actor vs Component
- GameInstanceSubsystem vs WorldSubsystem
- UObject ownership
- threading
- async work boundaries
- GC-sensitive lifetime model

## Power/automation architecture

- power graph model
- logistics graph
- scheduling
- batching
- state replication
- dirty propagation

## Large performance refactors

- data-oriented redesign
- allocator strategy
- chunk cache policy
- parallelization model
- replication compression design

## Vision/scoping decisions

- abandon voxels
- change player count
- switch engines
- alter multiplayer architecture
- change persistence pillar

Those belong to Astra/Opus/Director.

---

# 8. Explicit ambiguity escalation rule

Add this rule to every GLM worker packet:

> **If the specification does not uniquely determine an architectural choice, stop and report the ambiguity. Do not invent a design.**

Examples that should cause escalation:

```text
"Should revision IDs be global or per chunk?"
```

Escalate.

```text
"Should this live in a WorldSubsystem or ActorComponent?"
```

Escalate unless specified.

```text
"I need Voxel Plugin types in the gameplay authority module."
```

Escalate.

```text
"The requested API requires changing the save schema."
```

Escalate.

```text
"Tests reveal current architecture cannot satisfy both requirements."
```

Escalate.

GLM should be rewarded for stopping at ambiguity, not punished for failing to improvise.

---

# 9. Suggested task risk classification

Use a risk class in planning.

## R0 — mechanical

Examples:

- rename
- formatting
- comment update
- trivial config
- copy approved pattern

Worker:

- GLM

Review:

- automated diff/build may be enough

## R1 — bounded implementation

Examples:

- serializer
- validator
- small function
- test
- approved interface implementation

Worker:

- GLM

Review:

- Astra or strong automated verification

## R2 — localized design

Examples:

- choose internal data structure
- small component API
- localized integration detail

Worker:

- Astra decides
- GLM implements

Review:

- Astra

## R3 — subsystem architecture

Examples:

- terrain replication
- save system
- power graph
- JIP
- inventory ownership
- threading

Worker:

- Astra proposal
- Opus challenge where available
- Director approval
- GLM only implements bounded pieces

Review:

- Astra + selective Opus

## R4 — vision/irreversible

Examples:

- abandon arbitrary terrain deformation
- switch engine
- change player scale
- change dedicated-server model
- remove technology pillar

Worker:

- no AI decides

Director decides after agent analysis.

---

# 10. Recommended worker packet template

Every GLM task should use a structured packet.

```markdown
# TASK ID
TERRAIN-017

# RISK CLASS
R1

# PURPOSE
Add serialization for approved FTerrainEditOp.

# CONTEXT
Only include context required for this task.

# ALLOWED FILES
- ...
- ...

# FORBIDDEN FILES
- ...
- ...

# APPROVED INTERFACES
Exact signatures.

# REQUIRED BEHAVIOR
1.
2.
3.

# INVARIANTS
- ...
- ...

# MUST NOT
- change architecture
- add new dependency
- modify save schema
- call plugin directly
- introduce Tick
- alter replication topology

# TESTS REQUIRED
1.
2.
3.

# BUILD / RUN COMMAND
...

# DEFINITION OF DONE
- clean compile
- tests pass
- no warning regression
- diff scoped

# ESCALATE IF
- specification conflict
- new dependency needed
- public API change needed
- save schema change needed
- plugin type must leak
- task cannot be completed in allowed files

# RETURN
- summary
- exact files changed
- exact test/build output
- git diff
- unresolved concerns
```

---

# 11. Context minimization

A cheap worker does not need the whole project brain for every task.

Give GLM:

- exact task packet
- relevant interface file
- relevant implementation file
- relevant test file
- only the specific architecture rule required

Do not routinely send:

- entire GDD
- entire vision
- all docs
- whole repository

Astra should perform context selection.

This has several benefits:

- less distraction
- fewer architecture inventions
- lower token use
- faster execution
- easier review

For R3 work, Astra receives broad context.

For R1 work, GLM receives narrow context.

---

# 12. Architecture leakage checks

This is one of the biggest risks.

Example approved architecture:

```text
Gameplay -> ITerrainBackend -> Voxel Plugin
```

Bad worker shortcut:

```text
Gameplay -> UVoxelWorld / plugin API directly
```

It may compile and work.

It is still wrong.

Astra review should check:

- dependency direction
- plugin-specific types crossing interfaces
- direct access to persistence
- new replicated ticking actors
- client-authoritative state
- public API expansion
- save schema changes
- Blueprint becoming persistent authority
- new global singleton
- hidden static state
- unexpected cross-module include
- missing test

Review is not only:

> "Does it compile?"

It is:

> "Did this change preserve the architecture?"

---

# 13. Compiler and tests are a third source of intelligence

This strategy works better in software than in subjective domains because external systems judge correctness.

Useful validators:

- C++ compiler
- Unreal Header Tool
- Unreal Build Tool
- linker
- Automation Tests
- multiplayer PIE
- dedicated server test
- assertions
- logs
- Unreal Insights
- static analysis
- sanitizers where possible
- Git diff

A weaker model cannot successfully bluff these.

Therefore a bounded implementation worker surrounded by strong tests can perform above its unaided capability.

---

# 14. Test-first delegation

For risky R1/R2 implementation, Astra should often design tests before GLM writes production code.

Example:

```text
Astra:
1. define serializer contract
2. define 8 tests
3. GLM writes tests
4. verify tests fail for missing implementation
5. GLM writes implementation
6. run tests
7. Astra reviews
```

This prevents worker from redefining success after writing code.

---

# 15. Use GLM for debugging only when bug scope is bounded

Suitable:

- compile error
- failing known unit test
- obvious null check
- exact regression with reproduction

Not suitable autonomously:

- intermittent multiplayer desync
- save corruption
- race
- unexplained terrain divergence
- engine crash involving GC
- threading bug
- performance cliff

For those:

- Astra/Opus diagnoses
- GLM may implement exact fix after root cause is known

---

# 16. How GLM should interact with VoxelWorld terrain work

GLM should not be asked:

> "Build terrain networking."

Instead terrain project decomposes into units.

Example possible sequence:

## TERRAIN-001
Define exact already-approved `FTerrainChunkId`.

GLM.

## TERRAIN-002
Add serialization and tests.

GLM.

## TERRAIN-003
Define `ETerrainEditOpType` from approved values.

GLM.

## TERRAIN-004
Implement `FTerrainEditOp` constructors/validation.

GLM.

## TERRAIN-005
Design authoritative operation sequencing.

Astra + review.

## TERRAIN-006
Implement approved sequence counter.

GLM.

## TERRAIN-007
Design request/RPC ownership.

Astra.

## TERRAIN-008
Implement exact server RPC.

GLM.

## TERRAIN-009
Design persistence snapshot model.

Astra + Opus.

## TERRAIN-010
Implement chunk journal serializer.

GLM.

This is the intended pattern.

---

# 17. Premium model usage should concentrate on irreversible/high-leverage reasoning

Use Astra for:

- architecture
- spec production
- risk analysis
- code review
- root-cause analysis
- interface design
- test strategy
- performance interpretation
- refactor planning
- cross-subsystem effects

Do not use scarce Astra usage for:

- comments
- repetitive structs
- boilerplate
- simple getters
- mechanical renames
- routine docs

Likewise, preserve Claude/Opus allowance for:

- independent audit
- architecture disagreement
- dangerous diff
- subtle bug
- milestone review

---

# 18. Why GLM can sometimes improve engineering quality indirectly

A weaker worker forces the architect to externalize assumptions.

Astra cannot say:

> "Implement this the obvious way."

Instead it must define:

- API
- invariant
- ownership
- allowed dependencies
- failure behavior
- tests
- scope

That creates better project documentation.

Even if GLM disappears later, these worker packets and architecture contracts remain valuable for:

- Codex
- Claude Code
- future models
- human collaborators

Therefore use GLM in a way that produces **portable specifications**, not GLM-specific prompts.

---

# 19. How to measure whether GLM is actually worth using

Run a representative benchmark.

Suggested tasks:

1. UE `USTRUCT` + serializer + tests
2. server-side request validation
3. refactor plugin call behind approved interface
4. diagnose/fix a deliberately introduced simple replication bug
5. implement small versioned persistent repository
6. mechanical rename/refactor
7. test generation

For each task record:

- first-attempt compile success
- number of worker turns
- number of Astra review defects
- number of architectural violations
- hallucinated Unreal API count
- test quality
- amount of unnecessary diff
- premium review tokens
- human intervention time
- final accepted result

Compare with:

- Codex directly
- Claude Code directly
- Astra directly, where usage permits

The correct metric:

```text
Total scarce-resource cost per accepted change
```

This includes:

- Astra usage
- Opus usage
- human time
- correction cycles

Free GLM output that requires excessive supervision may not actually be cheaper.

---

# 20. Promotion expiration strategy

Assume GLM access can vanish at any time.

Therefore:

## No critical state in GLM chat

All important state belongs in repo.

## Worker packets committed or reproducible

Task specs can be used by another model.

## No GLM-specific code conventions

Use normal UE/C++ project conventions.

## No GLM-specific API integration unless independently useful

Do not build infrastructure solely because temporary model access exists.

## Worker tier replaceable

If promotion ends:

```text
GLM -> Codex
```

or:

```text
GLM -> Claude Code
```

without changing architecture.

The workflow should become:

```text
Astra -> replacement worker -> tests -> review
```

instead of collapsing.

---

# 21. Suggested project governance language

Do not write:

> "GLM-5.3-Flash is the implementation agent."

Prefer:

> **Low-cost implementation worker:** currently GLM-5.3-Flash when economically available. This role is replaceable and has no architectural authority.

This avoids accidental dependency.

---

# 22. Recommended reviewer frequency

Not every R0 change needs Astra.

Possible cadence:

## R0
- compile/test
- diff check
- batch review later

## R1
- Astra review before merge

## R2
- Astra authors design and reviews

## R3
- Astra architecture
- Opus independent challenge when possible
- Director ruling
- Astra reviews implementation batches

## R4
- Director decision
- both frontier agents can advise

This preserves scarce usage.

---

# 23. Batch strategy

Do not batch unrelated work.

Acceptable batch:

```text
Terrain serialization package:
- FTerrainChunkId serialization
- FTerrainEditOp serialization
- tests
```

Bad batch:

```text
- terrain serializer
- inventory UI
- day/night
- generator class
- networking refactor
```

Reviewability matters more than throughput.

---

# 24. GLM and documentation

GLM can update documentation after implementation, but should not rewrite architecture decisions.

Allowed:

- mark test run
- update state
- add implementation note
- add command
- add measured benchmark

Not allowed without Director:

- change VISION
- change accepted DECISIONS
- alter architectural rule
- redefine milestone success

---

# 25. GLM and Git

Worker should:

- inspect diff before completion
- not commit unrelated files
- not rewrite history
- not amend unrelated commit
- avoid destructive commands
- return exact diff summary

Ideal branch/task isolation remains unchanged.

---

# 26. Failure modes of this strategy

## Failure mode A — over-decomposition overhead

Astra spends more time writing specification than direct implementation.

Mitigation:

- use GLM only where task volume/repetition justifies it

## Failure mode B — review tax

GLM writes mediocre code and Astra repeatedly fixes it.

Mitigation:

- benchmark
- demote task class
- let Codex/Claude perform it directly

## Failure mode C — architecture leakage

Worker takes shortcut.

Mitigation:

- forbidden dependencies
- allowed-file list
- diff review
- architecture tests where possible

## Failure mode D — hallucinated Unreal APIs

Mitigation:

- compile immediately
- reference local engine headers/docs
- prohibit speculative API invention
- escalate uncertain engine behavior

## Failure mode E — giant cheap-model output

"Free" encourages too much code.

Mitigation:

- small task size
- diff size expectations
- one contract at a time

## Failure mode F — worker changes specification

Mitigation:

- worker cannot reinterpret task
- ambiguity requires stop/escalation

## Failure mode G — promotion becomes permanent assumption

Mitigation:

- this document remains supplemental
- no core dependency
- replaceable worker role

---

# 27. Relationship to Claude Pro / Opus 5

Even if GLM handles most R0/R1 implementation, Claude Pro remains useful.

Best use of Claude/Opus:

- adversarial architecture review
- inspect dangerous diff
- diagnose subtle Unreal issue
- provide independent alternative
- review a subsystem before checkpoint

This is potentially better value than using Opus to type every routine method.

Claude does not need to participate in every task.

Use it where independence has high value.

---

# 28. Relationship to Codex

Codex remains a strong primary or fallback worker.

If GLM promotion ends:

- Codex can inherit R0/R1 implementation role
- Astra can remain architecture/supervision
- Claude remains independent reviewer

If Codex proves materially better than GLM on accepted-change cost even while GLM is free, use Codex.

"Free" is not automatically efficient.

---

# 29. Recommended initial experiment before formal operational use

Before using GLM on core terrain code, run a sandbox benchmark.

Create branch:

```text
experiment/glm-worker-eval
```

Give 5–7 bounded tasks.

Do not let experiment change durable architecture.

Evaluate.

Suggested acceptance threshold:

- high compile success
- no repeated architectural leakage
- low hallucinated API rate
- tests are meaningful
- review fixes are small
- worker follows allowed-file scope
- worker escalates ambiguity correctly

If not, restrict GLM to R0 tasks.

---

# 30. Example worker packet — terrain request validation

```markdown
TASK ID: TERRAIN-RPC-VALIDATE-001
RISK: R1

PURPOSE:
Implement validation for an already-approved server terrain edit request.

ALLOWED FILES:
- Source/VoxelWorld/Terrain/VWTerrainRequestValidator.h
- Source/VoxelWorld/Terrain/VWTerrainRequestValidator.cpp
- Source/VoxelWorldTests/Terrain/VWTerrainRequestValidatorTests.cpp

DO NOT MODIFY:
- Voxel Plugin integration
- PlayerController RPC declaration
- inventory
- persistence
- Blueprint assets

INPUT:
FTerrainEditRequest {
    FVector Position;
    float RadiusCm;
    uint32 ToolInstanceId;
}

INPUTS PROVIDED TO VALIDATOR:
- player position
- max reach
- max allowed radius

REQUIRED:
- reject non-finite position
- reject radius <= 0
- reject radius > max
- reject target beyond reach
- do not mutate request
- deterministic
- no world access
- no plugin access

TESTS:
- valid request
- exact reach boundary
- past reach
- zero radius
- negative radius
- oversized radius
- NaN position

ESCALATE:
- if FVector finite-check API is uncertain
- if approved struct needs to change
- if new module dependency appears necessary

DONE:
- compile
- all tests pass
- no warnings
```

This is an appropriate GLM task.

---

# 31. Example task that should not go to GLM

```text
"Design the join-in-progress terrain synchronization system."
```

Why not:

Requires decisions about:

- chunk relevancy
- snapshots
- revisions
- compression
- ordering
- base generator version
- partial arrival
- retry
- corrupted state
- server/client ownership
- live ops during snapshot transfer

Astra should design it.

Opus should challenge it.

Then GLM can implement pieces.

---

# 32. Suggested permanent abstraction for AI roles

Potential `AGENTS.md` language:

```markdown
## Agent capability roles

### Architect
May propose subsystem design.
Must surface alternatives and risks.
Cannot change accepted decisions without Director approval.

### Implementation worker
Implements approved contracts.
Has no architectural authority.
Must stop on ambiguity.
Must obey allowed-file and dependency boundaries.
Must compile/test before completion.

### Independent reviewer
Reviews architecture or diffs without assuming the author is correct.
Should search for hidden failure modes and architectural drift.

Current model assignments are operational choices and may change at any time.
```

This survives GLM promotion expiration.

---

# 33. Suggested daily operating model during promotion

If 10 hours/day or similar free window is available:

Do not attempt to fill all 10 hours.

Use demand-driven work.

Example:

### Before worker window
Astra creates 3–6 reviewed R0/R1 packets.

### During window
GLM performs them sequentially.

After each:

- build
- test
- store diff/results

### Review
Astra reviews batches only after verified outputs exist.

### Stop conditions
Stop worker queue if:

- repeated architecture leakage
- build state becomes unclear
- task requires redesign
- same error recurs
- diff grows outside scope

Free capacity is not a target to consume.

---

# 34. How much work could GLM perform?

If worker quality is good:

Potentially:

- 60–80% of implementation **turns**
- much less than 60–80% of architectural reasoning

That is normal.

A small number of architecture decisions determine a large amount of implementation.

Example:

One well-designed persistence interface may allow dozens of simple implementation tasks.

---

# 35. Final recommendation

Use GLM-5.3-Flash opportunistically.

Do **not** build the project around it.

The best temporary arrangement is:

```text
Astra:
- architecture
- decomposition
- specifications
- high-risk debugging
- review

GLM:
- bounded R0/R1 implementation
- tests
- repetitive work
- mechanical refactors
- instrumentation

Compiler/tests:
- mandatory external correctness checks

Opus:
- selective independent review for R3 work

Director:
- final authority
```

The project should remain fully functional if tomorrow becomes:

```text
Astra -> Codex -> tests -> Opus review
```

or:

```text
Opus -> Claude Code -> tests -> Astra review
```

The durable asset is not access to a promotional model.

The durable asset is:

- precise architecture
- bounded task specifications
- automated tests
- repository-owned state
- model-independent governance
- review discipline

If GLM is free and competent, exploit it.

If the promotion ends, replace it.

If it creates more review cost than value, stop using it.

---

# 36. One-sentence rule

> **Use GLM-5.3-Flash as a temporary replaceable implementation worker, never as a source of architectural truth; Astra defines and reviews the contract, tests adjudicate behavior, and important subsystem boundaries receive independent senior-model review.**
