# Proposed protected native capture authority

Status: **design candidate, not an implemented permission or runtime pass**. Base:
`690c0112d13ca7d861e070865c9054d657946b75` (tree
`b507de4f9b0deba11eb9ecee1980f0d593e4a4df`). Governing container-wm
contracts: accepted ADR-0318, proposed ADR-0324 and
`docs/wiki/reference/portal-capture.md` in the portal candidate; session-lock launch precedent is this fork's
`qindaqt/session-lock/{lockerlauncher,lockerlaunchpolicy,sessionlockcontroller}.cpp`.

## Why a new boundary is required

`src/wayland/clientconnection.cpp` reads `/proc/<pid>/exe` at client creation.
`src/wayland_server.cpp` then rejects an unidentified restricted-global client;
the requested desktop permissions in `src/utils/serviceutils.h` cannot repair
that missing executable. Independently,
`src/plugins/screenshot/screenshotdbusinterface2.cpp` gets the D-Bus caller PID
and calls `fetchRestrictedDBusInterfacesFromPid()` on every Screenshot2 call.
That also rereads `/proc/<pid>/exe`. The candidate's resident and native helper
set `PR_SET_DUMPABLE=0` before ordinary connections in
`src/services/portal/capture/main.cpp` and the resident entry. A task-owned
same-UID child probe already returned `EACCES` for that protected link, and a
fork690 private log could not identify the protected Wayland peer. The later
e1b06401 protected private probe directly observed mapped native Screenshot
`NoAuthorized` and frontend Screenshot Response 2/empty URI. Its ScreenCast
journey reached CreateSession response 0, SelectSources response 0, then Start
response 2 with empty streams; no frames or usability was qualified. The
ScreenCast Start failure is not assigned a cause by these observations alone.

Three possible repairs:

1. Delay `PR_SET_DUMPABLE=0` until after connection, cache a desktop path, or
   register a path/app ID before protection. Reject: the executable/exec gap
   exposes the process, an ordinary client can self-assert metadata, and a
   cached PID/path cannot prove the same protected image on reconnect.
2. Let any same-UID D-Bus caller request a compositor-side grant by PID or
   pidfd. Reject: PIDFD proves lifetime, not that a root-managed portal binary
   executed or that its request passed the frontend and consent checks. The
   caller could ask for pixels itself.
3. Compositor-launch the entire existing portal resident. Reject: ADR-0318
   assigns its optional-child and selected ordinary display attachment to the
   session supervisor. Moving that resident would change Settings, Secret and
   chooser lifecycles, introduce a Session1 startup/restart cycle, and create
   two possible owners for the same backend name.
4. **Choose a compositor-launched capture-only broker** with a private per-job
   authority. The existing general backend remains supervisor-owned. The new
   broker directly owns only the standard Screenshot/ScreenCast backend
   adaptors and is the sole process able to request a fixed, compositor-launched
   native helper over an inherited private channel. The compositor creates
   exact private Wayland clients and binds the helper's D-Bus sender to the
   same launch. No public method grants capture or launches a trusted process.
   This covers both entrypoints without a permission bypass or relaxed PR0.

## Public contract and process flow

The fork exposes one QindaQt-owned, documented `CaptureAuthority` boundary to
the separately built capture broker. It is an FD handoff/control protocol, not a
desktop-file lookup replacement or a new general privileged D-Bus method. A
versioned, bounded message type carries a job ID and interface kind; no path,
app ID, PID, nonce, or environment string supplied by an arbitrary client is
itself authorization. The public client side is a small `compositor_capture`
adapter; policy remains in the capture broker, presentation remains in the native helper.
No fork-private headers enter container-wm.

