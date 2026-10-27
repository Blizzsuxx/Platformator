# Lifecycle dispositions

C02 removes unused hierarchy depth and its recursive argument while retaining depth-first ordering and transform queues. C04 removes the unused Audio manager index and accessors; autoplay and transient-audio ownership remain. C05 removes only the unobserved manager total, preserving SimulationClock accounting. C06 removes an unused constructor; C07 and C08 remove uncalled registration/removal wrappers without altering live reconciliation. GameObject and Audio installed class layouts change, and Audio accessors are removed; all consumers must rebuild.
