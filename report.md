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

### Q5.3

## Phase 6: Garbage Collection and Space Reclamation

### Q6.1

### Q6.2