1. After becoming the selected current `org.qindaqt.KWin` owner on the session
   bus, the compositor launches exactly one installed capture broker from a
   compiled-in absolute executable and desktop-entry path. Both paths and all
   parent directories must satisfy the root-owned, non-group/world-writable
   checks used by `lockerlaunchpolicy.cpp`; the desktop entry must name exactly
   that executable and only the declared capture interfaces. Fail closed if
   either check, the restricted Yama policy, no-core setup, or launch fails.
   The compositor supplies a `SOCK_CLOEXEC` private control socketpair with
   one intentional inherited FD, a minimal allowlisted environment, and no
   ambient capture display. It retains QProcess lifetime and a pidfd. The
   broker applies no-core and `PR_SET_DUMPABLE=0` before Qt, D-Bus, Wayland,
   PipeWire, or helper startup, then sends a bounded control hello. The
   compositor verifies kernel credentials on a *post-exec message* and matches
   UID/PID with its child launch and live pidfd. Inherited socketpair
   `SO_PEERCRED` alone is not enough: the pair was made in the compositor and
   can report its creator's credentials. The private channel is the capability;
   the actual current D-Bus unique owner/PID/UID and compositor generation are
   checked before any job. The broker registers its own
   `org.freedesktop.impl.portal.desktop.qindaqt.capture` standard backend name,
   with only Screenshot/ScreenCast adaptors. No D-Bus activation file may start
   this protected broker without the compositor's private inherited channel.
   If the compositor or broker is unavailable, capture fails closed. A bounded
   compositor restart creates a new generation; old Requests and Sessions die
   with the old backend unique owner. The general portal backend continues its
   ADR-0318 supervisor launch and reattachment for unrelated families.
2. The capture broker authenticates the actual
   `org.freedesktop.portal.Desktop` unique owner and caller UID/PIDFD using
   the public `RequestRegistry` and `CaptureSessions` classes, then confirms
   the current compositor owner/PID/pidfd equals its private launching parent.
   It owns app identity as *frontend-supplied
   policy data*, bounded Request/Session handles, selected parent and consent
   state. It receives primary frontend calls directly through separate capture
   `.portal` metadata; it never joins through the general resident's
   `AttachSessionWithDisplay`. That method remains an ordinary supervisor
   attachment and cannot mint capture authority. A same-UID process can request
   normal portal capture, which still needs a native prompt, but cannot ask the
   compositor to launch or enroll a privileged process.
3. For one live request/session the broker sends a bounded job intent over
   the private channel. The compositor verifies its channel, current owner,
   native privacy (unlocked and known), selected generation, and that the
   claimed frontend/caller unique D-Bus names still resolve to the supplied
   UID/PID with live pidfds. This is defense in depth; the compositor does not
   parse portal options or become the portal Request registry. The compositor
   launches only the compiled-in, root-managed native helper, with the same
   launch hardening. It creates one ordinary GUI Wayland pair for Qt UI and
   xdg-foreign parent import and a **separate** restricted capture pair via
   `Display::createClient()`; the latter's exact
   `ClientConnection *` is tagged with this job and the sole
   `zkde_screencast_unstable_v1` enumeration grant. The existing portal resident-supplied
   restricted Wayland FD is replaced by this compositor-supplied FD; neither
   the general resident nor the broker can manufacture a grant by calling
   `CompositorAttachment::openConnection()`. The helper inherits only these
   required FDs and a private job channel. Its post-exec credential hello,
   process PIDFD, and selected generation must match before admission.
4. The helper imports a nonempty foreign parent, presents the real native
   Allow/Share choice, and reports parent/consent lifetime directly to the
   compositor through its private job channel. Empty parent is explicitly
   unparented. The compositor grants
   **Screenshot2** only to the helper's exact D-Bus unique sender after the
   daemon reports the launched UID/PID and the private helper channel confirms
   the same live pidfd; grant scope is one job and interface, with no general
   restricted-D-Bus permission. For **ScreenCast** it exposes the restricted
   global only on the job's exact private Wayland client and allows stream
   creation only while that job is authorized. The current public
   `WaylandScreenCast` binds the restricted manager while it enumerates outputs
   for the selection UI, before Share is pressed. Preserve that early binding
   if needed, but add a compositor-side job/consent check at every
   `stream_output` creation and before frame production; a client-side
   `pixelsAllowed()` callback alone cannot enforce consent. The capture client
   receives only the globals used by that public port; no other globals or
   Screenshot2 methods gain authority by implication.
