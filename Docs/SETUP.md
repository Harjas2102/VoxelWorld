# SETUP.md — Phase 0: From Zero to Working Brain

> The complete, ordered setup. Follow top to bottom. Each step states the
> goal, exact actions, and expected output. Report results between phases.
> When finished, this file lives in `/Docs` as the record of how the
> environment was built.

**Placeholder rule:** every command below uses `VoxelWorld` as the project
name and `YOURNAME` as your GitHub username. Substitute your real choices
everywhere. UE project name rules: letters and numbers only, no spaces, must
not start with a number, keep it short (≤20 chars). The repo name should
match the project name. (Naming the *game* is still D-008 — the repo
codename doesn't have to be the final title.)

---

## PHASE A — Installations (one evening, mostly download time)

### A1. Pre-flight
**Goal:** confirm the machine is ready.
1. Verify **250GB+ free** on the NVMe drive you'll use.
2. Update AMD Adrenalin drivers to current (amd.com/en/support).

### A2. Unreal Engine 5.7
**Goal:** engine installed.
1. Go to **epicgames.com** → download the Epic Games Launcher → sign in or
   create an account.
2. Launcher → **Unreal Engine** tab (left sidebar) → **Library** → click the
   **+** next to ENGINE VERSIONS → select the latest **5.7.x** → Install →
   choose a path on the NVMe.
3. ~50GB download. Let it run while you do A3–A4.

### A3. Git + Git LFS
**Goal:** version control installed, LFS confirmed.
1. Download Git for Windows from **git-scm.com** → run installer → accept
   **all defaults** through every screen.
2. Open a new **PowerShell** window and verify:
   ```
   git --version
   git lfs version
   ```
**Expected output:** two version strings (e.g. `git version 2.4x...` and
`git-lfs/3.x...`). If `git lfs` errors, install it from **git-lfs.com**,
then re-check.

### A4. Claude Code
**Goal:** Claude Code installed and signed in.
1. Open **PowerShell** (not CMD — the command below only works in
   PowerShell; your prompt should start with `PS`):
   ```
   irm https://claude.ai/install.ps1 | iex
   ```
   (If anything errors, the authoritative instructions live at
   **code.claude.com/docs/en/setup** — follow the Windows section.)
2. Close and reopen PowerShell, then run:
   ```
   claude --version
   ```
3. Run `claude` once anywhere — it will walk you through a one-time browser
   login with your Claude account. Complete it and exit (`/exit` or Ctrl+C).
**Expected output:** a version string, and a successful login.

### A5. Deferred — do NOT install tonight
**Visual Studio 2022 Community** with the "Game development with C++"
workload becomes required when we write our first C++ (Phase 1, ~T-104).
Noted here so it's not a surprise. Skip for now.

---

## PHASE B — Create the UE Project

**Goal:** a running project on disk — this folder will become the repo.

1. Epic Launcher → Library → **Launch** UE 5.7.
2. In the Project Browser: **Games** → **Third Person** template.
3. Settings on the right:
   - **Blueprint** (not C++ — C++ gets added later)
   - Target Platform: **Desktop**
   - Quality: **Maximum**
   - Starter Content: **OFF** ← *amended from the earlier plan: starter
     content bloats the repo past GitHub's free LFS quota, and we'll use
     Fab assets instead anyway*
   - Raytracing: leave default
4. Project Name: `VoxelWorld` · Location: a folder on the NVMe
   (e.g. `D:\Dev\`) → **Create**.
5. First open triggers shader compilation — 10–30 min of heavy CPU.
   **Normal.** While it churns, start Epic's "Your First Hour in Unreal
   Engine 5" learning path (dev.epicgames.com/community/learning).
6. When the editor settles, press **Play** (toolbar) and walk the mannequin
   around with WASD. Press **Esc** to stop.

**Expected:** smooth movement, high framerate, no crashes.

---

## PHASE C — The Multiplayer Moment (15 min — do not skip)

**Goal:** see UE replication working before writing a single line.

1. Next to the Play button, click the **⋮** (vertical dots).
2. Set **Number of Players: 3** and under Multiplayer Options set
   **Net Mode: Play As Listen Server**.
3. Press **Play**.

**Expected:** three game windows open. Move the character in one window —
it moves in all three. That is the replication backbone of the entire game,
working out of the box. Close with Esc.

---

## PHASE D — Birth of the Repo

**Goal:** the project folder becomes a Git repository with correct UE rules.

1. **Close the Unreal editor.**
2. Open PowerShell and go to the project folder — the one containing
   `VoxelWorld.uproject`:
   ```
   cd D:\Dev\VoxelWorld
   ```
3. Create the ignore file with Notepad *(amended in the field at CP-001:
   the original PowerShell here-string blocks hung when pasted, so plain
   Notepad is the method of record)*:
   ```
   notepad .gitignore
   ```
   Notepad asks to create the file — click **Yes**, paste exactly this,
   then save and close (Ctrl+S, then close the window):
   ```
   Binaries/
   DerivedDataCache/
   Intermediate/
   Saved/
   .vs/
   *.sln
   ```
4. Create the LFS rules the same way:
   ```
   notepad .gitattributes
   ```
   Click **Yes** to create, paste exactly this, save and close:
   ```
   *.uasset filter=lfs diff=lfs merge=lfs -text
   *.umap filter=lfs diff=lfs merge=lfs -text
   *.fbx filter=lfs diff=lfs merge=lfs -text
   *.png filter=lfs diff=lfs merge=lfs -text
   *.tga filter=lfs diff=lfs merge=lfs -text
   *.wav filter=lfs diff=lfs merge=lfs -text
   ```
5. First-time Git identity *(added at CP-001 — Git refuses to commit until
   it knows who you are; one-time setup per machine)*:
   ```
   git config --global user.name "YOURNAME"
   git config --global user.email "ID+YOURNAME@users.noreply.github.com"
   ```
   For the email, use your GitHub **noreply** address so your real email
   stays out of the public commit history. Find it at github.com →
   Settings → Emails → it's shown under "Keep my email addresses private"
   (looks like `12345678+YOURNAME@users.noreply.github.com`).
6. Initialize and commit:
   ```
   git init
   git lfs install
   git add .
   git commit -m "chore: initial UE 5.7 third-person template project"
   ```

**Expected:** the commit reports a few hundred files. `git status` then
says `nothing to commit, working tree clean`.

---

## PHASE E — Transplant the Brain (the doc pack)

**Goal:** the six docs from the planning chat live in the repo.

1. On the PC, open **claude.ai** in a browser, sign in — your account syncs,
   so the planning conversation from your phone is there. Open it and
   **download all seven files** (VISION, GDD, DECISIONS, STATE, BACKLOG,
   CLAUDE, SETUP).
2. Place them:
   - `CLAUDE.md` → **repo root**, next to `VoxelWorld.uproject`
     (Claude Code auto-reads it from there every session)
   - Everything else → a new `Docs` folder:
   ```
   cd D:\Dev\VoxelWorld
   mkdir Docs
   ```
   Move VISION.md, GDD.md, DECISIONS.md, STATE.md, BACKLOG.md, SETUP.md
   into `Docs\` (File Explorer is fine).
3. Commit the brain:
   ```
   git add CLAUDE.md Docs
   git commit -m "docs: project brain v1 (CP-000)"
   ```

**Expected:** clean `git status`; `Docs` shows six files; `CLAUDE.md` sits
at root.

---

## PHASE F — GitHub (offsite backup + sync)

**Goal:** the repo exists on GitHub, pushed, private.

1. On github.com: **New repository** → Name: `VoxelWorld` →
   **Private** → do **NOT** check "Add a README", no .gitignore, no license
   (the repo already has content) → Create.
2. Back in PowerShell, in the project folder:
   ```
   git remote add origin https://github.com/YOURNAME/VoxelWorld.git
   git branch -M main
   git push -u origin main
   ```
3. The first push pops a browser window — Git Credential Manager handling
   GitHub auth. Approve it once; it's remembered.
4. The push uploads LFS objects too — with no starter content this should
   fit comfortably in GitHub's free LFS quota. (If we ever outgrow it,
   that's a cheap, solvable problem — flag it when it happens.)

**Expected:** refresh github.com — all files visible, `.uasset` files show
an "LFS" badge, repo marked Private.

---

## PHASE G — First Contact: Claude Code Meets the Brain

**Goal:** verify Claude Code reads the docs and knows the project cold.

1. In PowerShell, from the project folder:
   ```
   cd D:\Dev\VoxelWorld
   claude
   ```
2. Send this exact first message:
   > Read CLAUDE.md, /Docs/STATE.md, and /Docs/VISION.md. Then tell me:
   > (1) what this project is in two sentences, (2) the engineering rules
   > you must follow, (3) the current task, and (4) what you will never
   > rely on for project facts.
3. Grade the answer against the docs yourself.

**Expected:** an accurate summary — smooth-voxel realistic survival game,
server-authoritative rules, current task = finishing Phase 0, and "never
chat history — /Docs is the only truth." If it nails this, **the brain is
online** and chat amnesia is permanently solved.

---

## PHASE H — Close-out

Report back to planning-Claude (the chat) with:
1. Shader compile: done / issues
2. Three-window PIE replication: worked / didn't
3. Output of `git status` and the GitHub repo URL existing
4. Claude Code's answer from Phase G

That report triggers **CP-001**: STATE.md gets rewritten (Phase 0 → done,
Starter-Content amendment recorded, current task → T-006 fluency drills +
T-101 Voxel Plugin), BACKLOG Done-log updated, and you commit + push the
new docs. From then on, that's the rhythm of every session.

---

## The Ongoing Rhythm (how the two Claudes split the work)

| Where | Role | Session opens with | Session ends with |
|---|---|---|---|
| **Claude app/web (chat)** | Design office: decisions, architecture, planning, debugging discussion | You paste `STATE.md` + `VISION.md` | "checkpoint" → updated doc text to commit |
| **Claude Code (terminal, in repo)** | Primary programmer: writes C++, edits files, sees compile errors | Auto-reads `CLAUDE.md`; instructed to read `/Docs` | Updated docs + `git commit` + `git push` |

Three habits make the system unbreakable:
1. **Every session ends with a checkpoint and a push.** No exceptions.
2. **Chats are disposable.** Long chat getting stale? Checkpoint, close it,
   start fresh — the repo remembers.
3. **The recite test.** Any time you suspect drift, ask either Claude to
   recite the vision. Wrong answer = reload the docs, never argue from
   memory.
