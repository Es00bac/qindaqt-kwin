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

The coordinated private boundary uses the standard `VirtualBackend` and
`NoopSession`, virtual outputs, `LIBGL_ALWAYS_SOFTWARE=1`, explicit
`MESA_LOADER_DRIVER_OVERRIDE=swrast`/`GALLIUM_DRIVER=llvmpipe`, and one explicitly
identified render node (`/dev/dri/renderD128`) for buffer allocation. Read-only
sysfs metadata permits libdrm discovery. No primary/card node, input/audio
node, host bus or display is exposed. Setup requires a real EGL backend and
an actual llvmpipe renderer; software fallback to QPainter fails. A render
node permits render/allocation ioctls but cannot mode-set the host display.
This is a narrowly approved exception to the earlier device-free namespace,
not evidence of physical-display or real portal/PipeWire coverage. The prior
custom software backend experiment is preserved on a separate branch and
is absent from this fixture.


### Bounded owner-loss qualification evidence

Executable source `48d2f2dd0f82f9c45f6a5a880d98292cb4cd5efd` passed the
coordinated render-only namespace on 2026-10-01. Its actual GL renderer was
`llvmpipe (LLVM 22.1.8, 256 bits)`. The changed fixture target rebuilt with
`cmake --build build/focused --target testCaptureOwnerLoss -- -j8 -l24`
(exit 0, three effective Ninja steps). Inside the approved namespace:

| Command | Qt results | Exit |
| --- | --- | --- |
| `python3 -I -B qindaqt/capture-authority/tests/run_owner_loss.py build/focused/bin/testCaptureOwnerLoss` | 6 passed, 0 failed; all four role-loss rows | 0 |
| `build/focused/bin/testCaptureWire` | 15 passed, 0 failed | 0 |
| `build/focused/bin/testCaptureLaunchPolicy` | 4 passed, 0 failed | 0 |
| `python3 -I -B qindaqt/tools/run-native-lock build/focused/bin/testCaptureInterfaceFilter` | 3 passed, 0 failed | 0 |

The matched negative-control source
`21bd149c9087399874ea04533aeca84b5cf1e30c` differs from that executable
source only by omitting the constructor's `watchPortalOwners()` call. Its
identical rebuilt fixture (exit 0, three effective Ninja steps) ran only
`ownerLoss:backend-replacement`: Qt 2 passed/1 failed, exit 1, at the expected
queued-revocation barrier after the actual writer wait. Its cleanup passed;
this establishes that the role watcher supplies the tested revocation.

Raw commands, image hashes, logs, timing and descendant inventories are
preserved in ignored `build/namespace-owner-loss-render-only-48d2/` and
`build/namespace-owner-loss-negative-21bd/` evidence directories. Every phase
had zero owned survivors, scoped core files or timeouts. The earlier
`115378c2e453e3d31ab4033bd04f896ee5987b4c` prerequisite failure remains in
`build/namespace-owner-loss-render-only/`: Qt 1 passed/1 failed, exit 1,
`isSoftwareRenderer()` false before any owner-loss row. Explicit swrast and
llvmpipe selection was the causal changed input; no assertion was weakened.

The bounded runs also emitted denied DRM dumb-buffer allocation and virtual
output-layer warnings. They qualify the actual Screenshot2 cancellation path
and its real blocked writer, not successful virtual output presentation or
changing captured pixels. ScreenCast coverage remains a local idle closure
observer; the full native frontend/consent/PipeWire matrix remains a separate
held gate. Request independent review of the exact handoff commit, then the
manager's matching native-driver rebuild and real frontend matrix.


## ScreenCast source lifetime during revocation

Stream closure synchronously marks the stream closed, stops its pending frame,
disconnects cursor/source notifications and emits the existing closure. Every
later frame entry and the final pre-queue authority check remain fail closed.
The QObject source is transferred from unique ownership to the closing stream
as parent, and retired with `deleteLater()` on the compositor thread. The stream
owns fallback cleanup if it dies before the deferred event. PipeWire callbacks
are dispatched on that thread through the existing Qt socket notifier.

The physical source destruction must wait until the current synchronous scene
notification unwinds: `Item::scheduleSceneRepaintInternal()` holds a snapshot
of raw scene-view pointers, and `OutputLayer::repaintScheduled` can synchronously
reach `ScreenCastStream::scheduleRecord()` and close when native lock begins.
Immediate source deletion frees its later cursor/scene views while that snapshot
is still being traversed. Exact447 failed the real native-lock consumer row's
compositor-survival assertion; kernel fault context and the first return address
place the invalid virtual dispatch at item.cpp508 (`view->shouldRenderItem`).
The logical revocation stays immediate; no new pixels may be queued while the
source waits for deletion. Ready and active-record continuations recheck closed
state before dereferencing source ownership after a reentrant callback.

Focused behavioral regression is the existing unchanged native consumer
`nativeLockStopsActualStreamPendingCaptureAndRetainedFile` **and** its runner's
private-compositor-survival assertion, followed by the full seven-case/three-
compositor matrix. A Qt3/3 row accompanied by compositor death is a failure.
The pre-repair original447 and two observer-driver failures remain preserved.
Executable repair42604a8f passes the bounded plugin build and one unchanged
native-lock row with original447 driver, actual AMD rendering and a scoped
private-bus monitor: Qt3/3, runner exit0, compositor alive through cleanup,
lockedChanged(true), protectedChanged(true), real stream/file retirement and
post-lock refusal. The exact same unmonitored row first failed its initial
Screenshot before lock (response2/no helper). Monitoring shifts timing, so that
recurring admission prerequisite remains unresolved; the full seven-case/three-
compositor matrix and independent review remain required. All failures are
retained. No production routing or full capture milestone is claimed.