5. The capture broker owns Close, Session close, frontend/caller owner loss, parent
   loss notification, helper response validation, and final publication. It
   sends revoke over the private channel immediately when any of these retire
   the job, then rechecks the same live lineage and privacy immediately before
   returning URI/node. Helper result alone is never a grant. The compositor
   directly revokes on native lock or unknown lock state, compositor owner or
   generation change, broker/helper control HUP or process exit, D-Bus sender
   replacement, caller/frontend PIDFD exit, and a bounded pre-consent timeout.
   It destroys the tagged Wayland client, closes active streams, cancels
   in-flight Screenshot2 work **and its pipe writer**, and removes the exact D-Bus sender grant. A
   stream must not survive a revoked job even if the ordinary Wayland socket
   remains open elsewhere. Screenshot pixel publication must check the grant
   again at completion using an explicit current lock check. Restart or
   ordinary reconnect creates a *new* generation/job only through a new
   compositor launch and frontend request/consent; old FDs and unique names
   cannot authorize it. The helper's parent-loss message also revokes directly
   at the compositor; the broker independently checks parent before publication.

## Mandatory fork admission ordering for private capture clients

The private socketpair's creator is the compositor. `Display::createClient()`
constructs `ClientConnection` while it still owns both ends; the cached
`wl_client_get_credentials` PID can therefore be `getpid()`. Fork690
`KWinDisplay::allowInterface()` returns true on that self-PID at
`src/wayland_server.cpp:165-167`, before the blacklist and input-method/XWayland
checks. The capture-specific branch **must precede the self-PID return and all
generic allow paths**. Lookup uses the exact compositor-owned
`ClientConnection *` tagged to a live job, never the cached PID, executable
path or app ID.

For a tagged capture client, that branch returns an answer for **every**
global: `wl_output` and `zxdg_output_manager_v1` are allowed for monitor
enumeration; `zkde_screencast_unstable_v1` is visible only while the
launch-authenticated job/generation is live; every other global is denied by
default. The current public `WaylandScreenCast`
(`src/services/compositor_capture/src/wayland_screencast.cpp`) binds only those
three globals. In particular, the branch denies all other
`interfacesBlackList` entries (`org_kde_plasma_window_management`,
`org_kde_kwin_fake_input`, `org_kde_plasma_activation_feedback`,
`kde_lockscreen_overlay_v1`), `ext_session_lock_manager_v1`, both
`inputmethodInterfaces`, both `xwaylandInterfaces`, and
`wp_security_context_manager_v1`. Its default-deny behavior also covers a
new sensitive global added later. Ordinary GUI/foreign-parent work uses the
separate ordinary client and its existing filter. A ScreenCast manager binding
before user consent authorizes enumeration only: the compositor checks live
job, actual helper consent, selected generation and unlocked/known privacy at
each `stream_output` request and before frame production. Retiring the job
destroys its exact capture client and stream; a new socketpair cannot inherit
the retired pointer's tag.

## Complete Screenshot2 method and delayed-delivery policy

Fork690 XML exports seven methods: `CaptureWindow`, `CaptureActiveWindow`,
`CaptureArea`, `CaptureScreen`, `CaptureActiveScreen`, `CaptureInteractive` and
`CaptureWorkspace`. All seven must enter one authorization decision at
invocation, before duplicating the caller's pipe or starting selection. The
decision requires known/unlocked native privacy and either (a) the live exact
capture job's helper D-Bus unique sender, daemon UID/PID, pidfd and generation,
or (b) the existing restricted desktop-entry permission for an ordinary
identifiable legacy client. Option (b) preserves already authorized Spectacle
use; it is not a new same-UID privilege. `CaptureInteractive` additionally
requires its actual click/selection, but selection alone is not authorization.
An untrusted same-UID caller without (a) or (b) receives an error and zero
pipe bytes even if it supplies a writable pipe. A failed identity/lock lookup
fails closed, including for `CaptureInteractive`.

