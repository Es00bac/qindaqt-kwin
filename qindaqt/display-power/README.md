# Scoped native display power (container-wm ADR-0338)

This source candidate adds finite compositor-owned blank causes to the existing
Workspace DPMS route. Production admission starts false. Only the actual current
Session1 unique sender, resolved by the bus daemon with this real UID, can admit
or mutate causes, and the legacy PowerDevil writer must be absent. Epochs are
nonzero random compositor-incarnation values. The supervisor uses its ordinary
attachment and authenticated nonce-correlated InventoryReceipt before admission.

AcquireBlank(epoch, requestId, deadlineMs) uses a 32-lowercase-hex request ID and
an absolute same-host Qt monotonic reference deadline, at most60seconds ahead.
The finite deadline prevents a delayed acquire/renew from reviving a canceled
request after its bounded cancel record expires. Same-connection release order
and owner loss remain the primary cancellation fences. At most64 live IDs and a
bounded retired-ID set are retained; exhausted capacity rejects new work. A
renewal uses the same ID. Return true means cause admitted, not physical blank
completion; inventory reports each actual backend's mode separately.

InventoryReceipt includes actual enabled, nonplaceholder backend identities,
DPMS capability, linked logical geometry and mode. Empty/incomplete coverage and
an unexplained mixed physical baseline cannot admit a new all-output blank.
Topology/capability changes withdraw composition. Ordinary DPMS mode events do
not manufacture topology loss. The supervisor must reacquire current receipts
and declare only genuinely composed consumers.

An external Off is recorded before Workspace's same-mode early return. Releasing
our last cause restores On only when the recorded external baseline permits it.
A preexisting or later external Off remains Off. A genuine existing On/wake
consumes every owned episode; a continuous closed lid cannot renew that episode.
Owner/legacy/admission loss and expiry release only our causes. This preserves
upstream input wake, animation and output routing, without changing layout or
disabling outputs. Unknown/mixed output provenance fails closed.

Only source/static checks have run for this next slice. Native compositor/bus
integration and installation/collision gates remain required before acceptance.
