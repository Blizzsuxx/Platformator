# Support identity reduction

ColliderPair now stores two nullable applied-or-queued contributor identities rather than two pointers plus two flags. reconcileSupportContribution handles identity changes and removal in one place; null means no credit. Canonical A/B mapping still uses current reference/incident support bits. Old recipients remain allocated through net support flush and retirement callbacks. The per-body signed delta, queue index and committed count are retained: a queued net-zero body still needs deduplication, and transient zero must not wake it. No new allocation-failure guarantee is added.

Debug support/bookkeeping/contact checks pass (three passes, one expected work-disabled skip). Work configuration: all four selected checks pass. Original output: us2-support-cleanup-debug.txt and us2-support-cleanup-work.txt. Replacement-before-free and net-zero transfer assertions pass with workers one/six. Current source has no old supportBodyA/B or pair supportsA/B consumers.
