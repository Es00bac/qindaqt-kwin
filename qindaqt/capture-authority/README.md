# Compositor capture authority

`CaptureAuthority` belongs to the selected QindaQt compositor. It owns a
capture-only broker process and one helper process per admitted portal job.
The general portal resident remains supervisor-owned. The public, installed
boundary is [`protocol.h`](protocol.h), generated `launchpaths.h`, and
[`WIRE.md`](WIRE.md); portal packages include those installed headers and do
not include this module's private C++ headers. The accepted rationale and
negative-case plan are in
[`PROTECTED_CAPTURE_AUTHORITY_PROPOSAL.md`](../PROTECTED_CAPTURE_AUTHORITY_PROPOSAL.md).

`CaptureLaunchPolicy` checks the fixed installed executable and desktop entry,
including root ownership and non-writable original/canonical parent
directories. It rejects missing Yama protection of the exec-to-PR0 interval.
`CaptureChild` creates the private seqpacket control socket and, for a helper,
separate ordinary GUI and restricted capture Wayland socketpairs and two
request/result pipes. It launches only the configured broker/helper image with
empty arguments and a sanitized environment. The children must set and verify
`PR_SET_DUMPABLE=0` before Qt, Wayland or D-Bus connection startup.

`CaptureAuthority` checks every control packet against kernel UID/PID, its
launched child and live pidfd, the current selected compositor owner, the
capture backend's current D-Bus owner, and the frontend/caller/helper unique
senders and live pidfds. The broker chooses monotonic job IDs but cannot
grant itself pixels: the helper must report parent import and a real consent
action before the compositor sends `CaptureReady`. Request/Session/app policy,
dialog choice and screenshot URI publication stay with the capture-only
broker/helper. The compositor retains only process/interface/lease authority.
Its lifecycle and actor checks live in `captureauthority.cpp`; role-owner
change handling lives in `captureauthorityowners.cpp`; the exported
Wayland and Screenshot/ScreenCast grants live in `captureauthorityinterfaces.cpp`
so process control and interface policy remain reviewable separately.

The exact helper capture `ClientConnection` is tagged before KWin's self-PID
shortcut. It sees only `wl_output`, `zxdg_output_manager_v1`, and, for a
ScreenCast job, `zkde_screencast_unstable_v1`. Every other global is denied on
that connection. The helper's GUI/foreign-parent connection is separate,
permits ordinary Qt globals and denies every restricted/lock/input-method/
XWayland/security-context global before the compositor self-PID shortcut.
Screenshot2 consumes one protected invocation grant and rechecks
native privacy and actor lineage through selection, sink, writer and reply.
ScreenCast checks the same current job at stream creation and before a frame is
queued. Revocation synchronously closes writers and streams before destroying
the tagged client. Bytes already delivered before a revoke cannot be recalled.

The default `QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION=OFF` build admits no
test executable override and ignores the old screenshot and Wayland
permission-bypass environment variables. An ON build requires `BUILD_TESTING`
and refuses installation. Its compile-only `WaylandServer` fixture setter
accepts disposable broker/helper paths **before** workspace initialization;
it does not alter production launch validation, public wire framing or current
native privacy gates. Focused wire and launch-policy unit tests exercise
framing, kernel credentials, fixed file trust and environment sanitization.
The full protected frontend, actual changing pixels/frames and revocation
matrix still requires the separately coordinated private native fixture; a
successful unit test or plugin build alone does not establish that behavior.


## Portal owner revocation regression

Loss or replacement of `org.freedesktop.portal.Desktop` immediately revokes
jobs bound to that event's old unique frontend sender, even if the sender and
its pidfd remain alive. Loss or replacement of the capture backend's well-known
name retires its selected broker and revokes every job. Matching the event's
old owner prevents a delayed event for a different actor from retiring newly
admitted authority. Existing current-owner checks still fence every invocation.

The noninstalled test-authorization target `testCaptureOwnerLoss` uses a
QtCore peer launched through the existing pre-workspace test-program seam.
It exchanges actual fd3 Hello/Ready and normal StartJob/ParentReady/
ConsentGranted/CaptureReady packets; it never seeds authority jobs or grants.
Its four rows cover frontend/backend release/replacement while the original
unique owner and kernel process are alive before mutation. Each row starts a
real Screenshot2 call from its admitted helper against a pipe prefilled to
EAGAIN, observes the actual writer's first POLLOUT wait, waits for queued
post-direct `jobRevoked` observers, then requires writer exit, a terminal error,
EOF, exactly the sentinel bytes and no successful reply or image bytes.
A simultaneous idle ScreenCast job has only a direct local closure observer;
this proves notification ordering, **not** actual PipeWire stream/node teardown,
real portal consent, or the separate native consumer matrix. The observer is
compiled only into the noninstallable test-authorization build and cannot
alter admission or publication. Run this target with its
`qindaqt/capture-authority/tests/run_owner_loss.py` zero-activation private
bus runner after coordinating the private runtime.
