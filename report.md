# PES-VCS Lab Report

## Phase 5: Branching and Checkout

### Q5.1
`pes checkout <branch>` needs to update both repository metadata and working files.

Metadata changes in `.pes/`:
- Verify `.pes/refs/heads/<branch>` exists and read its target commit hash.
- Update `.pes/HEAD` so it points to `ref: refs/heads/<branch>` (unless implementing detached mode explicitly).
- Load the target commit and its root tree from object storage.

Working directory changes:
- Compare current tracked snapshot (from current HEAD tree) against target branch tree.
- For files only in target: create/write file from blob and set mode.
- For files only in current: remove from working tree (if safe).
- For files in both but different blob hash: overwrite with target blob contents and mode.
- Rebuild the index to match the checked-out target tree metadata.

Why checkout is complex:
- It is a coordinated multi-step operation that touches refs, index, and many files.
- It must detect local conflicts first (dirty tracked files) before overwriting anything.
- It needs failure-safe ordering (or rollback plan) so partial updates do not leave mixed snapshots.

### Q5.2
Conflict detection can be done with index metadata + staged blob hashes + target tree hashes:

1. Build map `current_head[path] -> blob_hash` by expanding the current HEAD tree.
2. Build map `target[path] -> blob_hash` by expanding the destination branch tree.
3. For each tracked path in index:
	- If file is missing in working dir: mark dirty (`deleted` locally).
	- Else compare current stat (`mtime`, `size`) with index entry.
	- If metadata differs, recompute blob hash from file and compare with index hash.
	- If hash differs from index hash, the file has unstaged local edits.
4. A checkout conflict exists when:
	- File has local edits, and
	- `target[path]` exists, and
	- `target[path]` hash differs from `current_head[path]` hash.

In that case, refuse checkout and print conflicting paths. This prevents clobbering local user changes.

Quick rule summary:
- Local clean + target changed: safe to checkout.
- Local dirty + target unchanged from current: can keep local file.
- Local dirty + target changed differently: conflict, refuse.

### Q5.3
In detached HEAD mode, `HEAD` contains a commit hash directly instead of a branch ref.

If you commit in this state:
- New commits are created normally with parent pointers.
- `HEAD` moves to the new commit hash.
- No branch name advances, so these commits can become unreachable later.

Recovery options:
- Create a branch at current detached commit (`pes branch rescue` equivalent).
- Or manually write the commit hash to a new ref file under `.pes/refs/heads/`.
- If user already moved away, recover via reflog-like history (if implemented) before GC prunes unreachable commits.

Example recovery flow:
- While detached at `<H>` after new work, create `refs/heads/recover` = `<H>`.
- Reattach `HEAD` to `ref: refs/heads/recover`.
- Commits are now protected from becoming unreachable.

Operational ordering note:
1. Resolve target commit and compute file actions.
2. Run dirty/conflict checks.
3. Apply file updates in working directory.
4. Rewrite index from target tree.
5. Update `HEAD`/branch pointer last.

Updating refs last avoids advertising a branch tip that the working tree/index has not finished materializing.

## Phase 6: Garbage Collection and Space Reclamation

### Q6.1
Use a mark-and-sweep GC over the object graph.

Mark phase:
1. Seed a worklist with all branch tips from `.pes/refs/heads/*` (and detached `HEAD` hash if present).
2. Pop each commit hash:
	- Mark commit hash as reachable.
	- Parse commit and enqueue its `tree` and optional `parent`.
3. For each tree hash:
	- Mark tree hash.
	- Parse tree entries; enqueue child tree hashes and blob hashes.
4. For blobs: mark and stop (leaf objects).

Sweep phase:
1. Enumerate all files under `.pes/objects/`.
2. Reconstruct each object hash from shard path.
3. Delete objects not present in the reachable set.

Data structure:
- Use a hash set keyed by 32-byte object id (or 64-char hex) for O(1) average membership checks.

Scale estimate:
- 100,000 commits and 50 branches still seed only 50 starting points.
- Reachability walk visits each reachable commit/tree/blob once.
- In a typical repo this is on the order of hundreds of thousands to a few million objects, depending on file churn and tree fan-out.

### Q6.2
