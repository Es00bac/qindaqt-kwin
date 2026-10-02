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

## Native integration qualification fixture (source checkpoint)

`testNativeDisplayPower` is a noninstalled KWinIntegrationTestFramework target
under test-only session-lock authorization. It uses the real Workspace,
DisplayPower1 D-Bus object and separate ordinary/Session1-owner bus connections.
The fixture's Session1 name owner is admitted by the existing actual unique
sender/UID check; no authorizer or production permission is overridden.

Stock virtual output capability remains unsupported: the fixture starts from
VirtualOutput's actual CustomModes-only information and explicitly verifies that
restoring this information rejects admission. For supported rows only, a private
derived output advertises DPMS through BackendOutput's existing protected state
setter, selected through Application::setOutputBackend before startup. Its
upstream VirtualOutput::applyChanges and real Workspace animation/completion
perform output mode transitions. Because VirtualOutput is not exported from the
shared core, the target compiles the unchanged upstream virtual_output.cpp into
its private executable; it does not modify production output/backend code.
This proves native scoped routing over deterministic software nested outputs,
not physical DRM or hardware DPMS.

Authored rows cover real targeted epoch/inventory receipt, rejected ordinary and
stale-epoch mutation, unsupported/mixed/missing/off inventory, conflicting legacy
writer, topology withdrawal, actual output Off/On, same-mode/preexisting external
Off preservation, injected keyboard wake, finite expiry and Session1 owner loss.
Each test uses a fresh request ID so bounded retired-ID rejection cannot leak
between rows. Compile and runtime are pending manager's coherent warm-cache
lease; source alone is not qualification.

Desktop2944dcd41 exclusive activation additionally requires a separate gate.
At this checkpoint it suppresses its PowerDevil child before native composition
readiness and only logs failed native-lock/power composition. That cannot satisfy
“refuse exclusive activation without prerequisites”; this is a reported source
blocker, not a passing fixture or installed cutover claim.
