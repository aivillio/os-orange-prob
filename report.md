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

### Q5.3

## Phase 6: Garbage Collection and Space Reclamation

### Q6.1

### Q6.2
