# Collision cleanup dispositions

C01 removed the unused pair-slot table, free list and per-pair slot; candidate ownership, adjacency, witness transitions and pending/contact/solver indexes remain. C03 removed the never-consumed accumulated bias impulse; normal and tangent warm starting and the active bias remain. C09 removed unused support accessors, C10 the unused grid sync wrapper, C11 unused pair mutators, and C12 the unused binding/rebinding chain. C03 and C12 remove installed declarations and require consumer migration and rebuilding.

Review of Grid::flushPendingCellUpdates and PhysicsManager::narrowPhase confirms the existing parallel calls complete before serial pair-delta application, contact/support reduction and later structural work. Existing queued-pair ownership and cancellation paths were not altered.