Each accepted invocation holds a revocable operation lease with exact sender,
pidfd, permission kind, compositor privacy generation and, for (a), capture
job generation. `CaptureInteractive` stores this lease in both window/position
callbacks; cancellation or late selection after lock, owner change or broker
Request.Close closes the duplicated pipe and sends at most one failure, never
calls `takeScreenShot`. All seven methods recheck the lease immediately before
`takeScreenShot`, at sink `flush`, and before any success reply. A late frame
or sink completion after revocation discards its image, closes the pipe and
cannot emit a new success reply. Each operation has a single terminal reply
state: revocation may send a failure only if no reply was sent already, and a
pre-revocation legacy success cannot be followed by a contradictory failure.

`ScreenShotSinkPipe2::flush()` currently sends success before launching
`ScreenShotWriter2`; the worker then may poll/write for 60 seconds without
checking lock or grant (`screenshotdbusinterface2.cpp:35-99,191-209`). The
revocable lease must travel into that worker. Revoke marks it terminal, wakes
any stalled poll, and establishes a short synchronized barrier with each
nonblocking write; after revoke completes, no further write is allowed. The
worker must not call Qt/Wayland lock objects from its thread; the compositor
updates the shared lease on the owning thread. For a protected capture-job
lease, send success only after the complete write and a final live check; the
public `KWinCapturePort` already drains the pipe concurrently with its pending
D-Bus reply (`kwin_capture_port.cpp:89-129,179-205`). This also prevents a
success reply when revocation interrupts a stalled writer. Legacy authorized
clients may retain reply-before-bytes ordering for compatibility, but that
reply must be sent only while its lease is live, and its writer must stop at
revoke. A success reply or bytes sent *before* revocation cannot be recalled;
the broker must still refuse a URI if the write is truncated or lineage is
stale. For a pipe stalled before its first write, releasing it after revoke
must produce zero bytes and no later success reply. Canceling a pending D-Bus
reply alone does not stop this writer.

### One bounded protected write/reply deadline

The protected reply-after-complete-write contract needs the *same total
deadline* on both sides. Today `KWinCaptureCall::timeoutMilliseconds` defaults
to 15,000 ms (`capture_request.h:49`), `KWinCapturePort` passes that to
`m_bus.asyncCall()` and separately times pipe completion at that value plus
`PipeGraceMilliseconds` 10,000 (`kwin_capture_port.cpp:27,107,127`). A valid
16–20-second continuously drained write would therefore lose its D-Bus call
at 15 seconds if success follows the write. Do not use that legacy split for
the protected capture job.

Publish one versioned `ProtectedCaptureDeadline` in the installed
`CaptureAuthority`/public `CompositorCapture` contract: **30,000 ms total from
the accepted Screenshot2 invocation to both complete pipe EOF and terminal
D-Bus reply**. The fork's protected writer must finish, close its pipe and
send its final live-lease success reply by **28,000 ms** from the same
invocation; the remaining 2,000 ms is an explicit transport/order margin.
The public port starts a monotonic 30,000-ms total timer with the protected
call and sets its pending D-Bus timeout to that same 30,000 ms, never the
default 15,000 ms; it succeeds only after complete bytes/EOF, success reply
and current lineage. Fork and port use the same versioned constants/epoch,
not separately guessed timeouts. On either budget expiry, cancellation or
revoke, close the pipe, wake/retire the writer, send at most one terminal error
if no reply exists, and discard any late reply or bytes. The broker must not
publish a URI from a timeout or partially drained pipe.

Only a compositor-authenticated capture-job lease selects this protected
deadline; untrusted callers cannot request it through an option or app ID.
The pre-existing ordinary desktop-entry client timing, including
`CaptureInteractive`'s selection allowance, remains its compatibility
contract. If a protected job invokes `CaptureInteractive`, its selection and
write share the same 30-second total; an expired selection fails closed. The
native helper's existing 60-second pre-consent expiry must be
retired when Allow starts capture, so a late but valid consent does not cut
short this 30-second operation; Close/parent/privacy loss still cancels it.
A protected write that takes longer than 28 seconds fails closed, even if
the pipe remains writable.

The compositor's grant record holds only generation, job ID, two exact client
pointers, launched helper UID/PID/pidfd, its private channel, one exact D-Bus
unique sender, frontend/caller unique names and pidfds, and an expiry/state.
It stores no app permission database, screenshot bytes, portal options or
parent widgets. Broker and helper remain independently protected by PR0.

`execve` resets dumpability before helper/broker `main`; the existing native
lock policy requires Yama `ptrace_scope` 1–3 to cover this unavoidable gap,
then the trusted child applies PR0 and zero core limits before loading Qt or
opening connections. Reuse that *policy*, not the lock authority object or its
private connection. Same-UID replacement of installed files or a writable
ancestor must fail root-managed checks. A PID can be recycled before a late
pidfd open; private inherited channel continuity, post-exec kernel message
credentials, exact D-Bus unique sender, and immediate revocation on HUP/exit
are all required in addition to pidfd. Do not use a socketpair's creation-time
peer credentials or `ClientConnection::processId()` as proof of the child.

## Implementation ownership and compatibility

- Fork owner: new `qindaqt/capture-authority/` private launcher, registry and
  focused tests plus explicit public wire/header documentation; a capture
  branch before the self-PID shortcut in `src/wayland_server.cpp`, covering
  exactly the tagged three-global allowlist; all-seven-method invocation,
  selection callback, sink, reply and asynchronous writer lease checks in
  `src/plugins/screenshot/screenshotdbusinterface2.cpp`, including the
  protected 28-second writer/reply bound; and stream/job
  creation and revoke gates in `src/plugins/screencast/`.
  Preserve ordinary desktop-file permission behavior for unrelated clients and
  the deliberate legacy `org.kde.KWin.ScreenShot2` wire name until PF21.
- Portal owner: public adapter in `src/services/compositor_capture/`, including
  coordinated protected call/port total deadlines in
  `include/qindaqt/services/compositor_capture/capture_request.h` and
  `src/kwin_capture_port.cpp`; new
  capture-only `src/services/portal/capture_backend/` executable composed from
  the existing capture adaptors, `RequestRegistry` and `CaptureSessions`, plus
  helper FD/consent/pre-consent expiry wiring in
  `src/services/portal/capture/`. Remove capture
  adaptors from `src/services/portal/src/foundation_composition.cpp` only when
  the separate backend qualifies. Its own `.portal` metadata and backend name
  route Screenshot/ScreenCast only after native gates; the general resident and
  `PortalSessionLifetime` retain ADR-0318's ownership. Add a new/superseding
  container-wm ADR for this process boundary and update ADR-0324, capture
  architecture/reference, package entries and native tests together.
- The compositor owns process authority and revocation; the capture backend owns user
  consent, parent/window lifetime, frontend Request/Session logic, PipeWire
  remote filtering and publication. The generated installed protocol remains
  the only Wayland build dependency of `CompositorCapture`.
- Do not install a D-Bus activation command that starts the protected capture
  broker without its private inherited channel. If frontend startup precedes
  compositor-owned broker registration, the bounded request fails and a new
  user request can retry. Broker loss withdraws its unique backend owner and
  the compositor retires all grants. A bounded fixed-binary restart gets a new
  generation. General backend restart keeps its supervisor reattachment and
  cannot confer capture authority. If a same-UID process preclaims the capture
  backend bus name, the real broker must fail to claim it and capture remains
  unavailable; it must not hand that process a grant.

## Executable acceptance plan (no private slot used for this proposal)

1. Fork unit/integration fixtures: use a deliberately root-managed test
   executable only under a compile-time test flag. Assert an ordinary same-UID
   client, a self-launched copy of the correct executable, a self-reported
   desktop/app ID, forged job message, wrong UID/PID, stale pidfd, reused PID,
   changed bus unique sender, and `CompositorAttachment::openConnection()` all
   fail both restricted entrypoints. Assert owned helper PR0 via
   `PR_GET_DUMPABLE==0`; missing Yama/root-managed files fail closed. Assert
   exact private ScreenCast client and exact helper D-Bus sender succeed only
   for their own live job. Create a real compositor-owned socketpair, pass its
   peer to the protected helper, and assert its actual `ClientConnection`
   credentials include the self-PID case. The tagged capture client must bind
   only `wl_output`, `zxdg_output_manager_v1` and the planned ScreenCast manager:
   deny the four other blacklist names, lock/overlay, both input-method and
   both XWayland globals, `wp_security_context_manager_v1`, an unknown global,
   and a newly added restricted test global. Assert ordinary same-UID and
   self-launched copies gain no tag; destroy/reconnect after revocation cannot
   reuse it. Test this through `KWinDisplay::allowInterface`, not a policy-only
   substitute, so the early self-PID return is exercised.
2. Revocation fixtures: Close Request/Session, frontend/caller owner or pidfd
   loss, parent import/loss, helper/broker crash, private channel HUP,
   compositor owner replacement, native lock/unknown, pre-consent timeout,
   ordinary reconnect, and late screenshot/stream completion must remove the
   job and stop frames. Exercise both a grant before creation and a live stream
   or in-flight screenshot. Invoke **all seven** Screenshot2 methods with an
   untrusted same-UID sender and verify denial/zero bytes, especially
   `CaptureInteractive` with a writable pipe. Preserve a desktop-entry
   authorized interactive client and its real selection; it still fails on
   locked/unknown privacy. For protected jobs, retire during pending window
   and position selection by lock, compositor owner replacement and
   Request.Close; no `takeScreenShot`, pipe bytes, late success or URI. Retire
   between accepted request and frame/sink completion, and during a stalled
   `ScreenShotWriter2` poll; wake/release the pipe after revoke and assert no
   **post-revocation** bytes, no later success reply/URI, and timely writer
   exit. Check no stale node, screenshot-file residue or descendant processes.
   A deterministic fork/public-port fixture must also pace a protected
   screenshot pipe with a **continuously draining** reader so the complete
   write takes 16–20 seconds (including an exact 20-second boundary row).
   Assert real bytes/EOF and one success reply before the shared 30-second
   total deadline; this must fail under the old 15-second D-Bus timeout.
   Repeat with cancellation and lock/request revoke mid-write: writer stops
   promptly, no writes follow the revoke barrier, no later success/URI, and
   any late D-Bus reply is ignored by port serial/generation. A controlled
   >28-second writer and 30-second total expiry both fail once without
   extending the deadline or leaking a process/pipe.
3. In an isolated actual frontend run, keep the `run_native_capture.py`
   three-group default matrix in `tests/services/portal/capture/`; use the
   corrected protected caller/resident probe with its current mapped native
   Screenshot `NoAuthorized`/frontend Response 2, and ScreenCast
   CreateSession 0, SelectSources 0, Start 2/empty streams. Neither provides
   native pixels or frames. No `KWIN_SCREENSHOT_NO_PERMISSION_CHECKS` or
   `KWIN_WAYLAND_NO_PERMISSION_CHECKS`, no dumpable controller, host/root
   fallback, or fabricated PipeWire remote. Require actual changing mapped
   pixels, PickColor selection, an actual frontend-returned PipeWire FD with
   decodable changing frames, and cessation on Close/lock/owner/parent loss.
   The full default matrix, not a selected `--case`, qualifies this gate.
4. Build fork focused tests and compositor, portal focused policy/request tests
   and native target, then run strict docs/link/source-shape and staged install
   collision/desktop-entry/activation/selector gates. Assert the general
   backend remains supervisor-owned and its replacement reattaches ordinary
   authority; new capture backend can register, fail, restart and retire its
   own frontend sessions without invoking Session1 or displacing other portal
   families. Manager owns compiler/private runtime slots,
   combined-tree rerun, installed routing cutover, and current hub/checkout
   synchronization. This design commit intentionally runs no compiler or
   private native gate and claims no capture usability.
